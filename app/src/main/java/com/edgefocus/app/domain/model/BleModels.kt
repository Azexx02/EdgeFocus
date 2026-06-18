package com.edgefocus.app.domain.model

data class DeviceStatus(
    val batteryLevel: Int,
    val firmwareVersion: String,
    val isConnected: Boolean,
    val rssi: Int = 0
)

data class FocusState(
    val state: TimerState,
    val elapsedSeconds: Int = 0,
    val sessionType: SessionType = SessionType.WORK
)

data class TodaySummary(
    val totalFocusMinutes: Int,
    val sessionsCompleted: Int,
    val streakDays: Int,
    val distractionsBlocked: Int
)

data class SessionReport(
    val sessionId: String,
    val startTime: Long,
    val endTime: Long,
    val focusScore: Float,
    val notes: String = ""
)
