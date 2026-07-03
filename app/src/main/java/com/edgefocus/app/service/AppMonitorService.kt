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
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.entity.AppUsage
import com.edgefocus.app.data.remote.AppEventLog
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.PhoneUsage
import com.edgefocus.app.util.AppCategorizer
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
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
        private const val DESKTOP_OR_BACKGROUND = "desktop_or_background"

        const val ACTION_START_MONITORING = "com.edgefocus.app.ACTION_START_MONITORING"
        const val ACTION_STOP_MONITORING = "com.edgefocus.app.ACTION_STOP_MONITORING"

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

    }

    @Inject
    lateinit var appUsageDao: AppUsageDao

    @Inject
    lateinit var bleManager: BleManager

    @Inject
    lateinit var appEventLog: AppEventLog

    @Inject
    lateinit var timerStateHolder: TimerStateHolder

    @Inject
    lateinit var settingsDao: SettingsDao

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val monitoringMutex = Mutex()
    private var monitoringJob: Job? = null
    private var currentPackageName: String? = null
    private var currentStartTime: Long = 0L
    private var sessionId: String? = null
    private var distractionLevels: Map<String, Int> = emptyMap()
    private var lastBleReportedKey: String? = null

    // 通知时间记录
    private var lastHighDistractionAlert = 0L
    private var lastLowDistractionAlert = 0L
    private var lastBackgroundAlert = 0L
    private var lastUnclassifiedAlert = 0L
    private var lowDistractionInitialAlertSent = false

    // 通知计数器：每次生成新 ID，确保强干扰等持续提醒不会被 Android 系统静默
    // Android 对相同 ID 的通知只更新内容不重新提醒（不震动/不响铃），
    // 使用递增 ID 让每次通知都是新通知，触发新的提醒行为
    private var notificationCounter = 0

    // 通知间隔（毫秒）
    private val HIGH_DISTRACTION_INTERVAL = 1000L // 强干扰：每秒
    private val LOW_DISTRACTION_INTERVAL = 60000L // 弱干扰：每分钟
    private val LOW_DISTRACTION_INITIAL_DELAY = 3000L // 弱干扰：启动后3秒首次提醒
    private val BACKGROUND_INTERVAL = 10000L // 后台：每10秒
    private val DISTRACTION_RELOAD_INTERVAL = 5000L
    private var lastDistractionReload = 0L

    override fun onCreate() {
        super.onCreate()
        createNotificationChannels()
        startForeground(NOTIFICATION_ID, createNotification("准备监控应用..."))
        loadDistractionLevels()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_MONITORING -> {
                val requestedSessionId = intent.getStringExtra("session_id")
                serviceScope.launch {
                    monitoringMutex.withLock {
                        sessionId = requestedSessionId
                        startMonitoring()
                    }
                }
            }
            ACTION_STOP_MONITORING -> {
                serviceScope.launch {
                    monitoringMutex.withLock {
                        stopMonitoringAndPersist()
                    }
                    stopSelf(startId)
                }
            }
        }
        return START_REDELIVER_INTENT
    }

    override fun onBind(intent: android.content.Intent?): android.os.IBinder? {
        return null
    }

    override fun onDestroy() {
        super.onDestroy()
        monitoringJob?.cancel()
        monitoringJob = null
        serviceScope.cancel()
    }

    private fun loadDistractionLevels() {
        lastDistractionReload = System.currentTimeMillis()
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
        distractionLevels = distractionLevels.toMutableMap().apply {
            put(packageName, 0)
        }
        Log.d(TAG, "Loaded ${distractionLevels.size} distraction level entries")
    }

    private fun startMonitoring() {
        if (monitoringJob?.isActive == true) return

        loadDistractionLevels()
        lastBleReportedKey = null

        monitoringJob = serviceScope.launch {
            while (isActive) {
                try {
                    checkDistractionNotifications()
                } catch (e: Exception) {
                    Log.e(TAG, "Foreground monitoring iteration failed", e)
                }
                delay(POLL_INTERVAL_MS)
            }
        }
        updateNotification("正在监控前台应用...")
    }

    private suspend fun stopMonitoringAndPersist() {
        monitoringJob?.cancelAndJoin()
        monitoringJob = null
        closeCurrentAppUsage()
        lastBleReportedKey = null
        Log.d(TAG, "Monitoring stopped")
    }

    private suspend fun checkDistractionNotifications() {
        val now = System.currentTimeMillis()

        if (now - lastDistractionReload >= DISTRACTION_RELOAD_INTERVAL) {
            loadDistractionLevels()
        }

        val foregroundPackage = AppCategorizer.getForegroundApp(this)
        Log.d(TAG, "Foreground: $foregroundPackage, self=$packageName")

        // 1. EdgeFocus 自己在前台：BLE 上报 learning，不通知
        if (foregroundPackage == packageName) {
            closeCurrentAppUsage()
            reportBleIfChanged(
                packageName = packageName,
                category = "learning",
                level = 0,
                isInUse = false
            )
            return
        }

        // 2. null：复用原通知逻辑，按后台/桌面/other 处理
        if (foregroundPackage == null) {
            closeCurrentAppUsage()
            reportBleIfChanged(
                packageName = DESKTOP_OR_BACKGROUND,
                category = "other",
                level = 3,
                isInUse = false
            )

            if (now - lastBackgroundAlert >= BACKGROUND_INTERVAL) {
                lastBackgroundAlert = now
                showBackgroundNotification()
            }
            return
        }

        // 3. 有明确前台 App：保持原有应用切换记录逻辑
        if (foregroundPackage != currentPackageName) {
            recordCurrentAppUsage()
            currentPackageName = foregroundPackage
            currentStartTime = now
            lowDistractionInitialAlertSent = false
            lastHighDistractionAlert = 0L
            lastLowDistractionAlert = 0L
            lastBackgroundAlert = 0L
            lastUnclassifiedAlert = 0L
        }

        val level = distractionLevels[foregroundPackage] ?: 3
        val category = categoryForLevel(level)

        Log.d(TAG, "App: $foregroundPackage, Level: $level")

        // BLE 只跟随最终分类结果上报一次，不跟随通知频率重复发送
        reportBleIfChanged(
            packageName = foregroundPackage,
            category = category,
            level = level
        )

        // 下面的通知策略保持原样
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
                    showDistractionNotification("强干扰应用", "请停止使用 $appName！")
                }
            }
            3 -> {
                // 未分类应用（包括桌面）：每10秒提醒
                if (now - lastUnclassifiedAlert >= BACKGROUND_INTERVAL) {
                    lastUnclassifiedAlert = now
                    showDistractionNotification("专注中", "请返回 EdgeFocus 继续专注")
                }
            }
        }
    }

    private fun showBackgroundNotification() {
        showAlertNotification(
            title = "EdgeFocus 专注中",
            message = "您的专注会话正在进行，请返回应用继续专注",
            bigText = "您的专注会话正在进行中。为了保持专注效果，请返回 EdgeFocus 应用。放下手机，专注于当前任务。"
        )
    }

    private fun showDistractionNotification(title: String, message: String) {
        showAlertNotification(title = title, message = message)
    }

    private fun showAlertNotification(title: String, message: String, bigText: String? = null) {
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

        val builder = NotificationCompat.Builder(this, ALERT_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle(title)
            .setContentText(message)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(pendingIntent)
            .setAutoCancel(true)
            .setVibrate(longArrayOf(0, 500, 200, 500))

        bigText?.let {
            builder.setStyle(NotificationCompat.BigTextStyle().bigText(it))
        }

        notificationManager.notify(notificationId, builder.build())
        Log.d(TAG, "Alert notification sent: $notificationId")
    }

    private fun getAppName(packageName: String): String {
        return try {
            val appInfo = packageManager.getApplicationInfo(packageName, 0)
            packageManager.getApplicationLabel(appInfo).toString()
        } catch (e: Exception) {
            packageName
        }
    }

    private suspend fun reportBleIfChanged(
        packageName: String,
        category: String,
        level: Int,
        isInUse: Boolean = true
    ) {
        val key = "$packageName|$category|$level|$isInUse"

        if (key == lastBleReportedKey) {
            return
        }

        val settings = settingsDao.getSettingsOnce()
        if (settings?.useEdgeFocusBoard != true) {
            return
        }

        if (!bleManager.isConnected()) {
            return
        }

        val result = bleManager.phoneSync(
            PhoneUsage(
                isInUse = isInUse,
                appCategory = category,
                distractionLevel = level,
                appName = packageName,
                durationSec = null
            )
        )

        if (result.isSuccess) {
            lastBleReportedKey = key
            appEventLog.log("PHONE_SYNC", "$category, level=$level, pkg=$packageName")
            // 手机干扰计数（level > 0 表示干扰应用）
            if (isInUse && level > 0) {
                timerStateHolder.incrementPhoneDistraction()
            }
            Log.d(TAG, "BLE phoneSync sent: $key")
        } else {
            Log.e(TAG, "BLE phoneSync failed: ${result.exceptionOrNull()?.message}")
        }
    }

    private suspend fun recordCurrentAppUsage() {
        val packageName = currentPackageName ?: return
        val duration = ((System.currentTimeMillis() - currentStartTime) / 1000).toInt()

        if (duration <= 0) return

        val level = distractionLevels[packageName] ?: 3
        val category = categoryForLevel(level)

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

    private suspend fun closeCurrentAppUsage() {
        recordCurrentAppUsage()
        currentPackageName = null
        currentStartTime = 0L
        lowDistractionInitialAlertSent = false
    }

    private fun categoryForLevel(level: Int): String {
        return when (level) {
            0 -> "learning"
            1 -> "social"
            2 -> "entertainment"
            else -> "other"
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
