package com.edgefocus.app.data.session

import com.edgefocus.app.data.remote.BoardSessionPhase
import org.junit.Assert.assertEquals
import org.junit.Test

class BoardRemainingTimePolicyTest {
    @Test
    fun sameFocusingSessionNeverMovesRemainingTimeUp() {
        listOf(1_491_000L, 1_500_000L).forEach { incoming ->
            assertEquals(
                1_490_000L,
                select(current = 1_490_000L, incoming = incoming)
            )
        }
    }

    @Test
    fun sameFocusingSessionAcceptsLowerRemainingTime() {
        assertEquals(1_470_000L, select(current = 1_490_000L, incoming = 1_470_000L))
    }

    @Test
    fun explicitNewSessionAndPrecheckTransitionCanInitialize() {
        assertEquals(
            1_500_000L,
            BoardRemainingTimePolicy.select(
                currentSessionId = "old",
                incomingSessionId = "new",
                currentPhase = BoardSessionPhase.FOCUSING,
                incomingPhase = BoardSessionPhase.FOCUSING,
                currentRemainingMs = 10_000L,
                incomingRemainingMs = 1_500_000L
            )
        )
        assertEquals(
            1_500_000L,
            BoardRemainingTimePolicy.select(
                currentSessionId = "same",
                incomingSessionId = "same",
                currentPhase = BoardSessionPhase.PRECHECK,
                incomingPhase = BoardSessionPhase.FOCUSING,
                currentRemainingMs = 1_490_000L,
                incomingRemainingMs = 1_500_000L
            )
        )
    }

    @Test
    fun missingIncomingSessionIdCannotBypassSameSessionGuard() {
        assertEquals(
            1_490_000L,
            BoardRemainingTimePolicy.select(
                currentSessionId = "known",
                incomingSessionId = null,
                currentPhase = BoardSessionPhase.FOCUSING,
                incomingPhase = BoardSessionPhase.FOCUSING,
                currentRemainingMs = 1_490_000L,
                incomingRemainingMs = 1_500_000L
            )
        )
    }

    private fun select(current: Long, incoming: Long): Long =
        BoardRemainingTimePolicy.select(
            currentSessionId = "session-1",
            incomingSessionId = "session-1",
            currentPhase = BoardSessionPhase.FOCUSING,
            incomingPhase = BoardSessionPhase.FOCUSING,
            currentRemainingMs = current,
            incomingRemainingMs = incoming
        )
}
