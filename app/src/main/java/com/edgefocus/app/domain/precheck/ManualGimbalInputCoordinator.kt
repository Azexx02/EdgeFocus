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
import kotlin.math.abs
import kotlin.math.min

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
 * Coalesces user intent while a previous asynchronous target is being accepted.
 * The host remains authoritative for pose and generation; this class only keeps
 * not-yet-submitted deltas and never replays them after a context change.
 */
internal class ManualGimbalInputCoordinator(
    scope: CoroutineScope,
    private val send: suspend (
        sessionId: String,
        direction: ManualGimbalDirection,
        stepDeg: Double
    ) -> Result<GimbalCommandAcceptance>
) {
    private data class InputContext(
        val sessionId: String,
        val leaseGeneration: Long?
    )

    private data class PendingInput(
        val context: InputContext,
        val direction: ManualGimbalDirection,
        val stepDeg: Double,
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
        stepDeg: Double
    ): Result<Unit> {
        if (sessionId.isBlank()) return Result.failure(IllegalArgumentException("缺少 PRECHECK session_id"))
        if (!stepDeg.isFinite() || stepDeg !in MIN_STEP_DEG..MAX_STEP_DEG) {
            return Result.failure(IllegalArgumentException("云台步进必须在 1° 到 10° 之间"))
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
            if (projectedAxisMagnitude(direction, stepDeg) > MAX_PENDING_AXIS_DEG) {
                false
            } else {
                pending += PendingInput(nextContext, direction, stepDeg, deferred)
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
            val batch = mutex.withLock {
                val first = pending.firstOrNull() ?: return
                buildList {
                    while (pending.firstOrNull()?.context == first.context) {
                        add(pending.removeFirst())
                    }
                }
            }
            val result = submitNetDelta(batch)
            batch.forEach { it.result.complete(result.map { Unit }) }
        }
    }

    private suspend fun submitNetDelta(
        batch: List<PendingInput>
    ): Result<GimbalCommandAcceptance> {
        val context = batch.first().context
        var yawDeg = 0.0
        var pitchDeg = 0.0
        batch.forEach { input ->
            when (input.direction) {
                ManualGimbalDirection.LEFT -> yawDeg -= input.stepDeg
                ManualGimbalDirection.RIGHT -> yawDeg += input.stepDeg
                ManualGimbalDirection.UP -> pitchDeg += input.stepDeg
                ManualGimbalDirection.DOWN -> pitchDeg -= input.stepDeg
            }
        }
        val commands = buildCommands(yawDeg, pitchDeg)
        var acceptance = GimbalCommandAcceptance(accepted = true)
        for ((direction, stepDeg) in commands) {
            acceptance = send(context.sessionId, direction, stepDeg).getOrElse {
                return Result.failure(it)
            }
            if (!acceptance.accepted) {
                return Result.failure(
                    IllegalStateException(acceptance.error ?: "主机未接受云台微调")
                )
            }
            _lastAcceptance.value = acceptance
        }
        return Result.success(acceptance)
    }

    private suspend fun projectedAxisMagnitude(
        newDirection: ManualGimbalDirection,
        newStepDeg: Double
    ): Double {
        var axis = 0.0
        pending.forEach { input ->
            if (input.direction in HORIZONTAL && newDirection in HORIZONTAL) {
                axis += if (input.direction == ManualGimbalDirection.RIGHT) input.stepDeg else -input.stepDeg
            }
            if (input.direction in VERTICAL && newDirection in VERTICAL) {
                axis += if (input.direction == ManualGimbalDirection.UP) input.stepDeg else -input.stepDeg
            }
        }
        axis += when (newDirection) {
            ManualGimbalDirection.RIGHT, ManualGimbalDirection.UP -> newStepDeg
            ManualGimbalDirection.LEFT, ManualGimbalDirection.DOWN -> -newStepDeg
        }
        return abs(axis)
    }

    private fun buildCommands(
        yawDeg: Double,
        pitchDeg: Double
    ): List<Pair<ManualGimbalDirection, Double>> = buildList {
        appendAxis(yawDeg, ManualGimbalDirection.RIGHT, ManualGimbalDirection.LEFT)
        appendAxis(pitchDeg, ManualGimbalDirection.UP, ManualGimbalDirection.DOWN)
    }

    private fun MutableList<Pair<ManualGimbalDirection, Double>>.appendAxis(
        deltaDeg: Double,
        positive: ManualGimbalDirection,
        negative: ManualGimbalDirection
    ) {
        var remaining = abs(deltaDeg)
        val direction = if (deltaDeg >= 0.0) positive else negative
        while (remaining >= MIN_STEP_DEG - EPSILON) {
            val step = min(MAX_STEP_DEG, remaining)
            add(direction to step)
            remaining -= step
        }
    }

    private companion object {
        const val MIN_STEP_DEG = 1.0
        const val MAX_STEP_DEG = 10.0
        const val MAX_PENDING_AXIS_DEG = 60.0
        const val EPSILON = 0.0001
        val HORIZONTAL = setOf(ManualGimbalDirection.LEFT, ManualGimbalDirection.RIGHT)
        val VERTICAL = setOf(ManualGimbalDirection.UP, ManualGimbalDirection.DOWN)
    }
}
