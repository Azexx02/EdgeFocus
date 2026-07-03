package com.edgefocus.app.data.remote

import com.edgefocus.app.domain.model.FocusState

data class TimerComplete(
    val elapsedMs: Long,
    val targetDurationMs: Long,
    val distractionCount: Int,
    val reminderCount: Int
)

interface PushEventHandler {
    fun onPushState(state: FocusState)
    fun onPushStateSnapshot(state: BoardSessionState) {}
    fun onPushSessionEnd(summary: String)
    fun onPushTimerComplete(timerComplete: TimerComplete)
    fun onPushAlert(alert: PushAlert) {}
}
