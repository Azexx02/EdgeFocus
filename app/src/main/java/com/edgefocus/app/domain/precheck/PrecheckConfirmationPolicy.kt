package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import com.edgefocus.app.data.remote.PrecheckGimbal
import com.edgefocus.app.data.remote.effectivePhase
import com.edgefocus.app.domain.model.TimerState

internal enum class PositionReconcileDecision {
    SESSION_RUNNING,
    POSITION_CONFIRMED,
    GIMBAL_DEGRADED,
    RETRY_POSITION,
    SESSION_IDLE,
    SESSION_CHANGED,
    UNKNOWN
}

internal enum class PrecheckGimbalReadiness {
    NO_GIMBAL,
    DEGRADED,
    POSITION_CONFIRMED,
    MANUAL_READY,
    BLOCKED_UNREADY
}

internal enum class SessionReconcileDecision {
    RUNNING,
    STILL_PRECHECK,
    SESSION_IDLE,
    SESSION_CHANGED,
    UNKNOWN
}

internal object PrecheckConfirmationPolicy {
    fun gimbalReadiness(gimbal: PrecheckGimbal?): PrecheckGimbalReadiness = when {
        gimbal == null -> PrecheckGimbalReadiness.NO_GIMBAL
        gimbal.state.equals("degraded", ignoreCase = true) -> PrecheckGimbalReadiness.DEGRADED
        !gimbal.manualLease &&
            (gimbal.state.equals("holding", ignoreCase = true) ||
                gimbal.detail.equals("position_confirmed", ignoreCase = true)) ->
            PrecheckGimbalReadiness.POSITION_CONFIRMED
        gimbal.manualLease || gimbal.state.equals("manual_available", ignoreCase = true) ||
            gimbal.state.equals("manual", ignoreCase = true) ->
            PrecheckGimbalReadiness.MANUAL_READY
        else -> PrecheckGimbalReadiness.BLOCKED_UNREADY
    }

    fun canConfirm(
        preparationReady: Boolean,
        isConfirming: Boolean,
        gimbal: PrecheckGimbal?
    ): Boolean = preparationReady &&
        !isConfirming &&
        gimbalReadiness(gimbal) != PrecheckGimbalReadiness.BLOCKED_UNREADY

    fun canUseManualControls(gimbal: PrecheckGimbal?): Boolean = gimbal != null &&
        !gimbal.state.equals("degraded", ignoreCase = true) &&
        (gimbal.manualLease || gimbal.manualControlAvailable ||
            gimbal.state.lowercase() in setOf("manual_available", "holding", "manual"))

    fun canSkipAutoAdjust(gimbal: PrecheckGimbal?): Boolean = gimbal != null &&
        !gimbal.manualLease &&
        !gimbal.state.equals("degraded", ignoreCase = true) &&
        gimbal.state.lowercase() in setOf("searching", "centering", "framing")

    fun afterPositionUncertain(
        expectedSessionId: String,
        state: BoardSessionState
    ): PositionReconcileDecision {
        if (state.sessionId != expectedSessionId) {
            return PositionReconcileDecision.SESSION_CHANGED
        }
        return when (state.effectivePhase()) {
            BoardSessionPhase.FOCUSING, BoardSessionPhase.PAUSED ->
                PositionReconcileDecision.SESSION_RUNNING
            BoardSessionPhase.IDLE -> PositionReconcileDecision.SESSION_IDLE
            BoardSessionPhase.PRECHECK -> {
                when (gimbalReadiness(state.precheck?.gimbal)) {
                    PrecheckGimbalReadiness.POSITION_CONFIRMED ->
                        PositionReconcileDecision.POSITION_CONFIRMED
                    PrecheckGimbalReadiness.DEGRADED,
                    PrecheckGimbalReadiness.NO_GIMBAL ->
                        PositionReconcileDecision.GIMBAL_DEGRADED
                    PrecheckGimbalReadiness.MANUAL_READY ->
                        PositionReconcileDecision.RETRY_POSITION
                    PrecheckGimbalReadiness.BLOCKED_UNREADY ->
                        PositionReconcileDecision.UNKNOWN
                }
            }
        }
    }

    fun afterSessionControl(
        expectedSessionId: String,
        state: BoardSessionState
    ): SessionReconcileDecision {
        if (state.sessionId != expectedSessionId) {
            return SessionReconcileDecision.SESSION_CHANGED
        }
        return when (state.effectivePhase()) {
            BoardSessionPhase.FOCUSING, BoardSessionPhase.PAUSED -> SessionReconcileDecision.RUNNING
            BoardSessionPhase.PRECHECK -> SessionReconcileDecision.STILL_PRECHECK
            BoardSessionPhase.IDLE -> SessionReconcileDecision.SESSION_IDLE
        }
    }

    fun isStalePrecheckRegression(
        currentTimerState: TimerState,
        currentSessionId: String?,
        incomingSessionId: String?,
        incomingPhase: BoardSessionPhase
    ): Boolean = currentTimerState in setOf(TimerState.RUNNING, TimerState.PAUSED) &&
        !currentSessionId.isNullOrBlank() &&
        currentSessionId == incomingSessionId &&
        incomingPhase == BoardSessionPhase.PRECHECK
}
