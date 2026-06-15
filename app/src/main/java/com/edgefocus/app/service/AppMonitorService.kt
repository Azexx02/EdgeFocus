package com.edgefocus.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import com.edgefocus.app.MainActivity
import com.edgefocus.app.data.local.dao.AppUsageDao
import com.edgefocus.app.data.local.entity.AppUsage
import com.edgefocus.app.util.AppCategorizer
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import javax.inject.Inject

@AndroidEntryPoint
class AppMonitorService : Service() {

    companion object {
        private const val TAG = "AppMonitorService"
        private const val CHANNEL_ID = "app_monitor_channel"
        private const val ALERT_CHANNEL_ID = "app_alert_channel"
        private const val NOTIFICATION_ID = 1002
        private const val ALERT_NOTIFICATION_ID = 2000
        private const val POLL_INTERVAL_MS = 1000L // 1秒检查一次（支持强干扰每秒提醒）

        const val ACTION_START_MONITORING = "com.edgefocus.app.ACTION_START_MONITORING"
        const val ACTION_STOP_MONITORING = "com.edgefocus.app.ACTION_STOP_MONITORING"
        const val ACTION_APP_TO_BACKGROUND = "com.edgefocus.app.ACTION_APP_TO_BACKGROUND"
        const val ACTION_APP_TO_FOREGROUND = "com.edgefocus.app.ACTION_APP_TO_FOREGROUND"

        fun startService(context: Context, sessionId: String? = null) {
            val intent = Intent(context, AppMonitorService::class.java).apply {
                action = ACTION_START_MONITORING
                sessionId?.let { putExtra("session_id", it) }
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            val intent = Intent(context, AppMonitorService::class.java).apply {
                action = ACTION_STOP_MONITORING
            }
            context.startService(intent)
        }

        fun notifyAppToBackground(context: Context) {
            val intent = Intent(context, AppMonitorService::class.java).apply {
                action = ACTION_APP_TO_BACKGROUND
            }
            context.startService(intent)
        }

        fun notifyAppToForeground(context: Context) {
            val intent = Intent(context, AppMonitorService::class.java).apply {
                action = ACTION_APP_TO_FOREGROUND
            }
            context.startService(intent)
        }
    }

    @Inject
    lateinit var appUsageDao: AppUsageDao

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var monitoringJob: Job? = null
    private var currentPackageName: String? = null
    private var currentStartTime: Long = 0L
    private var sessionId: String? = null
    private var distractionLevels: Map<String, Int> = emptyMap()
    private var isAppInForeground = true

    // 通知时间记录
    private var lastHighDistractionAlert = 0L
    private var lastLowDistractionAlert = 0L
    private var lastBackgroundAlert = 0L
    private var lastUnclassifiedAlert = 0L
    private var lowDistractionInitialAlertSent = false

    // 通知计数器（用于生成不同ID）
    private var notificationCounter = 0

    // 通知间隔（毫秒）
    private val HIGH_DISTRACTION_INTERVAL = 1000L // 强干扰：每秒
    private val LOW_DISTRACTION_INTERVAL = 60000L // 弱干扰：每分钟
    private val LOW_DISTRACTION_INITIAL_DELAY = 3000L // 弱干扰：启动后3秒首次提醒
    private val BACKGROUND_INTERVAL = 10000L // 后台：每10秒

    override fun onCreate() {
        super.onCreate()
        createNotificationChannels()
        startForeground(NOTIFICATION_ID, createNotification("准备监控应用..."))
        loadDistractionLevels()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_MONITORING -> {
                sessionId = intent.getStringExtra("session_id")
                startMonitoring()
            }
            ACTION_STOP_MONITORING -> {
                stopMonitoring()
                stopSelf()
            }
            ACTION_APP_TO_FOREGROUND -> {
                isAppInForeground = true
            }
        }
        return START_STICKY
    }

    override fun onBind(intent: android.content.Intent?): android.os.IBinder? {
        return null
    }

    override fun onDestroy() {
        super.onDestroy()
        stopMonitoring()
        serviceScope.cancel()
    }

    private fun loadDistractionLevels() {
        val prefs = getSharedPreferences("test_prefs", MODE_PRIVATE)
        val saved = prefs.getString("distraction_levels", "") ?: ""
        if (saved.isNotEmpty()) {
            distractionLevels = saved.split(";").filter { it.isNotEmpty() }.associate { entry ->
                val parts = entry.split("=")
                if (parts.size == 2) {
                    parts[0] to (parts[1].toIntOrNull() ?: 3)
                } else {
                    "" to 3
                }
            }.filterKeys { it.isNotEmpty() }
        }
        Log.d(TAG, "Loaded ${distractionLevels.size} distraction level entries")
    }

    private fun startMonitoring() {
        if (monitoringJob?.isActive == true) return

        loadDistractionLevels()
        isAppInForeground = true

        monitoringJob = serviceScope.launch {
            while (isActive) {
                checkDistractionNotifications()
                delay(POLL_INTERVAL_MS)
            }
        }
        updateNotification("正在监控前台应用...")
    }

    private fun stopMonitoring() {
        monitoringJob?.cancel()
        monitoringJob = null
        serviceScope.launch {
            recordCurrentAppUsage()
        }
        currentPackageName = null
        currentStartTime = 0L
        isAppInForeground = true
        Log.d(TAG, "Monitoring stopped")
    }

    private suspend fun checkDistractionNotifications() {
        val now = System.currentTimeMillis()

        // 每次检查时重新加载干扰等级
        loadDistractionLevels()

        val foregroundPackage = AppCategorizer.getForegroundApp(this)

        // 1. 应用在前台 - 不通知
        if (foregroundPackage == packageName) {
            return
        }

        // 2. 无前台应用（锁屏）- 后台通知
        if (foregroundPackage == null) {
            if (now - lastBackgroundAlert >= BACKGROUND_INTERVAL) {
                lastBackgroundAlert = now
                showBackgroundNotification()
            }
            return
        }

        // 3. 前台有应用 - 根据干扰等级通知，禁用后台通知

        // 记录应用切换
        if (foregroundPackage != currentPackageName) {
            recordCurrentAppUsage()
            currentPackageName = foregroundPackage
            currentStartTime = now
            lowDistractionInitialAlertSent = false
        }

        val level = distractionLevels[foregroundPackage] ?: 3 // 默认为未分类
        Log.d(TAG, "App: $foregroundPackage, Level: $level")

        when (level) {
            0 -> {
                // 学习软件，不提醒
            }
            1 -> {
                // 弱干扰源：3秒后首次提醒，之后每分钟
                if (!lowDistractionInitialAlertSent) {
                    val elapsed = now - currentStartTime
                    if (elapsed >= LOW_DISTRACTION_INITIAL_DELAY) {
                        lowDistractionInitialAlertSent = true
                        lastLowDistractionAlert = now
                        val appName = getAppName(foregroundPackage)
                        showDistractionNotification("弱干扰应用", "您正在使用 $appName")
                    }
                } else if (now - lastLowDistractionAlert >= LOW_DISTRACTION_INTERVAL) {
                    lastLowDistractionAlert = now
                    val appName = getAppName(foregroundPackage)
                    showDistractionNotification("弱干扰应用", "您已使用 $appName 超过一分钟")
                }
            }
            2 -> {
                // 强干扰源：每秒提醒
                if (now - lastHighDistractionAlert >= HIGH_DISTRACTION_INTERVAL) {
                    lastHighDistractionAlert = now
                    val appName = getAppName(foregroundPackage)
                    showDistractionNotification("⚠️ 强干扰应用", "请停止使用 $appName！")
                }
            }
            3 -> {
                // 未分类应用（包括桌面）：每10秒提醒
                if (now - lastUnclassifiedAlert >= BACKGROUND_INTERVAL) {
                    lastUnclassifiedAlert = now
                    val appName = getAppName(foregroundPackage)
                    showDistractionNotification("专注中", "请返回 EdgeFocus 继续专注")
                }
            }
        }
    }

    private fun showBackgroundNotification() {
        notificationCounter++
        val notificationId = ALERT_NOTIFICATION_ID + notificationCounter

        val notificationManager = getSystemService(NotificationManager::class.java)

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            this, notificationId, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, ALERT_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle("📱 EdgeFocus 专注中")
            .setContentText("您的专注会话正在进行，请返回应用继续专注")
            .setStyle(NotificationCompat.BigTextStyle()
                .bigText("您的专注会话正在进行中。为了保持专注效果，请返回 EdgeFocus 应用。放下手机，专注于当前任务。"))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(pendingIntent)
            .setAutoCancel(true)
            .setVibrate(longArrayOf(0, 500, 200, 500))
            .build()

        notificationManager.notify(notificationId, notification)
        Log.d(TAG, "Background notification sent: $notificationId")
    }

    private fun showDistractionNotification(title: String, message: String) {
        notificationCounter++
        val notificationId = ALERT_NOTIFICATION_ID + notificationCounter

        val notificationManager = getSystemService(NotificationManager::class.java)

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            this, notificationId, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, ALERT_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle(title)
            .setContentText(message)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(pendingIntent)
            .setAutoCancel(true)
            .setVibrate(longArrayOf(0, 500, 200, 500))
            .build()

        notificationManager.notify(notificationId, notification)
        Log.d(TAG, "Distraction notification sent: $notificationId")
    }

    private fun getAppName(packageName: String): String {
        return try {
            val appInfo = packageManager.getApplicationInfo(packageName, 0)
            packageManager.getApplicationLabel(appInfo).toString()
        } catch (e: Exception) {
            packageName
        }
    }

    private fun isLauncherApp(packageName: String): Boolean {
        val intent = android.content.Intent(android.content.Intent.ACTION_MAIN).apply {
            addCategory(android.content.Intent.CATEGORY_HOME)
        }
        val resolveInfo = packageManager.resolveActivity(intent, 0)
        return resolveInfo?.activityInfo?.packageName == packageName
    }

    private suspend fun recordCurrentAppUsage() {
        val packageName = currentPackageName ?: return
        val duration = ((System.currentTimeMillis() - currentStartTime) / 1000).toInt()

        if (duration <= 0) return

        val level = distractionLevels[packageName] ?: 3
        val category = when (level) {
            0 -> "learning"
            1 -> "social"
            2 -> "entertainment"
            else -> "other"
        }

        val appUsage = AppUsage(
            packageName = packageName,
            category = category,
            usageDuration = duration,
            sessionId = sessionId
        )

        try {
            appUsageDao.insertAppUsage(appUsage)
            Log.d(TAG, "Recorded usage: $packageName - ${duration}s (level: $level)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to record app usage: ${e.message}")
        }
    }

    private fun createNotificationChannels() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val notificationManager = getSystemService(NotificationManager::class.java)

            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "应用监控服务",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "EdgeFocus 应用监控服务"
            }
            notificationManager.createNotificationChannel(serviceChannel)

            val alertChannel = NotificationChannel(
                ALERT_CHANNEL_ID,
                "专注提醒",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "专注期间的干扰和后台提醒"
                enableVibration(true)
                vibrationPattern = longArrayOf(0, 500, 200, 500)
                enableLights(true)
                lightColor = android.graphics.Color.RED
            }
            notificationManager.createNotificationChannel(alertChannel)
        }
    }

    private fun createNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("EdgeFocus")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_info_details)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.notify(NOTIFICATION_ID, createNotification(text))
    }
}
