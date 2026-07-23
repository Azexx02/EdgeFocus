package com.edgefocus.app.data.remote

import com.edgefocus.app.domain.model.FocusState
import com.edgefocus.app.data.ble.model.ReliableEventEnvelope

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
    fun onPushGimbalPrecheckState(state: BoardSessionState) {}
    fun onPushSessionEnd(summary: String)
    fun onPushTimerComplete(timerComplete: TimerComplete)
    fun onPushAlert(alert: PushAlert) {}
}

fun interface ReliablePushEventHandler {
    fun onReliableEvent(event: ReliableEventEnvelope)
}
