package com.edgefocus.app.data.session

import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import kotlin.math.abs

object StartTransactionPolicy {
    private const val FAILURE_MAX_AGE_MS = 60_000L

    fun acceptsDirectAck(expectedOperationId: String, responseOperationId: String?): Boolean =
        responseOperationId.isNullOrBlank() || responseOperationId == expectedOperationId

    fun isAuthoritativeActive(state: BoardSessionState): Boolean =
        state.sessionPhase == BoardSessionPhase.PRECHECK ||
            state.sessionPhase == BoardSessionPhase.FOCUSING ||
            state.sessionPhase == BoardSessionPhase.PAUSED

    fun failureForCurrentOperation(
        expectedOperationId: String,
        state: BoardSessionState,
        nowWallMs: Long
    ): String? {
        if (state.preparationState != BoardPreparationState.FAILED) return null
        if (state.operationId != expectedOperationId) return null
        val failedAt = state.preparationFailedAtMs
        if (failedAt <= 0L || abs(nowWallMs - failedAt) > FAILURE_MAX_AGE_MS) return null
        return state.preparationError ?: "EdgeFocus 预检准备失败"
    }
}
