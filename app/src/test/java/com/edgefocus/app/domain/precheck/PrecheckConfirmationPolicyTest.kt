package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.remote.BoardPrecheck
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import com.edgefocus.app.data.remote.PrecheckGimbal
import com.edgefocus.app.domain.model.TimerState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PrecheckConfirmationPolicyTest {
    @Test
    fun degradedWinsOverContradictoryLeaseAndConfirmedFields() {
        val gimbal = PrecheckGimbal(
            enabled = true,
            online = false,
            safe = false,
            state = "DEGRADED",
            manualLease = true,
            detail = "position_confirmed"
        )

        assertEquals(
            PrecheckGimbalReadiness.DEGRADED,
            PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
        )
        assertTrue(PrecheckConfirmationPolicy.canConfirm(true, false, gimbal))
        assertFalse(PrecheckConfirmationPolicy.canUseManualControls(gimbal))
        assertFalse(PrecheckConfirmationPolicy.canSkipAutoAdjust(gimbal))
    }

    @Test
    fun parserStyleUnavailableObjectIsBlockedRatherThanNoGimbal() {
        val gimbal = PrecheckGimbal(
            enabled = false,
            online = false,
            safe = false,
            state = "idle",
            manualLease = false
        )

        assertEquals(
            PrecheckGimbalReadiness.BLOCKED_UNREADY,
            PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
        )
        assertFalse(PrecheckConfirmationPolicy.canConfirm(true, false, gimbal))
    }

    @Test
    fun noGimbalRemainsConfirmable() {
        assertEquals(
            PrecheckGimbalReadiness.NO_GIMBAL,
            PrecheckConfirmationPolicy.gimbalReadiness(null)
        )
        assertTrue(PrecheckConfirmationPolicy.canConfirm(true, false, null))
    }

    @Test
    fun preparationAndInFlightConfirmationRemainHardGates() {
        val degraded = PrecheckGimbal(state = "degraded")

        assertFalse(PrecheckConfirmationPolicy.canConfirm(false, false, degraded))
        assertFalse(PrecheckConfirmationPolicy.canConfirm(true, true, degraded))
    }

    @Test
    fun automaticSearchCanBeSkippedButCannotBeConfirmed() {
        val searching = PrecheckGimbal(
            enabled = true,
            online = true,
            safe = true,
            state = "searching",
            manualLease = false
        )

        assertEquals(
            PrecheckGimbalReadiness.BLOCKED_UNREADY,
            PrecheckConfirmationPolicy.gimbalReadiness(searching)
        )
        assertFalse(PrecheckConfirmationPolicy.canConfirm(true, false, searching))
        assertTrue(PrecheckConfirmationPolicy.canSkipAutoAdjust(searching))
        assertFalse(PrecheckConfirmationPolicy.canUseManualControls(searching))
    }

    @Test
    fun staleConfirmedFieldsWithManualLeaseStillRequirePositionConfirmation() {
        val gimbal = PrecheckGimbal(
            state = "holding",
            manualLease = true,
            detail = "position_confirmed"
        )

        assertEquals(
            PrecheckGimbalReadiness.MANUAL_READY,
            PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
        )
        assertTrue(PrecheckConfirmationPolicy.canUseManualControls(gimbal))
    }

    @Test
    fun degradedDuringPositionReconciliationAllowsFixedPositionContinuation() {
        val state = boardState(
            phase = BoardSessionPhase.PRECHECK,
            gimbal = PrecheckGimbal(state = "degraded", manualLease = false)
        )

        assertEquals(
            PositionReconcileDecision.GIMBAL_DEGRADED,
            PrecheckConfirmationPolicy.afterPositionUncertain("session_1", state)
        )
    }

    @Test
    fun runningStateCompletesEitherReconciliationStage() {
        val state = boardState(BoardSessionPhase.FOCUSING)

        assertEquals(
            PositionReconcileDecision.SESSION_RUNNING,
            PrecheckConfirmationPolicy.afterPositionUncertain("session_1", state)
        )
        assertEquals(
            SessionReconcileDecision.RUNNING,
            PrecheckConfirmationPolicy.afterSessionControl("session_1", state)
        )
    }

    @Test
    fun holdingWithoutManualLeaseMeansPositionWasConfirmed() {
        val state = boardState(
            phase = BoardSessionPhase.PRECHECK,
            gimbal = PrecheckGimbal(
                state = "holding",
                manualLease = false,
                detail = "position_confirmed"
            )
        )

        assertEquals(
            PositionReconcileDecision.POSITION_CONFIRMED,
            PrecheckConfirmationPolicy.afterPositionUncertain("session_1", state)
        )
    }

    @Test
    fun manualAvailableCanConfirmAndAcquireLeaseOnFirstManualCommand() {
        val gimbal = PrecheckGimbal(
            enabled = true,
            online = true,
            safe = true,
            state = "manual_available",
            manualLease = false
        )

        assertEquals(
            PrecheckGimbalReadiness.MANUAL_READY,
            PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
        )
        assertTrue(PrecheckConfirmationPolicy.canConfirm(true, false, gimbal))
        assertTrue(PrecheckConfirmationPolicy.canUseManualControls(gimbal))
    }

    @Test
    fun manualAvailableDetailCanAcquireLeaseOnFirstManualCommand() {
        val gimbal = PrecheckGimbal(
            enabled = true,
            online = true,
            safe = true,
            state = "idle",
            manualLease = false,
            detail = "automatic_search_skipped_manual_available"
        )

        assertEquals(
            PrecheckGimbalReadiness.MANUAL_READY,
            PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
        )
        assertTrue(PrecheckConfirmationPolicy.canUseManualControls(gimbal))
    }

    @Test
    fun remainingManualLeaseMeansPositionDidNotComplete() {
        val state = boardState(
            phase = BoardSessionPhase.PRECHECK,
            gimbal = PrecheckGimbal(state = "manual", manualLease = true)
        )

        assertEquals(
            PositionReconcileDecision.RETRY_POSITION,
            PrecheckConfirmationPolicy.afterPositionUncertain("session_1", state)
        )
    }

    @Test
    fun differentSessionIsRejected() {
        val state = boardState(BoardSessionPhase.FOCUSING, sessionId = "session_2")

        assertEquals(
            SessionReconcileDecision.SESSION_CHANGED,
            PrecheckConfirmationPolicy.afterSessionControl("session_1", state)
        )
    }

    @Test
    fun sameSessionCannotRegressFromRunningToPrecheck() {
        assertTrue(
            PrecheckConfirmationPolicy.isStalePrecheckRegression(
                currentTimerState = TimerState.RUNNING,
                currentSessionId = "session_1",
                incomingSessionId = "session_1",
                incomingPhase = BoardSessionPhase.PRECHECK
            )
        )
        assertFalse(
            PrecheckConfirmationPolicy.isStalePrecheckRegression(
                currentTimerState = TimerState.RUNNING,
                currentSessionId = "session_1",
                incomingSessionId = "session_2",
                incomingPhase = BoardSessionPhase.PRECHECK
            )
        )
        assertTrue(
            PrecheckConfirmationPolicy.isStalePrecheckRegression(
                currentTimerState = TimerState.PAUSED,
                currentSessionId = "session_1",
                incomingSessionId = "session_1",
                incomingPhase = BoardSessionPhase.PRECHECK
            )
        )
    }

    private fun boardState(
        phase: BoardSessionPhase,
        sessionId: String = "session_1",
        gimbal: PrecheckGimbal? = null
    ) = BoardSessionState(
        sessionId = sessionId,
        sessionPhase = phase,
        active = phase != BoardSessionPhase.IDLE,
        precheck = if (phase == BoardSessionPhase.PRECHECK) {
            BoardPrecheck(gimbal = gimbal)
        } else {
            null
        }
    )
}
