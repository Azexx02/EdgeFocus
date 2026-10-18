package com.edgefocus.app.data.session

import com.edgefocus.app.data.remote.BoardSessionPhase

object BoardRemainingTimePolicy {
    fun select(
        currentSessionId: String?,
        incomingSessionId: String?,
        currentPhase: BoardSessionPhase,
        incomingPhase: BoardSessionPhase,
        currentRemainingMs: Long,
        incomingRemainingMs: Long
    ): Long {
        val incoming = incomingRemainingMs.coerceAtLeast(0L)
        if (incomingPhase != BoardSessionPhase.FOCUSING ||
            currentPhase != BoardSessionPhase.FOCUSING
        ) {
            return incoming
        }

        val currentId = currentSessionId?.takeIf { it.isNotBlank() }
        val incomingId = incomingSessionId?.takeIf { it.isNotBlank() }
        if (currentId != null && incomingId != null && currentId != incomingId) {
            return incoming
        }

        return minOf(currentRemainingMs.coerceAtLeast(0L), incoming)
    }
}
