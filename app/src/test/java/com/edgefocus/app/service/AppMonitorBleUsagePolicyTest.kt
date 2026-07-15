package com.edgefocus.app.service

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppMonitorBleUsagePolicyTest {

    @Test
    fun selfForegroundReportsActiveLearningLevelZero() {
        val report = AppMonitorBleUsagePolicy.selfForeground("com.edgefocus.app")

        assertEquals("com.edgefocus.app", report.packageName)
        assertEquals("learning", report.category)
        assertEquals(0, report.level)
        assertFalse(report.isInUse)
    }

    @Test
    fun systemSurfaceReportsNotInUse() {
        val report = AppMonitorBleUsagePolicy.systemSurface("com.android.systemui")

        assertEquals("com.android.systemui", report.packageName)
        assertEquals("using", report.category)
        assertEquals(0, report.level)
        assertFalse(report.isInUse)
        assertTrue(AppMonitorBleUsagePolicy.isSystemSurface("com.miui.home.launcher"))
    }
}
