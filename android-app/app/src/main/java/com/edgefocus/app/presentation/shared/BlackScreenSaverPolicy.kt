package com.edgefocus.app.presentation.shared

import com.edgefocus.app.domain.model.TimerState

fun shouldEnableBlackScreenSaver(
    settingEnabled: Boolean,
    timerState: TimerState
): Boolean = settingEnabled && timerState in setOf(TimerState.RUNNING, TimerState.PAUSED)
