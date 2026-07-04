package com.edgefocus.app.service

object DistractionNotificationPolicy {
    const val BACKGROUND_ALERT_ID = 2001
    const val LOW_DISTRACTION_ALERT_ID = 2002
    const val HIGH_DISTRACTION_ALERT_ID = 2003
    const val USAGE_ACCESS_ALERT_ID = 2004
    const val HIGH_DISTRACTION_INTERVAL_MS = 30_000L
    const val HIGH_DISTRACTION_INITIAL_DELAY_MS = 3_000L
    const val USAGE_ACCESS_ALERT_INTERVAL_MS = 60_000L

    fun notificationIdForLevel(level: Int): Int {
        return when {
            level >= 2 -> HIGH_DISTRACTION_ALERT_ID
            level == 1 -> LOW_DISTRACTION_ALERT_ID
            else -> BACKGROUND_ALERT_ID
        }
    }

    fun canShowHighDistraction(
        nowMs: Long,
        lastShownMs: Long,
        hasShownInitial: Boolean
    ): Boolean {
        return if (!hasShownInitial) {
            nowMs >= HIGH_DISTRACTION_INITIAL_DELAY_MS
        } else {
            nowMs - lastShownMs >= HIGH_DISTRACTION_INTERVAL_MS
        }
    }
}
