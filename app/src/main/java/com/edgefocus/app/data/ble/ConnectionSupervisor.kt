package com.edgefocus.app.data.ble

import com.edgefocus.app.data.ble.model.HostConnectionPhase
import com.edgefocus.app.data.ble.model.HostConnectionSnapshot
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

interface HostConnectionTransport {
    val rawConnectionState: StateFlow<ConnectionState>
    suspend fun clientHello(): Result<ByteArray>
    suspend fun getCurrentState(): Result<ByteArray>
    fun shouldAutoReconnect(): Boolean
    fun requestReconnect(): Boolean
    fun cancelStaleConnectionAttempt(): Boolean
}

@Singleton
class BleHostConnectionTransport @Inject constructor(
    private val bleManager: BleManager,
    private val autoConnectGate: BleAutoConnectGate
) : HostConnectionTransport {
    override val rawConnectionState: StateFlow<ConnectionState> = bleManager.connectionState
    override suspend fun clientHello(): Result<ByteArray> = bleManager.clientHello()
    override suspend fun getCurrentState(): Result<ByteArray> = bleManager.getCurrentState()
    override fun shouldAutoReconnect(): Boolean =
        bleManager.shouldAutoReconnect() &&
            autoConnectGate.evaluate(rawConnectionState.value).allowed

    override fun requestReconnect(): Boolean {
        if (!autoConnectGate.evaluate(rawConnectionState.value).allowed) return false
        return bleManager.requestReconnect()
    }

    override fun cancelStaleConnectionAttempt(): Boolean =
        bleManager.cancelStaleConnectionAttempt()
}

@Singleton
class ConnectionSupervisor @Inject constructor(
    private val transport: HostConnectionTransport
) {
    private var scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val started = AtomicBoolean(false)
    private val epoch = AtomicLong(0)
    private var reconnectJob: Job? = null
    private var connectionTimeoutJob: Job? = null
    private var reconnectBackoffIndex = 0

    private val _snapshot = MutableStateFlow(HostConnectionSnapshot())
    val snapshot: StateFlow<HostConnectionSnapshot> = _snapshot.asStateFlow()

    internal constructor(
        transport: HostConnectionTransport,
        testScope: CoroutineScope
    ) : this(transport) {
        scope = testScope
    }

    fun start() {
        if (!started.compareAndSet(false, true)) return
        scope.launch {
            transport.rawConnectionState.collectLatest { rawState ->
                when (rawState) {
                    ConnectionState.CONNECTED -> negotiate()
                    ConnectionState.CONNECTING -> {
                        reconnectJob?.cancel()
                        scheduleConnectionTimeout()
                        _snapshot.value = _snapshot.value.copy(
                            phase = HostConnectionPhase.CONNECTING,
                            error = null
                        )
                    }
                    ConnectionState.SCANNING -> {
                        reconnectJob?.cancel()
                        connectionTimeoutJob?.cancel()
                        _snapshot.value = _snapshot.value.copy(
                            phase = HostConnectionPhase.CONNECTING,
                            error = null
                        )
                    }
                    ConnectionState.DISCONNECTED, ConnectionState.ERROR -> {
                        connectionTimeoutJob?.cancel()
                        _snapshot.value = HostConnectionSnapshot(
                            phase = HostConnectionPhase.DISCONNECTED,
                            connectionEpoch = _snapshot.value.connectionEpoch,
                            error = if (rawState == ConnectionState.ERROR) "gatt_error" else null
                        )
                        scheduleReconnectIfNeeded()
                    }
                }
            }
        }
    }

    fun renegotiate() {
        if (transport.rawConnectionState.value != ConnectionState.CONNECTED) return
        scope.launch { negotiate() }
    }

    suspend fun refreshHostState(): Result<com.edgefocus.app.data.ble.model.HostStateSnapshot> = runCatching {
        check(transport.rawConnectionState.value == ConnectionState.CONNECTED) { "主机未连接" }
        val current = _snapshot.value
        val expectedEpoch = current.connectionEpoch
        check(current.hello != null) { "主机能力尚未协商" }
        val bytes = transport.getCurrentState().getOrThrow()
        val state = HostJsonCodec.parseState(String(bytes, Charsets.UTF_8))
        check(state.session.isApplicable) { "主机状态缺少必要字段" }
        val latest = _snapshot.value
        check(
            transport.rawConnectionState.value == ConnectionState.CONNECTED &&
                latest.phase == HostConnectionPhase.READY &&
                latest.connectionEpoch == expectedEpoch
        ) { "host_connection_changed" }
        check(_snapshot.compareAndSet(latest, latest.copy(hostState = state, error = null))) {
            "host_connection_changed"
        }
        state
    }

    private suspend fun negotiate() {
        reconnectJob?.cancel()
        connectionTimeoutJob?.cancel()
        val nextEpoch = epoch.incrementAndGet()
        _snapshot.value = HostConnectionSnapshot(
            phase = HostConnectionPhase.SUBSCRIBED,
            connectionEpoch = nextEpoch
        )
        _snapshot.value = _snapshot.value.copy(phase = HostConnectionPhase.NEGOTIATING)

        val helloBytes = transport.clientHello().getOrElse {
            degrade(nextEpoch, "client_hello: ${it.message}")
            return
        }
        val hello = runCatching {
            HostJsonCodec.parseClientHello(String(helloBytes, Charsets.UTF_8))
        }.getOrElse {
            degrade(nextEpoch, "client_hello_parse: ${it.message}")
            return
        }
        if (hello.schemaVersion != "2.1") {
            degrade(nextEpoch, "unsupported_schema_${hello.schemaVersion}", hello = hello)
            return
        }

        val stateBytes = transport.getCurrentState().getOrElse {
            degrade(nextEpoch, "get_current_state: ${it.message}", hello = hello)
            return
        }
        val state = runCatching {
            HostJsonCodec.parseState(String(stateBytes, Charsets.UTF_8))
        }.getOrElse {
            degrade(nextEpoch, "state_parse: ${it.message}", hello = hello)
            return
        }
        if (!state.session.isApplicable) {
            degrade(nextEpoch, "state_missing_required_fields", hello = hello)
            return
        }

        _snapshot.value = HostConnectionSnapshot(
            phase = HostConnectionPhase.READY,
            connectionEpoch = nextEpoch,
            hello = hello,
            hostState = state
        )
        reconnectBackoffIndex = 0
    }

    private fun degrade(
        connectionEpoch: Long,
        message: String,
        hello: com.edgefocus.app.data.ble.model.ClientHelloResult? = null
    ) {
        _snapshot.value = HostConnectionSnapshot(
            phase = HostConnectionPhase.DEGRADED,
            connectionEpoch = connectionEpoch,
            hello = hello,
            error = message
        )
    }

    private fun scheduleReconnectIfNeeded() {
        if (!transport.shouldAutoReconnect()) return
        if (reconnectJob?.isActive == true) return
        reconnectJob = scope.launch {
            val delays = listOf(1_000L, 2_000L, 4_000L, 8_000L, 16_000L, 30_000L)
            for (index in reconnectBackoffIndex until delays.size) {
                val backoff = delays[index]
                delay(backoff)
                if (transport.rawConnectionState.value != ConnectionState.DISCONNECTED &&
                    transport.rawConnectionState.value != ConnectionState.ERROR
                ) return@launch
                if (!transport.shouldAutoReconnect()) return@launch
                if (transport.requestReconnect()) {
                    reconnectBackoffIndex = (index + 1).coerceAtMost(delays.lastIndex)
                    return@launch
                }
            }
        }
    }

    private fun scheduleConnectionTimeout() {
        if (connectionTimeoutJob?.isActive == true) return
        connectionTimeoutJob = scope.launch {
            delay(CONNECTION_ATTEMPT_TIMEOUT_MS)
            if (transport.rawConnectionState.value == ConnectionState.CONNECTING) {
                transport.cancelStaleConnectionAttempt()
            }
        }
    }

    companion object {
        private const val CONNECTION_ATTEMPT_TIMEOUT_MS = 15_000L
    }
}
