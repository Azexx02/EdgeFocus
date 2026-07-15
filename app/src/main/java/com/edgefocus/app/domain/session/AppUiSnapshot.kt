package com.edgefocus.app.domain.session

import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.domain.model.TimerState

data class AppUiSnapshot(
    val timerState: TimerState = TimerState.IDLE,
    val owner: SessionMode? = null,
    val sessionId: String? = null,
    val startTimeMs: Long = 0L,
    val timeRemainingMs: Long = 0L,
    val totalTimeMs: Long = 0L,
    val hostConnected: Boolean = false,
    val hostReady: Boolean = false,
    val recovering: Boolean = false
)
