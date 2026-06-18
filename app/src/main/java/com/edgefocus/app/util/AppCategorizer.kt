package com.edgefocus.app.util

import android.app.AppOpsManager
import android.app.KeyguardManager
import android.app.usage.UsageStatsManager
import android.content.Context
import android.content.Intent
import android.os.PowerManager
import android.os.Process
import android.provider.Settings

object AppCategorizer {
    private const val EVENT_LOOKBACK_MS = 10 * 60 * 1000L
    private const val EVENT_FALLBACK_LOOKBACK_MS = 24 * 60 * 60 * 1000L
    private const val USAGE_STATS_LOOKBACK_MS = 24 * 60 * 60 * 1000L
    private const val USAGE_STATS_FALLBACK_SUPPRESS_MS = 30 * 60 * 1000L

    @Volatile
    private var lastKnownForegroundPackage: String? = null

    @Volatile
    private var lastKnownForegroundUpdatedAt: Long = 0L

    @Volatile
    private var suppressUsageStatsFallbackUntil: Long = 0L

    private val entertainmentApps = setOf(
        "com.zhiliaoapp.musically",
        "com.smile.gifmaker",
        "com.youku.phone",
        "com.qiyi.video",
        "com.netflix.mediaclient",
        "com.spotify.music",
        "tv.danmaku.bili",
        "com.kuaishou.nebula",
        "com.duowan.mobile"
    )

    private val socialApps = setOf(
        "com.tencent.mm",
        "com.tencent.mobileqq",
        "com.sina.weibo",
        "com.twitter.android",
        "com.instagram.android",
        "com.facebook.katana",
        "com.whatsapp",
        "org.telegram.messenger",
        "com.snapchat.android"
    )

    private val learningApps = setOf(
        "com.duolingo",
        "org.khanacademy.android",
        "com.coursera.android",
        "com.udemy.android",
        "com.edx.android",
        "com.xueersi.parents",
        "com.zuoyebang.k12",
        "com.youdao.dict",
        "com.baidu.homework"
    )

    fun categorizeApp(packageName: String): String {
        return when (packageName) {
            in entertainmentApps -> "entertainment"
            in socialApps -> "social"
            in learningApps -> "learning"
            else -> "other"
        }
    }

    fun hasUsageStatsPermission(context: Context): Boolean {
        val appOps = context.getSystemService(Context.APP_OPS_SERVICE) as AppOpsManager
        val mode = appOps.checkOpNoThrow(
            AppOpsManager.OPSTR_GET_USAGE_STATS,
            Process.myUid(),
            context.packageName
        )
        return mode == AppOpsManager.MODE_ALLOWED
    }

    fun getUsageAccessSettingsIntent(): Intent {
        return Intent(Settings.ACTION_USAGE_ACCESS_SETTINGS)
    }

    fun getForegroundApp(context: Context): String? {
        if (!hasUsageStatsPermission(context)) {
            return null
        }

        val now = System.currentTimeMillis()

        if (!isDeviceAvailableForForegroundCheck(context)) {
            clearLastKnownForeground(suppressFallback = true, now = now)
            return null
        }

        val usageStatsManager =
            context.getSystemService(Context.USAGE_STATS_SERVICE) as? UsageStatsManager
                ?: return null

        val eventResult = getForegroundPackageFromEvents(
            usageStatsManager = usageStatsManager,
            startTime = now - EVENT_LOOKBACK_MS,
            endTime = now,
            now = now
        )

        if (eventResult.hasRelevantEvents) {
            return rememberForeground(eventResult.packageName, now)
        }

        lastKnownForegroundPackage?.let { cachedPackage ->
            val cacheCheckStart = lastKnownForegroundUpdatedAt
                .takeIf { it > 0L }
                ?: (now - EVENT_LOOKBACK_MS)
            val cacheCheckResult = getForegroundPackageFromEvents(
                usageStatsManager = usageStatsManager,
                startTime = cacheCheckStart,
                endTime = now,
                now = now
            )

            if (cacheCheckResult.hasRelevantEvents) {
                return rememberForeground(cacheCheckResult.packageName, now)
            }

            return cachedPackage
        }

        val fallbackEventResult = getForegroundPackageFromEvents(
            usageStatsManager = usageStatsManager,
            startTime = now - EVENT_FALLBACK_LOOKBACK_MS,
            endTime = now,
            now = now
        )

        if (fallbackEventResult.hasRelevantEvents) {
            return rememberForeground(fallbackEventResult.packageName, now)
        }

        if (now < suppressUsageStatsFallbackUntil) {
            return null
        }

        return rememberForeground(
            getForegroundPackageFromUsageStats(
                usageStatsManager = usageStatsManager,
                startTime = now - USAGE_STATS_LOOKBACK_MS,
                endTime = now
            ),
            now
        )
    }

    private data class ForegroundEventResult(
        val packageName: String?,
        val hasRelevantEvents: Boolean
    )

    private fun getForegroundPackageFromEvents(
        usageStatsManager: UsageStatsManager,
        startTime: Long,
        endTime: Long,
        now: Long
    ): ForegroundEventResult {
        val events = usageStatsManager.queryEvents(startTime, endTime)
            ?: return ForegroundEventResult(null, hasRelevantEvents = false)
        val event = android.app.usage.UsageEvents.Event()
        var currentPackage: String? = null
        var hasRelevantEvents = false
        var sawBackgroundWithoutReplacement = false

        while (events.hasNextEvent()) {
            events.getNextEvent(event)
            when {
                isForegroundEvent(event.eventType) -> {
                    hasRelevantEvents = true
                    currentPackage = event.packageName
                    sawBackgroundWithoutReplacement = false
                }
                isBackgroundEvent(event.eventType) -> {
                    hasRelevantEvents = true
                    if (lastKnownForegroundPackage == event.packageName) {
                        clearLastKnownForeground(suppressFallback = true, now = now)
                    }
                    if (currentPackage == event.packageName) {
                        currentPackage = null
                        sawBackgroundWithoutReplacement = true
                    } else if (currentPackage == null) {
                        sawBackgroundWithoutReplacement = true
                    }
                }
            }
        }

        if (currentPackage == null && sawBackgroundWithoutReplacement) {
            suppressUsageStatsFallbackUntil = now + USAGE_STATS_FALLBACK_SUPPRESS_MS
        }

        return ForegroundEventResult(currentPackage, hasRelevantEvents)
    }

    private fun getForegroundPackageFromUsageStats(
        usageStatsManager: UsageStatsManager,
        startTime: Long,
        endTime: Long
    ): String? {
        return usageStatsManager.queryUsageStats(
            UsageStatsManager.INTERVAL_DAILY,
            startTime,
            endTime
        )
            ?.maxByOrNull { it.lastTimeUsed }
            ?.packageName
    }

    private fun rememberForeground(packageName: String?, now: Long): String? {
        if (packageName == null) {
            clearLastKnownForeground(suppressFallback = false, now = now)
        } else {
            lastKnownForegroundPackage = packageName
            lastKnownForegroundUpdatedAt = now
            suppressUsageStatsFallbackUntil = 0L
        }
        return packageName
    }

    private fun clearLastKnownForeground(suppressFallback: Boolean, now: Long) {
        lastKnownForegroundPackage = null
        lastKnownForegroundUpdatedAt = 0L
        if (suppressFallback) {
            suppressUsageStatsFallbackUntil = now + USAGE_STATS_FALLBACK_SUPPRESS_MS
        }
    }

    private fun isDeviceAvailableForForegroundCheck(context: Context): Boolean {
        val powerManager = context.getSystemService(Context.POWER_SERVICE) as? PowerManager
        val keyguardManager = context.getSystemService(Context.KEYGUARD_SERVICE) as? KeyguardManager
        return powerManager?.isInteractive == true && keyguardManager?.isKeyguardLocked != true
    }

    private fun isForegroundEvent(eventType: Int): Boolean {
        return eventType == android.app.usage.UsageEvents.Event.ACTIVITY_RESUMED ||
            eventType == android.app.usage.UsageEvents.Event.MOVE_TO_FOREGROUND
    }

    private fun isBackgroundEvent(eventType: Int): Boolean {
        return eventType == android.app.usage.UsageEvents.Event.ACTIVITY_PAUSED ||
            eventType == android.app.usage.UsageEvents.Event.ACTIVITY_STOPPED ||
            eventType == android.app.usage.UsageEvents.Event.MOVE_TO_BACKGROUND
    }

    fun isDistractionApp(packageName: String): Boolean {
        val category = categorizeApp(packageName)
        return category == "entertainment" || category == "social"
    }
}
