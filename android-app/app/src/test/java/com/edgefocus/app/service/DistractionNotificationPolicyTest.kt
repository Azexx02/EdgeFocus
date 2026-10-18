package com.edgefocus.app.service

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DistractionNotificationPolicyTest {

    @Test
    fun notificationIntervalsMatchProductPolicy() {
        assertEquals(1_000L, DistractionNotificationPolicy.intervalForLevel(2))
        assertEquals(30_000L, DistractionNotificationPolicy.intervalForLevel(1))
        assertEquals(10_000L, DistractionNotificationPolicy.intervalForLevel(3))
        assertFalse(
            DistractionNotificationPolicy.canShowHighDistraction(
                nowMs = 1_999L,
                lastShownMs = 1_000L,
                hasShownInitial = true
            )
        )
        assertTrue(
            DistractionNotificationPolicy.canShowHighDistraction(
                nowMs = 2_000L,
                lastShownMs = 1_000L,
                hasShownInitial = true
            )
        )
    }

    @Test
    fun repeatedNotificationsUseDifferentIdsWithinTheSameCategory() {
        val first = DistractionNotificationPolicy.notificationIdForLevel(2, 1)
        val second = DistractionNotificationPolicy.notificationIdForLevel(2, 2)
        assertFalse(first == second)
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
