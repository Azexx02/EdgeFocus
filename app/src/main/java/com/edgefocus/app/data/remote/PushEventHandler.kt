package com.edgefocus.app.data.remote

import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.FocusState

data class TimerComplete(
    val elapsedMs: Long,
    val targetDurationMs: Long,
    val distractionCount: Int,
    val reminderCount: Int
)

interface PushEventHandler {
    fun onPushState(state: FocusState)
    fun onPushAlert(alert: AlertInfo)
    fun onPushSessionEnd(summary: String)
    fun onPushTimerComplete(timerComplete: TimerComplete)
}
