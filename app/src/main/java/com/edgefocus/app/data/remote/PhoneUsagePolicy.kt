package com.edgefocus.app.data.remote

object PhoneUsagePolicy {
    fun shouldCountPhoneDistraction(usage: PhoneUsage): Boolean {
        val state = (usage.usageState ?: usage.appCategory ?: "other").lowercase()
        return usage.isInUse && state != "learning" && usage.distractionLevel > 0
    }
}
