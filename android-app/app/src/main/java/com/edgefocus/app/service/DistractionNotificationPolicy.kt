package com.edgefocus.app.service

object DistractionNotificationPolicy {
    const val BACKGROUND_ALERT_ID = 2001
    const val LOW_DISTRACTION_ALERT_ID = 2002
    const val HIGH_DISTRACTION_ALERT_ID = 2003
    const val USAGE_ACCESS_ALERT_ID = 2004
    const val HIGH_DISTRACTION_INTERVAL_MS = 1_000L
    const val LOW_DISTRACTION_INTERVAL_MS = 30_000L
    const val BACKGROUND_OR_UNCLASSIFIED_INTERVAL_MS = 10_000L
    const val HIGH_DISTRACTION_INITIAL_DELAY_MS = 3_000L
    const val LOW_DISTRACTION_INITIAL_DELAY_MS = 3_000L
    const val USAGE_ACCESS_ALERT_INTERVAL_MS = 60_000L

    fun notificationIdForLevel(level: Int, sequence: Int = 0): Int {
        val base = when {
            level == 2 -> HIGH_DISTRACTION_ALERT_ID
            level == 1 -> LOW_DISTRACTION_ALERT_ID
            else -> BACKGROUND_ALERT_ID
        }
        return base + (sequence.coerceAtLeast(0) % 100_000) * 10
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

    fun intervalForLevel(level: Int): Long = when {
        level == 2 -> HIGH_DISTRACTION_INTERVAL_MS
        level == 1 -> LOW_DISTRACTION_INTERVAL_MS
        else -> BACKGROUND_OR_UNCLASSIFIED_INTERVAL_MS
    }
}
