package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.ble.model.GimbalCommandAcceptance
import java.util.concurrent.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

internal enum class ManualGimbalDirection(val wireValue: String) {
    UP("UP"),
    DOWN("DOWN"),
    LEFT("LEFT"),
    RIGHT("RIGHT");

    companion object {
        fun fromWire(value: String): ManualGimbalDirection = entries.firstOrNull {
            it.wireValue == value.trim().uppercase()
        } ?: error("未知云台方向：$value")
    }
}

/**
 * Serializes discrete manual-step intent without dropping rapid taps. The host
 * owns the actual step size and target accumulation; the App must therefore
 * preserve command count instead of inventing or merging degree values.
 */
internal class ManualGimbalInputCoordinator(
    scope: CoroutineScope,
    private val send: suspend (
        sessionId: String,
        direction: ManualGimbalDirection,
        fineTune: Boolean
    ) -> Result<GimbalCommandAcceptance>
) {
    private data class InputContext(
        val sessionId: String,
        val leaseGeneration: Long?
    )

    private data class PendingInput(
        val context: InputContext,
        val direction: ManualGimbalDirection,
        val fineTune: Boolean,
        val result: CompletableDeferred<Result<Unit>>
    )

    private val mutex = Mutex()
    private val pending = ArrayDeque<PendingInput>()
    private val wake = Channel<Unit>(Channel.CONFLATED)
    private var context: InputContext? = null
    private val _lastAcceptance = MutableStateFlow<GimbalCommandAcceptance?>(null)
    val lastAcceptance: StateFlow<GimbalCommandAcceptance?> = _lastAcceptance.asStateFlow()

    init {
        scope.launch {
            for (ignored in wake) drain()
        }
    }

    suspend fun enqueue(
        sessionId: String,
        leaseGeneration: Long?,
        direction: ManualGimbalDirection,
        fineTune: Boolean
    ): Result<Unit> {
        if (sessionId.isBlank()) {
            return Result.failure(IllegalArgumentException("缺少 PRECHECK session_id"))
        }
        val deferred = CompletableDeferred<Result<Unit>>()
        val nextContext = InputContext(sessionId, leaseGeneration)
        val cancelled = mutableListOf<PendingInput>()
        val accepted = mutex.withLock {
            if (context != null && context != nextContext) {
                cancelled += pending
                pending.clear()
            }
            context = nextContext
            if (pending.size >= MAX_PENDING_INPUTS) {
                false
            } else {
                pending += PendingInput(nextContext, direction, fineTune, deferred)
                true
            }
        }
        cancelled.forEach {
            it.result.complete(Result.failure(CancellationException("云台控制上下文已变化")))
        }
        if (!accepted) {
            return Result.failure(IllegalStateException("云台连续输入过快，请稍候"))
        }
        wake.trySend(Unit)
        return deferred.await()
    }

    suspend fun clear(reason: String) {
        val cancelled = mutex.withLock {
            val items = pending.toList()
            pending.clear()
            context = null
            items
        }
        cancelled.forEach {
            it.result.complete(Result.failure(CancellationException(reason)))
        }
    }

    private suspend fun drain() {
        while (true) {
            val input = mutex.withLock { pending.removeFirstOrNull() } ?: return
            val result = send(input.context.sessionId, input.direction, input.fineTune)
            result.onSuccess { acceptance ->
                if (acceptance.accepted) _lastAcceptance.value = acceptance
            }
            input.result.complete(result.mapCatching { acceptance ->
                check(acceptance.accepted) { acceptance.error ?: "主机未接受云台步进" }
            })
        }
    }

    private companion object {
        const val MAX_PENDING_INPUTS = 60
    }
}
