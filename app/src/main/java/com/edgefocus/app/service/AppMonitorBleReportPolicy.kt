package com.edgefocus.app.service

object AppMonitorBleReportPolicy {
    fun shouldReport(bleConnected: Boolean, activeEdgeFocusSession: Boolean): Boolean {
        return bleConnected && activeEdgeFocusSession
    }
}
