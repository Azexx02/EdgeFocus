package com.edgefocus.app.data.remote

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PhoneUsagePolicyTest {

    @Test
    fun learningOrLevelZeroDoesNotCountAsPhoneDistraction() {
        assertFalse(
            PhoneUsagePolicy.shouldCountPhoneDistraction(
                PhoneUsage(isInUse = true, appCategory = "learning", usageState = "learning", distractionLevel = 0)
            )
        )
        assertFalse(
            PhoneUsagePolicy.shouldCountPhoneDistraction(
                PhoneUsage(isInUse = true, appCategory = "other", usageState = "other", distractionLevel = 0)
            )
        )
        assertFalse(
            PhoneUsagePolicy.shouldCountPhoneDistraction(
                PhoneUsage(isInUse = false, appCategory = "playing", usageState = "playing", distractionLevel = 3)
            )
        )
    }

    @Test
    fun activeNonLearningPositiveLevelCountsAsPhoneDistraction() {
        assertTrue(
            PhoneUsagePolicy.shouldCountPhoneDistraction(
                PhoneUsage(isInUse = true, appCategory = "playing", usageState = "playing", distractionLevel = 2)
            )
        )
    }
}
