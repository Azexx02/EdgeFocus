package com.edgefocus.app.service

data class AppMonitorBleUsageReport(
    val packageName: String,
    val category: String,
    val level: Int,
    val isInUse: Boolean
)

object AppMonitorBleUsagePolicy {
    fun selfForeground(selfPackage: String): AppMonitorBleUsageReport {
        return AppMonitorBleUsageReport(
            packageName = selfPackage,
            category = "learning",
            level = 0,
            isInUse = true
        )
    }

    fun unknownForeground(backgroundPackageName: String): AppMonitorBleUsageReport {
        return AppMonitorBleUsageReport(
            packageName = backgroundPackageName,
            category = "using",
            level = 3,
            isInUse = true
        )
    }
}
