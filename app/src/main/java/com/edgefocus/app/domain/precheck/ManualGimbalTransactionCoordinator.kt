package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.remote.BoardSessionPhase
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

internal data class ManualLeaseObservation(
    val sessionId: String?,
    val phase: BoardSessionPhase,
    val manualLease: Boolean
)

/**
 * Serializes user PRECHECK commands and acquires the board-owned manual lease.
 * Serializes lease transitions and terminal PRECHECK commands. Ordinary nudge
 * input is handled by [ManualGimbalInputCoordinator] and never enters this lock.
 */
internal class ManualGimbalTransactionCoordinator(
    private val observe: suspend () -> Result<ManualLeaseObservation>,
    private val requestSkipAuto: suspend (String) -> Result<Unit>,
    private val delayMs: suspend (Long) -> Unit = { delay(it) },
    private val nowMs: () -> Long = { System.nanoTime() / 1_000_000L },
    private val confirmationTimeoutMs: Long = 6_000L,
    private val pollIntervalMs: Long = 250L
) {
    private val transactionMutex = Mutex()

    suspend fun runManual(
        expectedSessionId: String,
        command: suspend (String) -> Result<Unit>
    ): Result<Unit> = runOrdinary {
        val observation = observe().getOrThrow()
        validateObservation(observation, expectedSessionId)
        check(observation.manualLease) {
            "云台正在自动调整；请等待完成或点击跳过自动调整"
        }
        command(expectedSessionId).getOrThrow()
    }

    suspend fun takeOver(expectedSessionId: String): Result<Unit> = runOrdinary {
        acquireManualLease(expectedSessionId)
    }

    suspend fun runCommand(command: suspend () -> Result<Unit>): Result<Unit> =
        transactionMutex.withLock { runCatching { command().getOrThrow() } }

    suspend fun runOrdinaryCommand(command: suspend () -> Result<Unit>): Result<Unit> =
        runOrdinary { command().getOrThrow() }

    private suspend fun runOrdinary(block: suspend () -> Unit): Result<Unit> {
        return transactionMutex.withLock { runCatching { block() } }
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
