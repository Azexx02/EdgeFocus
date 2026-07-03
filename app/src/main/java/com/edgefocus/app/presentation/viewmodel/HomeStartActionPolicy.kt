package com.edgefocus.app.presentation.viewmodel

import com.edgefocus.app.domain.model.TimerState

enum class HomeStartAction {
    START_EDGEFOCUS,
    START_LOCAL,
    SKIP_PRECHECK,
    RESUME,
    NONE
}

object HomeStartActionPolicy {
    fun decide(timerState: TimerState, useBoard: Boolean): HomeStartAction {
        return when (timerState) {
            TimerState.IDLE, TimerState.FINISHED -> {
                if (useBoard) HomeStartAction.START_EDGEFOCUS else HomeStartAction.START_LOCAL
            }
            TimerState.PRECHECK -> HomeStartAction.SKIP_PRECHECK
            TimerState.PAUSED -> HomeStartAction.RESUME
            TimerState.RUNNING -> HomeStartAction.NONE
        }
    }
}
