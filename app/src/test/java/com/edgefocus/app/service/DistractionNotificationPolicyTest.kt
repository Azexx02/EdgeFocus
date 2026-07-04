package com.edgefocus.app.service

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DistractionNotificationPolicyTest {

    @Test
    fun highDistractionNotificationsAreThrottledAndUseStableId() {
        assertTrue(DistractionNotificationPolicy.HIGH_DISTRACTION_INTERVAL_MS >= 30_000L)
        assertEquals(DistractionNotificationPolicy.HIGH_DISTRACTION_ALERT_ID, DistractionNotificationPolicy.notificationIdForLevel(2))
        assertFalse(
            DistractionNotificationPolicy.canShowHighDistraction(
                nowMs = 10_000L,
                lastShownMs = 1_000L,
                hasShownInitial = true
            )
        )
    }

    @Test
    fun firstHighDistractionCanShowAfterInitialDelay() {
        assertFalse(
            DistractionNotificationPolicy.canShowHighDistraction(
                nowMs = 2_000L,
                lastShownMs = 0L,
                hasShownInitial = false
            )
        )
        assertTrue(
            DistractionNotificationPolicy.canShowHighDistraction(
                nowMs = 3_000L,
                lastShownMs = 0L,
                hasShownInitial = false
            )
        )
    }
}
