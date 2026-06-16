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

        val endTime = System.currentTimeMillis()
        val beginTime = endTime - 10_000

        val usageEvents = usageStatsManager.queryEvents(beginTime, endTime) ?: return null

        var lastForegroundPackage: String? = null
        val event = android.app.usage.UsageEvents.Event()

        while (usageEvents.hasNextEvent()) {
            usageEvents.getNextEvent(event)
            if (event.eventType == android.app.usage.UsageEvents.Event.ACTIVITY_RESUMED ||
                event.eventType == android.app.usage.UsageEvents.Event.MOVE_TO_FOREGROUND
            ) {
                lastForegroundPackage = event.packageName
            }
        }

        return lastForegroundPackage
    }

    fun isDistractionApp(packageName: String): Boolean {
        val category = categorizeApp(packageName)
        return category == "entertainment" || category == "social"
    }
}
