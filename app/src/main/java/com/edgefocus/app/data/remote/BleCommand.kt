package com.edgefocus.app.data.remote

data class PhoneUsage(
    val screenTimeMinutes: Int,
    val unlockCount: Int,
    val topApps: List<String>
)

interface BleCommand {
    suspend fun getDeviceStatus(): Result<ByteArray>
    suspend fun getCurrentState(): Result<ByteArray>
    suspend fun getTodaySummary(): Result<ByteArray>
    suspend fun getLastReport(): Result<ByteArray>
    suspend fun sessionControl(action: String, duration: Int? = null): Result<ByteArray>
    suspend fun updatePreferences(key: String, value: String): Result<ByteArray>
    suspend fun reminderAck(alertId: String): Result<ByteArray>
    suspend fun phoneSync(usage: PhoneUsage): Result<ByteArray>
}
