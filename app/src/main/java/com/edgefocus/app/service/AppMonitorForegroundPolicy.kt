package com.edgefocus.app.service

data class AppMonitorForegroundDecision(
    val shouldCloseCurrentAppUsage: Boolean,
    val shouldShowUsageAccessAlert: Boolean,
    val shouldReportBackgroundDistraction: Boolean,
    val shouldShowReturnToEdgeFocusAlert: Boolean
)

object AppMonitorForegroundPolicy {
    fun decide(
        hasUsageAccess: Boolean,
        foregroundPackage: String?,
        selfPackage: String
    ): AppMonitorForegroundDecision {
        if (!hasUsageAccess) {
            return AppMonitorForegroundDecision(
                shouldCloseCurrentAppUsage = true,
                shouldShowUsageAccessAlert = true,
                shouldReportBackgroundDistraction = false,
                shouldShowReturnToEdgeFocusAlert = false
            )
        }

        val isSelf = foregroundPackage == selfPackage
        val isBackgroundOrUnknown = foregroundPackage == null || foregroundPackage == "unknown"
        return AppMonitorForegroundDecision(
            shouldCloseCurrentAppUsage = isBackgroundOrUnknown || isSelf,
            shouldShowUsageAccessAlert = false,
            shouldReportBackgroundDistraction = isBackgroundOrUnknown,
            shouldShowReturnToEdgeFocusAlert = isBackgroundOrUnknown
        )
    }
}
