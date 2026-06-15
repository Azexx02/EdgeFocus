package com.edgefocus.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.PhoneUsage
import dagger.hilt.android.AndroidEntryPoint
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import javax.inject.Inject

@AndroidEntryPoint
class BleForegroundService : Service() {

    companion object {
        private const val TAG = "BleForegroundService"
        private const val CHANNEL_ID = "ble_service_channel"
        private const val NOTIFICATION_ID = 1001

        const val ACTION_START_SCAN = "com.edgefocus.app.ACTION_START_SCAN"
        const val ACTION_CONNECT = "com.edgefocus.app.ACTION_CONNECT"
        const val ACTION_DISCONNECT = "com.edgefocus.app.ACTION_DISCONNECT"
        const val ACTION_SEND_DATA = "com.edgefocus.app.ACTION_SEND_DATA"
        const val EXTRA_DATA = "extra_data"

        fun startService(context: Context) {
            val intent = Intent(context, BleForegroundService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stopService(context: Context) {
            context.stopService(Intent(context, BleForegroundService::class.java))
        }
    }

    @Inject
    lateinit var bleManager: BleManager

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, createNotification("BLE Service Running"))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_SCAN -> handleStartScan()
            ACTION_CONNECT -> handleConnect()
            ACTION_DISCONNECT -> handleDisconnect()
            ACTION_SEND_DATA -> handleSendData(intent)
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        super.onDestroy()
        serviceScope.cancel()
        bleManager.disconnect()
    }

    private fun handleStartScan() {
        serviceScope.launch {
            bleManager.startScan()
            updateNotification("Scanning for devices...")
        }
    }

    private fun handleConnect() {
        serviceScope.launch {
            bleManager.stopScan()
            updateNotification("Connecting...")
        }
    }

    private fun handleDisconnect() {
        serviceScope.launch {
            bleManager.disconnect()
            updateNotification("Disconnected")
        }
    }

    private fun handleSendData(intent: Intent) {
        val data = intent.getByteArrayExtra(EXTRA_DATA) ?: return
        serviceScope.launch {
            bleManager.sendData(data).onFailure { e ->
                Log.e(TAG, "Failed to send data: ${e.message}")
            }
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "BLE Service",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "EdgeFocus BLE connection service"
            }
            val notificationManager = getSystemService(NotificationManager::class.java)
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(text: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("EdgeFocus")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(text: String) {
        val notificationManager = getSystemService(NotificationManager::class.java)
        notificationManager.notify(NOTIFICATION_ID, createNotification(text))
    }
}
