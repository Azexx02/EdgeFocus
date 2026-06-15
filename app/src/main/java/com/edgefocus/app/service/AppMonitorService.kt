package com.edgefocus.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.lifecycle.LifecycleService
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
class AppMonitorService : LifecycleService() {

    companion object {
        private const val TAG = "AppMonitorService"
        private const val CHANNEL_ID = "app_monitor_channel"
        private const val NOTIFICATION_ID = 1002
        private const val POLL_INTERVAL_MS = 5000L

        const val ACTION_START_MONITORING = "com.edgefocus.app.ACTION_START_MONITORING"
        const val ACTION_STOP_MONITORING = "com.edgefocus.app.ACTION_STOP_MONITORING"

        fun startService(context: Context) {
            val intent = Intent(context, AppMonitorService::class.java).apply {
                action = ACTION_START_MONITORING
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

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification("Monitoring apps..."))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
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

    override fun onDestroy() {
        super.onDestroy()
        stopMonitoring()
        serviceScope.cancel()
    }

    private fun startMonitoring() {
        if (monitoringJob?.isActive == true) return

        monitoringJob = serviceScope.launch {
            while (isActive) {
                val foregroundPackage = AppCategorizer.getForegroundApp(this@AppMonitorService)
                if (foregroundPackage != null && foregroundPackage != currentPackageName) {
                    recordCurrentAppUsage()
                    currentPackageName = foregroundPackage
                    currentStartTime = System.currentTimeMillis()
                }
                delay(POLL_INTERVAL_MS)
            }
        }
        updateNotification("Monitoring foreground apps...")
    }

    private fun stopMonitoring() {
        monitoringJob?.cancel()
        monitoringJob = null
        recordCurrentAppUsage()
        currentPackageName = null
        currentStartTime = 0L
    }

    private suspend fun recordCurrentAppUsage() {
        val packageName = currentPackageName ?: return
        val duration = ((System.currentTimeMillis() - currentStartTime) / 1000).toInt()

        if (duration <= 0) return

        val appUsage = AppUsage(
            packageName = packageName,
            category = AppCategorizer.categorizeApp(packageName),
            usageDuration = duration,
            sessionId = sessionId
        )

        try {
            appUsageDao.insertAppUsage(appUsage)
            Log.d(TAG, "Recorded usage: $packageName - ${duration}s")
        } catch (e: Exception) {
            Log.e(TAG, "Failed to record app usage: ${e.message}")
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "App Monitor",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "EdgeFocus app monitoring service"
            }
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(channel)
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
