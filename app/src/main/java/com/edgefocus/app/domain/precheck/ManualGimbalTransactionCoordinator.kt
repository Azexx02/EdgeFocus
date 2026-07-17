package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.remote.BoardSessionPhase
import java.util.concurrent.atomic.AtomicBoolean
import kotlinx.coroutines.delay

internal data class ManualLeaseObservation(
    val sessionId: String?,
    val phase: BoardSessionPhase,
    val manualLease: Boolean
)

/**
 * Serializes user PRECHECK commands and acquires the board-owned manual lease.
 * Inputs received while a transaction is active are intentionally dropped,
 * rather than queued, so rapid taps cannot become a delayed command burst.
 */
internal class ManualGimbalTransactionCoordinator(
    private val observe: suspend () -> Result<ManualLeaseObservation>,
    private val requestSkipAuto: suspend (String) -> Result<Unit>,
    private val delayMs: suspend (Long) -> Unit = { delay(it) },
    private val nowMs: () -> Long = { System.nanoTime() / 1_000_000L },
    private val confirmationTimeoutMs: Long = 6_000L,
    private val pollIntervalMs: Long = 250L
) {
    private val transactionInFlight = AtomicBoolean(false)

    suspend fun runManual(
        expectedSessionId: String,
        command: suspend (String) -> Result<Unit>
    ): Result<Unit> = runExclusive {
        val observation = observe().getOrThrow()
        validateObservation(observation, expectedSessionId)
        check(observation.manualLease) {
            "云台正在自动调整；请等待完成或点击跳过自动调整"
        }
        command(expectedSessionId).getOrThrow()
    }

    suspend fun takeOver(expectedSessionId: String): Result<Unit> = runExclusive {
        acquireManualLease(expectedSessionId)
    }

    suspend fun runCommand(command: suspend () -> Result<Unit>): Result<Unit> = runExclusive {
        command().getOrThrow()
    }

    private suspend fun runExclusive(block: suspend () -> Unit): Result<Unit> {
        if (!transactionInFlight.compareAndSet(false, true)) {
            return Result.success(Unit)
        }
        return try {
            runCatching { block() }
        } finally {
            transactionInFlight.set(false)
        }
    }

    private suspend fun acquireManualLease(expectedSessionId: String) {
        val first = observe().getOrThrow()
        validateObservation(first, expectedSessionId)
        if (first.manualLease) return

        // A takeover is sent at most once per user transaction. Confirmation
        // comes only from subsequent authoritative state snapshots.
        requestSkipAuto(expectedSessionId).getOrThrow()
        val deadline = nowMs() + confirmationTimeoutMs
        while (nowMs() < deadline) {
            val remaining = (deadline - nowMs()).coerceAtLeast(1L)
            delayMs(minOf(pollIntervalMs, remaining))
            val observation = observe().getOrThrow()
            validateObservation(observation, expectedSessionId)
            if (observation.manualLease) return
        }
        error("主机未在 6 秒内确认云台手动控制")
    }

    private fun validateObservation(
        observation: ManualLeaseObservation,
        expectedSessionId: String
    ) {
        check(observation.sessionId == expectedSessionId) { "PRECHECK 会话已变化" }
        check(observation.phase == BoardSessionPhase.PRECHECK) { "主机已退出 PRECHECK" }
    }
}
