package com.edgefocus.app.data.remote

data class PhoneUsage(
    val screenTimeMinutes: Int,
    val unlockCount: Int,
    val topApps: List<String>
)

interface BleCommand {
    suspend fun getDeviceStatus(): ByteArray
    suspend fun getCurrentState(): ByteArray
    suspend fun getTodaySummary(): ByteArray
    suspend fun getLastReport(): ByteArray
    suspend fun sessionControl(action: String, duration: Int? = null): ByteArray
    suspend fun updatePreferences(key: String, value: String): ByteArray
    suspend fun reminderAck(alertId: String): ByteArray
    suspend fun phoneSync(usage: PhoneUsage): ByteArray
}
