package com.edgefocus.app.util

import android.app.AppOpsManager
import android.app.usage.UsageStatsManager
import android.content.Context
import android.content.Intent
import android.os.Process
import android.provider.Settings

object AppCategorizer {

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
        val usageStatsManager =
            context.getSystemService(Context.USAGE_STATS_SERVICE) as? UsageStatsManager
                ?: return null

        val now = System.currentTimeMillis()
        val myPackageName = context.packageName

        // 60秒窗口查询
        val events = usageStatsManager.queryEvents(now - 60_000, now) ?: return null
        var lastResumedPackage: String? = null
        val event = android.app.usage.UsageEvents.Event()

        while (events.hasNextEvent()) {
            events.getNextEvent(event)
            when (event.eventType) {
                android.app.usage.UsageEvents.Event.ACTIVITY_RESUMED -> {
                    // 过滤掉自身
                    if (event.packageName != myPackageName) {
                        lastResumedPackage = event.packageName
                    }
                }
                android.app.usage.UsageEvents.Event.ACTIVITY_PAUSED,
                android.app.usage.UsageEvents.Event.ACTIVITY_STOPPED -> {
                    if (event.packageName == lastResumedPackage) {
                        lastResumedPackage = null
                    }
                }
            }
        }

        // 扩大到5分钟兜底
        if (lastResumedPackage == null) {
            val extendedEvents = usageStatsManager.queryEvents(now - 300_000, now) ?: return null
            var extendedPkg: String? = null
            val extEvent = android.app.usage.UsageEvents.Event()
            while (extendedEvents.hasNextEvent()) {
                extendedEvents.getNextEvent(extEvent)
                when (extEvent.eventType) {
                    android.app.usage.UsageEvents.Event.ACTIVITY_RESUMED -> {
                        if (extEvent.packageName != myPackageName) {
                            extendedPkg = extEvent.packageName
                        }
                    }
                    android.app.usage.UsageEvents.Event.ACTIVITY_PAUSED,
                    android.app.usage.UsageEvents.Event.ACTIVITY_STOPPED -> {
                        if (extEvent.packageName == extendedPkg) extendedPkg = null
                    }
                }
            }
            return extendedPkg
        }

        return lastResumedPackage
    }

    fun isDistractionApp(packageName: String): Boolean {
        val category = categorizeApp(packageName)
        return category == "entertainment" || category == "social"
    }
}
