package com.edgefocus.app.data

import com.edgefocus.app.domain.model.TimerState

object TimerPhoneDistractionPolicy {
    fun shouldIncrementLocalPhoneDistraction(
        sessionMode: SessionMode,
        timerState: TimerState
    ): Boolean {
        return sessionMode == SessionMode.LOCAL && timerState == TimerState.RUNNING
    }
}
