package com.edgefocus.app.service

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AppMonitorBleReportPolicyTest {

    @Test
    fun reportsOnlyWhenBleConnectedAndEdgeFocusSessionIsActive() {
        assertTrue(AppMonitorBleReportPolicy.shouldReport(bleConnected = true, activeEdgeFocusSession = true))
        assertFalse(AppMonitorBleReportPolicy.shouldReport(bleConnected = false, activeEdgeFocusSession = true))
        assertFalse(AppMonitorBleReportPolicy.shouldReport(bleConnected = true, activeEdgeFocusSession = false))
    }
}
