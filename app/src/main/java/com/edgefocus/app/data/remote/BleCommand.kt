package com.edgefocus.app.data.remote

data class PhoneUsage(
    val isInUse: Boolean,
    val appCategory: String? = null,
    val distractionLevel: Int = 3,
    val appName: String? = null,
    val durationSec: Int? = null
)

interface BleCommand {
    suspend fun getDeviceStatus(): Result<ByteArray>
    suspend fun getCurrentState(): Result<ByteArray>
    suspend fun getTodaySummary(): Result<ByteArray>
    suspend fun getLastReport(): Result<ByteArray>
    suspend fun getSessionHistory(cursor: String? = null, limit: Int = 1): Result<ByteArray>
    suspend fun getPreferences(): Result<ByteArray>
    suspend fun getCameraPreviewImage(): Result<ByteArray>
    suspend fun getGimbalStatus(): Result<ByteArray>
    suspend fun sessionControl(action: String, targetDurationMin: Int? = null): Result<ByteArray>
    suspend fun updatePreferences(key: String, value: String): Result<ByteArray>
    suspend fun updatePreferences(preferences: Map<String, Any?>): Result<ByteArray>
    suspend fun gimbalControl(
        action: String,
        xDeg: Number? = null,
        yDeg: Number? = null,
        speed: Int? = null,
        stepDeg: Int? = null,
        debug: Boolean? = null
    ): Result<ByteArray>
    suspend fun sessionFeedback(sessionId: String, feedback: String): Result<ByteArray>
    suspend fun reminderAck(alertId: String): Result<ByteArray>
    suspend fun phoneSync(usage: PhoneUsage): Result<ByteArray>
    suspend fun getConsoleData(): Result<ByteArray>
}
