package com.edgefocus.app.data.remote

import com.edgefocus.app.domain.model.FocusState

data class TimerComplete(
    val sessionId: String? = null,
    val startTimeMs: Long = 0L,
    val endTimeMs: Long = 0L,
    val totalDurationSec: Int = 0,
    val focusDurationSec: Int = 0,
    val elapsedMs: Long,
    val targetDurationMs: Long,
    val distractionCount: Int,
    val reminderCount: Int,
    val phoneDistractionCount: Int = 0,
    val endReason: String? = null
)

interface PushEventHandler {
    fun onPushState(state: FocusState)
    fun onPushStateSnapshot(state: BoardSessionState) {}
    fun onPushSessionEnd(summary: String)
    fun onPushTimerComplete(timerComplete: TimerComplete)
    fun onPushAlert(alert: PushAlert) {}
}
