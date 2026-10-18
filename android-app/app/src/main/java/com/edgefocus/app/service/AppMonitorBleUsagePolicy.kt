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
            isInUse = false
        )
    }

    fun systemSurface(packageName: String): AppMonitorBleUsageReport {
        return AppMonitorBleUsageReport(
            packageName = packageName,
            category = "using",
            level = 0,
            isInUse = false
        )
    }

    fun isSystemSurface(packageName: String): Boolean =
        packageName == "com.android.systemui" ||
            packageName.contains("launcher", ignoreCase = true) ||
            packageName.contains("recents", ignoreCase = true)
}
