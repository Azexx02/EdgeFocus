package com.edgefocus.app.data.session

import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class StartTransactionPolicyTest {
    @Test
    fun legacyAckWithoutOperationIdIsAccepted() {
        assertTrue(StartTransactionPolicy.acceptsDirectAck("op-1", null))
        assertTrue(StartTransactionPolicy.acceptsDirectAck("op-1", "op-1"))
        assertFalse(StartTransactionPolicy.acceptsDirectAck("op-1", "op-old"))
    }

    @Test
    fun onlyHostPhasesRepresentAnActiveRuntime() {
        assertTrue(StartTransactionPolicy.isAuthoritativeActive(
            BoardSessionState(sessionPhase = BoardSessionPhase.PRECHECK)
        ))
        assertFalse(StartTransactionPolicy.isAuthoritativeActive(
            BoardSessionState(sessionPhase = BoardSessionPhase.IDLE, remainingMs = 60_000L)
        ))
    }

    @Test
    fun failureMustMatchOperationAndFreshWallClock() {
        val now = 100_000L
        val current = BoardSessionState(
            preparationState = BoardPreparationState.FAILED,
            preparationError = "camera failed",
            preparationFailedAtMs = now - 1_000L,
            operationId = "op-1"
        )
        assertEquals("camera failed", StartTransactionPolicy.failureForCurrentOperation("op-1", current, now))
        assertNull(StartTransactionPolicy.failureForCurrentOperation("op-2", current, now))
        assertNull(StartTransactionPolicy.failureForCurrentOperation("op-1", current, now + 61_000L))
    }
}
