package com.edgefocus.app.service

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppMonitorForegroundPolicyTest {

    @Test
    fun missingUsageAccessRequestsPermissionWithoutReportingDistraction() {
        val decision = AppMonitorForegroundPolicy.decide(
            hasUsageAccess = false,
            foregroundPackage = null,
            selfPackage = "com.edgefocus.app"
        )

        assertTrue(decision.shouldCloseCurrentAppUsage)
        assertTrue(decision.shouldShowUsageAccessAlert)
        assertFalse(decision.shouldReportBackgroundDistraction)
        assertFalse(decision.shouldShowReturnToEdgeFocusAlert)
    }

    @Test
    fun nullForegroundWithPermissionKeepsBackgroundLogic() {
        val decision = AppMonitorForegroundPolicy.decide(
            hasUsageAccess = true,
            foregroundPackage = null,
            selfPackage = "com.edgefocus.app"
        )

        assertFalse(decision.shouldShowUsageAccessAlert)
        assertTrue(decision.shouldReportBackgroundDistraction)
        assertTrue(decision.shouldShowReturnToEdgeFocusAlert)
    }
}
