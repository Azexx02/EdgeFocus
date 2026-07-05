package com.edgefocus.app.service

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AppMonitorBleUsagePolicyTest {

    @Test
    fun selfForegroundReportsActiveLearningLevelZero() {
        val report = AppMonitorBleUsagePolicy.selfForeground("com.edgefocus.app")

        assertEquals("com.edgefocus.app", report.packageName)
        assertEquals("learning", report.category)
        assertEquals(0, report.level)
        assertTrue(report.isInUse)
    }

    @Test
    fun unknownForegroundReportsActiveUsingLevelThree() {
        val report = AppMonitorBleUsagePolicy.unknownForeground("desktop_or_background")

        assertEquals("desktop_or_background", report.packageName)
        assertEquals("using", report.category)
        assertEquals(3, report.level)
        assertTrue(report.isInUse)
    }
}
