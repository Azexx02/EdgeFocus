package com.edgefocus.app.data.ble

import com.edgefocus.app.data.ble.model.HostConnectionPhase
import com.edgefocus.app.data.remote.ConnectionState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ConnectionSupervisorTest {
    @Test
    fun `ready requires hello then authoritative state`() = runTest {
        val transport = FakeTransport()
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        transport.rawConnectionState.value = ConnectionState.CONNECTED
        advanceUntilIdle()

        assertEquals(listOf("hello", "state"), transport.calls)
        assertEquals(HostConnectionPhase.READY, supervisor.snapshot.value.phase)
        assertEquals("2.1", supervisor.snapshot.value.hello?.schemaVersion)
        assertNotNull(supervisor.snapshot.value.hostState)
    }

    @Test
    fun `missing phase degrades instead of inventing idle`() = runTest {
        val transport = FakeTransport(
            stateResponse = """{"status":"success","data":{"session_id":"session_1"}}"""
        )
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        transport.rawConnectionState.value = ConnectionState.CONNECTED
        advanceUntilIdle()

        assertEquals(HostConnectionPhase.DEGRADED, supervisor.snapshot.value.phase)
        assertEquals("state_missing_required_fields", supervisor.snapshot.value.error)
    }

    @Test
    fun `late state response cannot restore ready after disconnect`() = runTest {
        val transport = FakeTransport()
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        transport.rawConnectionState.value = ConnectionState.CONNECTED
        advanceUntilIdle()
        assertEquals(HostConnectionPhase.READY, supervisor.snapshot.value.phase)

        transport.stateGate = CompletableDeferred()
        val refresh = async { supervisor.refreshHostState() }
        runCurrent()
        transport.rawConnectionState.value = ConnectionState.DISCONNECTED
        runCurrent()
        transport.stateGate?.complete(Unit)
        runCurrent()

        assertTrue(refresh.await().isFailure)
        assertEquals(HostConnectionPhase.DISCONNECTED, supervisor.snapshot.value.phase)
    }

    @Test
    fun `cold start disconnect schedules reconnect without previous ready state`() = runTest {
        val transport = FakeTransport(autoReconnect = true)
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        runCurrent()
        advanceTimeBy(1_000)
        runCurrent()

        assertEquals(1, transport.reconnectRequests)
    }

    @Test
    fun `repeated cold start failures advance reconnect backoff`() = runTest {
        val transport = FakeTransport(autoReconnect = true)
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        runCurrent()
        advanceTimeBy(1_000)
        runCurrent()
        assertEquals(1, transport.reconnectRequests)

        transport.rawConnectionState.value = ConnectionState.CONNECTING
        runCurrent()
        transport.rawConnectionState.value = ConnectionState.ERROR
        runCurrent()
        advanceTimeBy(1_999)
        runCurrent()
        assertEquals(1, transport.reconnectRequests)
        advanceTimeBy(1)
        runCurrent()
        assertEquals(2, transport.reconnectRequests)
    }

    @Test
    fun `connecting without callback is cancelled after timeout`() = runTest {
        val transport = FakeTransport(autoReconnect = true)
        val supervisor = ConnectionSupervisor(
            transport,
            CoroutineScope(StandardTestDispatcher(testScheduler))
        )

        supervisor.start()
        transport.rawConnectionState.value = ConnectionState.CONNECTING
        runCurrent()
        advanceTimeBy(15_000)
        runCurrent()

        assertEquals(1, transport.cancelRequests)
    }

    private class FakeTransport(
        private val stateResponse: String =
            """{"status":"success","data":{"session_phase":"idle","active":false,"external_power_state":"UNKNOWN"}}""",
        private val autoReconnect: Boolean = false
    ) : HostConnectionTransport {
        override val rawConnectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
        val calls = mutableListOf<String>()
        var stateGate: CompletableDeferred<Unit>? = null
        var reconnectRequests = 0
        var cancelRequests = 0

        override suspend fun clientHello(): Result<ByteArray> {
            calls += "hello"
            return Result.success(
                """{"status":"success","data":{"schema_version":"2.1","reliable_push":true,"phone_fresh_for_ms":5000,"server_capabilities":["event_ack_v1"]}}"""
                    .toByteArray()
            )
        }

        override suspend fun getCurrentState(): Result<ByteArray> {
            calls += "state"
            stateGate?.await()
            return Result.success(stateResponse.toByteArray())
        }

        override fun shouldAutoReconnect(): Boolean = autoReconnect
        override fun requestReconnect(): Boolean {
            reconnectRequests += 1
            return true
        }

        override fun cancelStaleConnectionAttempt(): Boolean {
            cancelRequests += 1
            return true
        }
    }
}
