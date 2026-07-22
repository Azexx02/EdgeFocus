package com.edgefocus.app.data.remote

data class PhoneUsage(
    val isInUse: Boolean,
    val appCategory: String? = null,
    val usageState: String? = appCategory,
    val sessionId: String? = null,
    val sequence: Long? = null,
    val timestampMs: Long? = null,
    @Deprecated("The host accepts raw facts only; this value is never serialized")
    val distractionLevel: Int = 0,
    val appName: String? = null,
    val durationSec: Int? = null
)

data class LocalSessionUpload(
    val sessionId: String,
    val startTimeMs: Long,
    val endTimeMs: Long,
    val totalDurationSec: Int,
    val focusDurationSec: Int,
    val distractionCount: Int,
    val reminderCount: Int,
    val phoneDistractionCount: Int,
    val source: String = "app_local"
)

interface BleCommand {
    suspend fun clientHello(): Result<ByteArray>
    suspend fun getDeviceStatus(): Result<ByteArray>
    suspend fun getCurrentState(): Result<ByteArray>
    suspend fun getTodaySummary(): Result<ByteArray>
    suspend fun getLastReport(): Result<ByteArray>
    suspend fun getSessionHistory(cursor: String? = null, limit: Int = 1): Result<ByteArray>
    suspend fun getPreferences(): Result<ByteArray>
    suspend fun getCameraPreviewImage(): Result<ByteArray>
    suspend fun getGimbalStatus(): Result<ByteArray>
    suspend fun getBandStatus(): Result<ByteArray>
    suspend fun sessionControl(
        action: String,
        targetDurationMin: Int? = null,
        operationId: String? = null,
        sessionId: String? = null
    ): Result<ByteArray>
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
    suspend fun gimbalPrecheckCommand(
        command: String,
        sessionId: String,
        sequence: Long,
        direction: String? = null,
        stepDeg: Double? = null,
        ownerId: String? = null,
        leaseToken: String? = null,
        fineTune: Boolean? = null,
        confirmed: Boolean? = null
    ): Result<ByteArray>
    suspend fun bandControl(action: String): Result<ByteArray>
    suspend fun setPowerProfile(profile: String): Result<ByteArray>
    suspend fun captureForwardOrigin(sequence: Long): Result<ByteArray>
    suspend fun eventAck(eventId: String, sessionId: String, stage: String): Result<ByteArray>
    suspend fun sessionFeedback(sessionId: String, feedback: String): Result<ByteArray>
    suspend fun reminderAck(alertId: String): Result<ByteArray>
    suspend fun phoneSync(usage: PhoneUsage): Result<ByteArray>
    suspend fun uploadLocalSession(session: LocalSessionUpload): Result<ByteArray>
    suspend fun getConsoleData(): Result<ByteArray>
    suspend fun cancelPreviewRequests()
    suspend fun cancelGimbalControlRequests()
}
