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
        private const val ALERT_NOTIFICATION_ID = 1003
        private const val POLL_INTERVAL_MS = 3000L // 3秒检查一次

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

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var monitoringJob: Job? = null
    private var currentPackageName: String? = null
    private var currentStartTime: Long = 0L
    private var sessionId: String? = null
    private var distractionApps: Set<String> = emptySet()
    private var lastAlertTime: Long = 0L
    private val alertCooldownMs = 30000L // 30秒冷却时间

    override fun onCreate() {
        super.onCreate()
        createNotificationChannels()
        startForeground(NOTIFICATION_ID, createNotification("准备监控应用..."))
        loadDistractionApps()
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

    private fun loadDistractionApps() {
        val prefs = getSharedPreferences("test_prefs", MODE_PRIVATE)
        distractionApps = prefs.getStringSet("distraction_apps", emptySet()) ?: emptySet()
        Log.d(TAG, "Loaded ${distractionApps.size} distraction apps")
    }

    private fun startMonitoring() {
        if (monitoringJob?.isActive == true) return

        loadDistractionApps() // 每次启动时重新加载

        monitoringJob = serviceScope.launch {
            while (isActive) {
                val foregroundPackage = AppCategorizer.getForegroundApp(this@AppMonitorService)
                if (foregroundPackage != null && foregroundPackage != currentPackageName) {
                    recordCurrentAppUsage()
                    currentPackageName = foregroundPackage
                    currentStartTime = System.currentTimeMillis()

                    // 检查是否是干扰应用
                    checkDistractionApp(foregroundPackage)
                }
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
    }

    private fun checkDistractionApp(packageName: String) {
        if (distractionApps.contains(packageName)) {
            val now = System.currentTimeMillis()
            if (now - lastAlertTime > alertCooldownMs) {
                lastAlertTime = now
                showDistractionAlert(packageName)
            }
        }
    }

    private fun showDistractionAlert(packageName: String) {
        val pm = packageManager
        val appName = try {
            val appInfo = pm.getApplicationInfo(packageName, 0)
            pm.getApplicationLabel(appInfo).toString()
        } catch (e: Exception) {
            packageName
        }

        val notificationManager = getSystemService(NotificationManager::class.java)

        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, ALERT_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle("检测到干扰应用")
            .setContentText("您正在使用 $appName，这可能会影响您的专注")
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(pendingIntent)
            .setAutoCancel(true)
            .setVibrate(longArrayOf(0, 500, 200, 500))
            .build()

        notificationManager.notify(ALERT_NOTIFICATION_ID, notification)
        Log.d(TAG, "Distraction alert: $appName ($packageName)")
    }

    private suspend fun recordCurrentAppUsage() {
        val packageName = currentPackageName ?: return
        val duration = ((System.currentTimeMillis() - currentStartTime) / 1000).toInt()

        if (duration <= 0) return

        val category = if (distractionApps.contains(packageName)) {
            "distraction"
        } else {
            AppCategorizer.categorizeApp(packageName)
        }

        val appUsage = AppUsage(
            packageName = packageName,
            category = category,
            usageDuration = duration,
            sessionId = sessionId
        )

        try {
            appUsageDao.insertAppUsage(appUsage)
            Log.d(TAG, "Recorded usage: $packageName - ${duration}s (category: $category)")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to record app usage: ${e.message}")
        }
    }

    private fun createNotificationChannels() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val notificationManager = getSystemService(NotificationManager::class.java)

            // 服务通知渠道
            val serviceChannel = NotificationChannel(
                CHANNEL_ID,
                "应用监控服务",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "EdgeFocus 应用监控服务"
            }
            notificationManager.createNotificationChannel(serviceChannel)

            // 警报通知渠道
            val alertChannel = NotificationChannel(
                ALERT_CHANNEL_ID,
                "干扰应用警报",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "检测到干扰应用时的警报通知"
                enableVibration(true)
                vibrationPattern = longArrayOf(0, 500, 200, 500)
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
