package com.edgefocus.app.data.remote

import org.json.JSONArray
import org.json.JSONObject

enum class BoardSessionPhase(val wireValue: String) {
    IDLE("idle"),
    PRECHECK("precheck"),
    FOCUSING("focusing"),
    PAUSED("paused");

    companion object {
        fun fromWireOrNull(value: String?): BoardSessionPhase? {
            val normalized = value?.trim()?.lowercase()
            return entries.firstOrNull { it.wireValue == normalized }
        }

        fun fromWire(value: String?): BoardSessionPhase {
            return fromWireOrNull(value) ?: IDLE
        }
    }
}

enum class BoardPreparationState {
    NONE,
    INITIALIZING,
    READY,
    DEGRADED,
    FAILED;

    companion object {
        fun fromWire(value: String?, phase: BoardSessionPhase): BoardPreparationState {
            val normalized = value?.trim().orEmpty()
            if (normalized.isBlank()) {
                return if (phase == BoardSessionPhase.PRECHECK) READY else NONE
            }
            val parsed = entries.firstOrNull { it.name.equals(normalized, ignoreCase = true) }
            if (parsed != null) return parsed
            return if (phase == BoardSessionPhase.PRECHECK) INITIALIZING else NONE
        }
    }
}

enum class NotificationMode(val wireValue: String) {
    NORMAL("normal"),
    SILENT("silent");

    companion object {
        fun fromWire(value: String?): NotificationMode? {
            val normalized = value?.trim()?.lowercase()?.takeIf { it.isNotBlank() } ?: return null
            return entries.firstOrNull { it.wireValue == normalized }
        }
    }
}

enum class PowerProfile(val wireValue: String) {
    HOME("home"),
    GO("go");

    companion object {
        fun fromWire(value: String?): PowerProfile? {
            val normalized = value?.trim()?.lowercase()?.takeIf { it.isNotBlank() } ?: return null
            return entries.firstOrNull { it.wireValue == normalized }
        }
    }
}

data class BoardSessionState(
    val success: Boolean = true,
    val alreadyActive: Boolean = false,
    val sessionId: String? = null,
    val active: Boolean = false,
    val paused: Boolean = false,
    val focusState: Int = 0,
    val phoneSignal: Boolean = false,
    val phoneUsageState: String = "unknown",
    val phoneDistractionLevel: Int = -1,
    val riskScore: Int = 0,
    val commandLevel: Int = 0,
    val batteryPercent: Int = 67,
    val batteryCharging: Boolean = false,
    val powerSource: String = "battery",
    val powerProfile: PowerProfile = PowerProfile.GO,
    val elapsedMs: Long = 0L,
    val remainingMs: Long = 0L,
    val hasRemainingMs: Boolean = false,
    val targetDurationMs: Long = 0L,
    val hasTargetDurationMs: Boolean = false,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val calibrationCompleted: Boolean = false,
    val sessionPhase: BoardSessionPhase = BoardSessionPhase.IDLE,
    val preparationState: BoardPreparationState = BoardPreparationState.NONE,
    val preparationError: String? = null,
    val preparationFailedAtMs: Long = 0L,
    val startGeneration: Long = 0L,
    val operationId: String? = null,
    val sessionStartSource: String? = null,
    val precheck: BoardPrecheck? = null,
    val precheckControl: PrecheckControl? = null,
    val outputCapability: OutputCapability? = null,
    val reminder: String? = null,
    val precheckGuidance: String? = null,
    val cameraPreview: CameraPreviewImage? = null,
    val focusReason: String? = null,
    val behaviorContext: String? = null,
    val partialGimbalPrecheckPush: Boolean = false
)

fun BoardSessionState.representsActiveRuntime(): Boolean =
    sessionPhase != BoardSessionPhase.IDLE || active || paused || remainingMs > 0L || precheck != null

fun BoardSessionState.effectivePhase(): BoardSessionPhase = when {
    sessionPhase != BoardSessionPhase.IDLE -> sessionPhase
    paused || focusState == 6 -> BoardSessionPhase.PAUSED
    active || remainingMs > 0L -> BoardSessionPhase.FOCUSING
    else -> BoardSessionPhase.IDLE
}

data class PrecheckControl(
    val owner: String = "board",
    val appCanSkipPrecheck: Boolean = false,
    val boardCanContinue: Boolean = false
)

data class BoardPrecheck(
    val camera: PrecheckCamera? = null,
    val gimbal: PrecheckGimbal? = null
)

data class PrecheckCamera(
    val required: Boolean = false,
    val guidance: String? = null
)

data class PrecheckGimbal(
    val enabled: Boolean = false,
    val online: Boolean = false,
    val safe: Boolean = false,
    val angleX: Double? = null,
    val angleY: Double? = null,
    val state: String = "idle",
    val controlMode: String? = null,
    val manualLease: Boolean = false,
    val manualControlAvailable: Boolean = manualLease,
    val searchCompleted: Boolean = false,
    val skipSearchEnabled: Boolean = false,
    val communicationHealth: String? = null,
    val hardwareFaultActive: Boolean = false,
    val hardwareFaultCode: String? = null,
    val lastCommandErrorCode: String? = null,
    val lastCommandErrorRetryable: Boolean = false,
    val currentYawDeg: Double? = null,
    val currentPitchDeg: Double? = null,
    val targetYawDeg: Double? = null,
    val targetPitchDeg: Double? = null,
    val motionGeneration: Long? = null,
    val settled: Boolean? = null,
    val calibrationValid: Boolean? = null,
    val originValid: Boolean? = null,
    val calibrationEpoch: Long? = null,
    val originEpoch: Long? = null,
    val searchGeneration: Long? = null,
    val waypointIndex: Int? = null,
    val detail: String? = null
)

data class OutputCapability(
    val boardScreenAvailable: Boolean = false,
    val boardSoundAvailable: Boolean = false,
    val screenOutputRoute: String = "board",
    val soundOutputRoute: String = "board",
    val bandAvailable: Boolean = false,
    val lastAttemptedChannels: List<String> = emptyList(),
    val lastDeliveredChannels: List<String> = emptyList()
)

data class CameraPreviewImage(
    val available: Boolean = false,
    val timestampMs: Long = 0L,
    val sequence: Int = 0,
    val width: Int = 0,
    val height: Int = 0,
    val pixelFormat: String? = null,
    val encoding: String? = null,
    val source: String? = null,
    val dataBase64: String? = null,
    val quality: Double? = null
)

data class GimbalStatus(
    val connected: Boolean = false,
    val online: Boolean = false,
    val safe: Boolean = false,
    val xhome: Boolean = false,
    val yhome: Boolean = false,
    val calibrated: Boolean = false,
    val angleX: Double? = null,
    val angleY: Double? = null,
    val state: String? = null,
    val controlMode: String? = null,
    val communicationHealth: String? = null,
    val hardwareFaultActive: Boolean = false,
    val hardwareFaultCode: String? = null,
    val lastCommandErrorCode: String? = null,
    val lastCommandErrorRetryable: Boolean = false,
    val currentYawDeg: Double? = null,
    val currentPitchDeg: Double? = null,
    val targetYawDeg: Double? = null,
    val targetPitchDeg: Double? = null,
    val motionGeneration: Long? = null,
    val settled: Boolean? = null,
    val calibrationValid: Boolean? = null,
    val originValid: Boolean? = null,
    val calibrationEpoch: Long? = null,
    val originEpoch: Long? = null,
    val manualLease: Boolean = false,
    val lastError: String? = null,
    val enabled: Boolean = false
)

data class PushAlert(
    val level: Int = 0,
    val message: String = "",
    val phoneActions: List<String> = emptyList(),
    val soundVolume: Float? = null,
    val reminderCount: Int? = null,
    val riskScore: Double? = null,
    val phoneDistractionLevel: Int? = null,
    val visionSignal: String? = null,
    val phoneSignal: String? = null,
    val reason: String? = null,
    val outputCapability: OutputCapability? = null
)

data class BoardPreferences(
    val systemMode: Int = 0,
    val notificationMode: NotificationMode = NotificationMode.NORMAL,
    val powerProfile: PowerProfile = PowerProfile.GO,
    val awayTimeoutMin: Int = 15,
    val defaultFocusDurationMin: Int = 25,
    val soundVolume: Float = 0.7f,
    val normalChannels: Set<String> = setOf("screen", "band", "sound"),
    val silentChannels: Set<String> = setOf("screen", "band"),
    val screenOutputRoute: String = "board",
    val soundOutputRoute: String = "board",
    val gimbalEnabled: Boolean = true
)

object BoardJsonParser {

    fun parseResponseError(jsonString: String): String? {
        val root = JSONObject(jsonString)
        if (!root.optString("status").equals("error", ignoreCase = true)) return null
        return root.optString("error").takeIf { it.isNotBlank() }
            ?: root.optString("message").takeIf { it.isNotBlank() }
            ?: "EdgeFocus 拒绝了请求"
    }

    fun buildCommand(command: String): JSONObject {
        return JSONObject().apply {
            put("cmd", command)
        }
    }

    fun buildSessionControl(
        action: String,
        targetDurationMin: Int? = null,
        operationId: String? = null,
        sessionId: String? = null
    ): JSONObject {
        val data = JSONObject().apply {
            put("action", action)
            if (action == "start") {
                targetDurationMin?.let { put("target_duration_min", it) }
                put("source_mode", "live_camera")
                operationId?.takeIf { it.isNotBlank() }?.let { put("operation_id", it) }
            }
            sessionId?.takeIf { it.isNotBlank() }?.let { put("session_id", it) }
        }
        return JSONObject().apply {
            put("cmd", "session_control")
            put("data", data)
        }
    }

    fun buildUpdatePreferences(preferences: Map<String, Any?>): JSONObject {
        val data = JSONObject()
        preferences.forEach { (key, value) ->
            if (value != null) {
                data.put(key, toJsonValue(value))
            }
        }
        return JSONObject().apply {
            put("cmd", "update_preferences")
            put("data", data)
        }
    }

    fun buildGimbalControl(
        action: String,
        xDeg: Number? = null,
        yDeg: Number? = null,
        speed: Int? = null,
        stepDeg: Int? = null,
        debug: Boolean? = null
    ): JSONObject {
        val data = JSONObject().apply {
            put("action", action)
            xDeg?.let { put("x_deg", it) }
            yDeg?.let { put("y_deg", it) }
            speed?.let { put("speed", it) }
            stepDeg?.let { put("step_deg", it) }
            debug?.let { put("debug", it) }
        }
        return JSONObject().apply {
            put("cmd", "gimbal_control")
            put("data", data)
        }
    }

    fun buildPhoneSync(usage: PhoneUsage): JSONObject {
        val data = JSONObject().apply {
            put("is_in_use", usage.isInUse)
            usage.sessionId?.takeIf { it.isNotBlank() }?.let { put("session_id", it) }
            usage.sequence?.takeIf { it > 0 }?.let { put("sequence", it) }
            usage.timestampMs?.let { put("timestamp_ms", it) }
            if (usage.isInUse) {
                val state = when ((usage.usageState ?: usage.appCategory).orEmpty().lowercase()) {
                    "learning" -> "learning"
                    "playing", "entertainment" -> "playing"
                    else -> "using"
                }
                put("usage_state", state)
            }
        }
        return JSONObject().apply {
            put("cmd", "phone_sync")
            put("data", data)
        }
    }

    fun buildUploadLocalSession(session: LocalSessionUpload): JSONObject {
        val data = JSONObject().apply {
            put("session_id", session.sessionId)
            put("start_time_ms", session.startTimeMs)
            put("end_time_ms", session.endTimeMs)
            put("total_duration", session.totalDurationSec)
            put("focus_duration", session.focusDurationSec)
            put("distraction_count", session.distractionCount)
            put("reminder_count", session.reminderCount)
            put("phone_distraction_count", session.phoneDistractionCount)
            put("source", session.source)
        }
        return JSONObject().apply {
            put("cmd", "upload_local_session")
            put("data", data)
        }
    }

    fun parseSessionState(jsonString: String): BoardSessionState {
        val root = JSONObject(jsonString)
        val data = root.optJSONObject("data") ?: JSONObject()
        return parseSessionState(data)
    }

    fun parseSessionState(data: JSONObject): BoardSessionState {
        val runtimeSnapshot = data.optJSONObject("runtime_snapshot")
        val runtimeSession = runtimeSnapshot?.optJSONObject("session")
        val runtimeGimbal = runtimeSnapshot?.optJSONObject("gimbal")
        val runtimeHasRemainingMs = runtimeSession?.let {
            it.has("remaining_ms") && !it.isNull("remaining_ms")
        } == true
        val hasRemainingMs = runtimeHasRemainingMs ||
            (data.has("remaining_ms") && !data.isNull("remaining_ms"))
        val hasTargetDurationMs = data.has("target_duration_ms") && !data.isNull("target_duration_ms")
        val phase = BoardSessionPhase.fromWire(
            runtimeSession?.optString("phase")?.takeIf { it.isNotBlank() }
                ?: data.optString("session_phase").takeIf { it.isNotBlank() }
        )
        val preparationState = BoardPreparationState.fromWire(
            runtimeSession?.optString("preparation_state")?.takeIf { it.isNotBlank() },
            phase
        )
        return BoardSessionState(
            success = data.optBoolean("success", true),
            alreadyActive = data.optBoolean("already_active", false),
            sessionId = runtimeSession?.optString("id")?.takeIf { it.isNotBlank() }
                ?: data.optString("session_id").takeIf { it.isNotBlank() },
            active = runtimeSession?.optBoolean("active", data.optBoolean("active", false))
                ?: data.optBoolean("active", false),
            paused = if (runtimeSession != null) {
                phase == BoardSessionPhase.PAUSED
            } else {
                data.optBoolean("paused", false)
            },
            focusState = data.optInt("focus_state", 0),
            phoneSignal = data.optBoolean("phone_signal", false),
            phoneUsageState = data.optString("phone_usage_state", "unknown").ifBlank { "unknown" },
            phoneDistractionLevel = data.optInt("phone_distraction_level", -1),
            riskScore = data.optInt("risk_score", 0),
            commandLevel = data.optInt("command_level", 0),
            batteryPercent = data.optBatteryPercent("battery_percent"),
            batteryCharging = data.optBoolean("battery_charging", false),
            powerSource = data.optJsonString("power_source") ?: "battery",
            powerProfile = PowerProfile.fromWire(data.optJsonString("power_profile")) ?: PowerProfile.GO,
            elapsedMs = runtimeSession?.optLong("elapsed_ms", data.optLong("elapsed_ms", 0L))
                ?: data.optLong("elapsed_ms", 0L),
            remainingMs = runtimeSession?.optLong("remaining_ms", data.optLong("remaining_ms", 0L))
                ?: data.optLong("remaining_ms", 0L),
            hasRemainingMs = hasRemainingMs,
            targetDurationMs = data.optLong("target_duration_ms", 0L),
            hasTargetDurationMs = hasTargetDurationMs,
            distractionCount = data.optInt("distraction_count", 0),
            reminderCount = data.optInt("reminder_count", 0),
            phoneDistractionCount = data.optInt("phone_distraction_count", 0),
            calibrationCompleted = if (runtimeSession != null) {
                phase == BoardSessionPhase.FOCUSING || phase == BoardSessionPhase.PAUSED
            } else {
                data.optBoolean("calibration_completed", false)
            },
            sessionPhase = phase,
            preparationState = preparationState,
            preparationError = runtimeSession?.optString("preparation_error")
                ?.takeIf { it.isNotBlank() && it != "null" },
            preparationFailedAtMs = runtimeSession?.optLong("preparation_failed_at_ms", 0L) ?: 0L,
            startGeneration = runtimeSession?.optLong("start_generation", 0L) ?: 0L,
            operationId = runtimeSession?.optString("operation_id")
                ?.takeIf { it.isNotBlank() && it != "null" },
            sessionStartSource = data.optJsonString("session_start_source"),
            precheck = parsePrecheck(data.optJSONObject("precheck"), runtimeGimbal),
            precheckControl = parsePrecheckControl(data.optJSONObject("precheck_control")),
            outputCapability = parseOutputCapability(data.optJSONObject("output_capability")),
            reminder = data.optJsonString("reminder"),
            precheckGuidance = parsePrecheckGuidance(data),
            cameraPreview = parseCameraPreviewObject(data.optJSONObject("camera_preview"))
                ?: parseCameraPreviewObject(
                    data.optJSONObject("precheck")
                        ?.optJSONObject("camera")
                        ?.optJSONObject("preview")
                ),
            focusReason = data.optJsonString("focus_reason"),
            behaviorContext = data.optJsonString("behavior_context")
        )
    }

    fun parseCameraPreview(jsonString: String): CameraPreviewImage {
        val data = JSONObject(jsonString).optJSONObject("data") ?: JSONObject()
        return parseCameraPreviewObject(data) ?: CameraPreviewImage()
    }

    fun parseGimbalStatus(jsonString: String): GimbalStatus {
        val data = JSONObject(jsonString).optJSONObject("data") ?: JSONObject()
        val currentYaw = data.optDoubleOrNull("current_yaw_deg")
            ?: data.optDoubleOrNull("angle_y")
        val currentPitch = data.optDoubleOrNull("current_pitch_deg")
            ?: data.optDoubleOrNull("angle_x")
        return GimbalStatus(
            connected = data.optBoolean("connected", false),
            online = data.optBoolean("online", false),
            safe = data.optBoolean("safe", false),
            xhome = data.optBoolean("xhome", false),
            yhome = data.optBoolean("yhome", false),
            calibrated = data.optBoolean("calibrated", false),
            angleX = data.optDoubleOrNull("angle_x"),
            angleY = data.optDoubleOrNull("angle_y"),
            state = data.optJsonString("state"),
            controlMode = data.optJsonString("control_mode"),
            communicationHealth = data.optJsonString("communication_health")
                ?: data.optJsonString("health_status"),
            hardwareFaultActive = data.optBoolean("hardware_fault_active", false),
            hardwareFaultCode = data.optJsonString("hardware_fault_code"),
            lastCommandErrorCode = data.optJsonString("last_command_error_code"),
            lastCommandErrorRetryable = data.optBoolean("last_command_error_retryable", false),
            currentYawDeg = currentYaw,
            currentPitchDeg = currentPitch,
            targetYawDeg = data.optDoubleOrNull("target_yaw_deg"),
            targetPitchDeg = data.optDoubleOrNull("target_pitch_deg"),
            motionGeneration = data.optLongOrNull("motion_generation"),
            settled = data.optBooleanOrNull("settled"),
            calibrationValid = data.optBooleanOrNull("calibration_valid"),
            originValid = data.optBooleanOrNull("origin_valid"),
            calibrationEpoch = data.optLongOrNull("calibration_epoch"),
            originEpoch = data.optLongOrNull("origin_epoch"),
            manualLease = data.optBoolean("manual_lease", false),
            lastError = data.optString("last_error").takeIf { it.isNotBlank() },
            enabled = data.optBoolean("enabled", false)
        )
    }

    fun parsePushAlert(data: JSONObject): PushAlert {
        return PushAlert(
            level = data.optInt("level", 0),
            message = data.optString("message", ""),
            phoneActions = data.optStringList("phone_actions"),
            soundVolume = data.optDoubleOrNull("sound_volume")?.toFloat(),
            reminderCount = data.optIntOrNull("reminder_count"),
            riskScore = data.optDoubleOrNull("risk_score"),
            phoneDistractionLevel = data.optIntOrNull("phone_distraction_level"),
            visionSignal = data.optJsonString("vision_signal"),
            phoneSignal = data.optJsonString("phone_signal"),
            reason = data.optJsonString("reason"),
            outputCapability = parseOutputCapability(data.optJSONObject("output_capability"))
        )
    }

    fun parseGimbalPrecheckState(data: JSONObject): BoardSessionState {
        val gimbal = parsePrecheckGimbal(legacy = null, runtime = data)
        val degraded = data.optBoolean("degraded", false) ||
            gimbal?.state.equals("degraded", ignoreCase = true) ||
            gimbal?.hardwareFaultActive == true
        return BoardSessionState(
            sessionId = data.optJsonString("session_id"),
            active = true,
            sessionPhase = BoardSessionPhase.PRECHECK,
            preparationState = if (degraded) {
                BoardPreparationState.DEGRADED
            } else {
                BoardPreparationState.READY
            },
            precheck = BoardPrecheck(gimbal = gimbal),
            partialGimbalPrecheckPush = true
        )
    }

    fun parseTimerComplete(data: JSONObject): TimerComplete {
        return TimerComplete(
            sessionId = data.optJsonString("session_id"),
            startTimeMs = data.optLong("start_time_ms", 0L),
            endTimeMs = data.optLong("end_time_ms", 0L),
            totalDurationSec = data.optInt("total_duration", 0),
            focusDurationSec = data.optInt("focus_duration", 0),
            elapsedMs = data.optLong("elapsed_ms", 0L),
            targetDurationMs = data.optLong("target_duration_ms", 0L),
            distractionCount = data.optInt("distraction_count", 0),
            reminderCount = data.optInt("reminder_count", 0),
            phoneDistractionCount = data.optInt("phone_distraction_count", 0),
            endReason = data.optJsonString("end_reason")
        )
    }

    fun parseChannelNames(array: JSONArray?): Set<String> {
        if (array == null) return emptySet()
        return buildSet {
            for (index in 0 until array.length()) {
                array.optString(index)
                    .trim()
                    .lowercase()
                    .takeIf { it.isNotBlank() }
                    ?.let { add(it) }
            }
        }
    }

    fun parsePreferences(data: JSONObject, defaults: BoardPreferences = BoardPreferences()): BoardPreferences {
        val systemMode = data.optInt("system_mode", defaults.systemMode)
        val legacyNotificationMode = when (systemMode) {
            1 -> NotificationMode.SILENT
            else -> NotificationMode.NORMAL
        }
        val legacyPowerProfile = when (systemMode) {
            2 -> PowerProfile.HOME
            else -> PowerProfile.GO
        }
        return BoardPreferences(
            systemMode = systemMode,
            notificationMode = NotificationMode.fromWire(data.optJsonString("notification_mode"))
                ?: legacyNotificationMode,
            powerProfile = PowerProfile.fromWire(data.optJsonString("power_profile"))
                ?: legacyPowerProfile,
            awayTimeoutMin = data.optInt("away_timeout_min", defaults.awayTimeoutMin),
            defaultFocusDurationMin = data.optInt(
                "default_focus_duration_min",
                defaults.defaultFocusDurationMin
            ),
            soundVolume = data.optDouble("sound_volume", defaults.soundVolume.toDouble()).toFloat(),
            normalChannels = data.optJSONArray("normal_channels")?.let { parseChannelNames(it) }
                ?: maskToChannels(data.optInt("normal_channels_mask", channelMask(defaults.normalChannels)), true),
            silentChannels = data.optJSONArray("silent_channels")?.let { parseChannelNames(it) }
                ?: maskToChannels(data.optInt("silent_channels_mask", channelMask(defaults.silentChannels)), false),
            screenOutputRoute = data.optString("screen_output_route", defaults.screenOutputRoute),
            soundOutputRoute = data.optString("sound_output_route", defaults.soundOutputRoute),
            gimbalEnabled = data.optBoolean("gimbal_enabled", defaults.gimbalEnabled)
        )
    }

    private fun parsePrecheck(json: JSONObject?, runtimeGimbal: JSONObject?): BoardPrecheck? {
        if (json == null && runtimeGimbal == null) return null
        return BoardPrecheck(
            camera = json?.optJSONObject("camera")?.let {
                PrecheckCamera(
                    required = it.optBoolean("required", false),
                    guidance = it.optJSONObject("guidance")?.toString()
                )
            },
            gimbal = parsePrecheckGimbal(json?.optJSONObject("gimbal"), runtimeGimbal)
        )
    }

    private fun parsePrecheckGimbal(
        legacy: JSONObject?,
        runtime: JSONObject?
    ): PrecheckGimbal? {
        if (legacy == null && runtime == null) return null
        val available = runtime?.optBooleanOrNull("available")
            ?: legacy?.optBooleanOrNull("enabled")
            ?: false
        val communicationHealth = runtime?.optJsonString("communication_health")
            ?: runtime?.optJsonString("health_status")
            ?: legacy?.optJsonString("communication_health")
        val normalizedHealth = communicationHealth?.trim()?.lowercase()
        val online = runtime?.optBooleanOrNull("online")
            ?: legacy?.optBooleanOrNull("online")
            ?: (available && normalizedHealth !in setOf("not_started", "offline", "failed"))
        val hardwareFault = runtime?.optBooleanOrNull("hardware_fault_active")
            ?: runtime?.optBooleanOrNull("degraded")
            ?: legacy?.optBooleanOrNull("hardware_fault_active")
            ?: false
        val currentYaw = runtime?.optDoubleOrNull("current_yaw_deg")
            ?: legacy?.optDoubleOrNull("current_yaw_deg")
            ?: runtime?.optDoubleOrNull("angle_y")
            ?: legacy?.optDoubleOrNull("angle_y")
        val currentPitch = runtime?.optDoubleOrNull("current_pitch_deg")
            ?: legacy?.optDoubleOrNull("current_pitch_deg")
            ?: runtime?.optDoubleOrNull("angle_x")
            ?: legacy?.optDoubleOrNull("angle_x")
        val manualLease = runtime?.optBooleanOrNull("manual_lease")
            ?: legacy?.optBooleanOrNull("manual_lease")
            ?: false
        val state = runtime?.optJsonString("state")
            ?: legacy?.optJsonString("state")
            ?: "idle"
        val controlMode = runtime?.optJsonString("control_mode")
            ?: legacy?.optJsonString("control_mode")
        val detail = runtime?.optJsonString("detail") ?: legacy?.optJsonString("detail")
        val controls = runtime?.optJSONObject("controls")
        val inferredManualControlAvailable =
            state.equals("manual_available", ignoreCase = true) ||
                state.equals("manual", ignoreCase = true) ||
                state.equals("holding", ignoreCase = true) ||
                controlMode.equals("manual", ignoreCase = true) ||
                detail?.contains("manual_available", ignoreCase = true) == true
        return PrecheckGimbal(
            enabled = available,
            online = online,
            safe = legacy?.optBooleanOrNull("safe")
                ?: (available && online && !hardwareFault && normalizedHealth in setOf("healthy", "ready")),
            angleX = runtime?.optDoubleOrNull("angle_x")
                ?: legacy?.optDoubleOrNull("angle_x")
                ?: currentPitch,
            angleY = runtime?.optDoubleOrNull("angle_y")
                ?: legacy?.optDoubleOrNull("angle_y")
                ?: currentYaw,
            state = state,
            controlMode = controlMode,
            manualLease = manualLease,
            manualControlAvailable = controls?.optBooleanOrNull("manual_enabled")
                ?: runtime?.optBooleanOrNull("manual_control_available")
                ?: legacy?.optBooleanOrNull("manual_control_available")
                ?: (manualLease || inferredManualControlAvailable),
            searchCompleted = runtime?.optBooleanOrNull("search_completed")
                ?: legacy?.optBooleanOrNull("search_completed")
                ?: inferredManualControlAvailable,
            skipSearchEnabled = controls?.optBooleanOrNull("skip_search_enabled")
                ?: (state.lowercase() in setOf("searching", "centering", "framing")),
            communicationHealth = communicationHealth,
            hardwareFaultActive = hardwareFault,
            hardwareFaultCode = runtime?.optJsonString("hardware_fault_code")
                ?: runtime?.optJsonString("fault_code")
                ?: legacy?.optJsonString("hardware_fault_code"),
            lastCommandErrorCode = runtime?.optJsonString("last_command_error_code")
                ?: legacy?.optJsonString("last_command_error_code"),
            lastCommandErrorRetryable = runtime?.optBooleanOrNull("last_command_error_retryable")
                ?: legacy?.optBooleanOrNull("last_command_error_retryable")
                ?: false,
            currentYawDeg = currentYaw,
            currentPitchDeg = currentPitch,
            targetYawDeg = runtime?.optDoubleOrNull("target_yaw_deg")
                ?: legacy?.optDoubleOrNull("target_yaw_deg"),
            targetPitchDeg = runtime?.optDoubleOrNull("target_pitch_deg")
                ?: legacy?.optDoubleOrNull("target_pitch_deg"),
            motionGeneration = runtime?.optLongOrNull("motion_generation")
                ?: legacy?.optLongOrNull("motion_generation"),
            settled = runtime?.optBooleanOrNull("settled")
                ?: legacy?.optBooleanOrNull("settled"),
            calibrationValid = runtime?.optBooleanOrNull("calibration_valid")
                ?: legacy?.optBooleanOrNull("calibration_valid"),
            originValid = runtime?.optBooleanOrNull("origin_valid")
                ?: legacy?.optBooleanOrNull("origin_valid"),
            calibrationEpoch = runtime?.optLongOrNull("calibration_epoch")
                ?: legacy?.optLongOrNull("calibration_epoch"),
            originEpoch = runtime?.optLongOrNull("origin_epoch")
                ?: legacy?.optLongOrNull("origin_epoch"),
            searchGeneration = runtime?.optLongOrNull("search_generation")
                ?: legacy?.optLongOrNull("search_generation"),
            waypointIndex = runtime?.optIntOrNull("waypoint_index")
                ?: legacy?.optIntOrNull("waypoint_index"),
            detail = detail
        )
    }

    private fun parsePrecheckGuidance(data: JSONObject): String? {
        val direct = data.opt("precheck_guidance")
        if (direct is String && direct.isNotBlank()) return direct

        val guidance = (direct as? JSONObject)
            ?: data.optJSONObject("precheck")
                ?.optJSONObject("camera")
                ?.optJSONObject("guidance")
            ?: return null
        val primaryHint = listOf(
            guidance.optString("distance_hint"),
            guidance.optString("pose_hint"),
            guidance.optString("frame_quality_reason")
        ).firstOrNull { it.isNotBlank() }
        if (primaryHint != null) return primaryHint

        val hasOtherText = guidance.keys().asSequence().any { key ->
            guidance.opt(key) is String && guidance.optString(key).isNotBlank()
        }
        return guidance.toString().takeIf { hasOtherText }
    }

    private fun parsePrecheckControl(json: JSONObject?): PrecheckControl? {
        if (json == null) return null
        return PrecheckControl(
            owner = json.optString("owner", "board"),
            appCanSkipPrecheck = json.optBoolean("app_can_skip_precheck", false),
            boardCanContinue = json.optBoolean("board_can_continue", false)
        )
    }

    private fun parseOutputCapability(json: JSONObject?): OutputCapability? {
        if (json == null) return null
        return OutputCapability(
            boardScreenAvailable = json.optBoolean("board_screen_available", false),
            boardSoundAvailable = json.optBoolean("board_sound_available", false),
            screenOutputRoute = json.optString("screen_output_route", "board"),
            soundOutputRoute = json.optString("sound_output_route", "board"),
            bandAvailable = json.optBoolean("band_available", false),
            lastAttemptedChannels = json.optStringList("last_attempted_channels"),
            lastDeliveredChannels = json.optStringList("last_delivered_channels")
        )
    }

    private fun parseCameraPreviewObject(data: JSONObject?): CameraPreviewImage? {
        if (data == null) return null
        return CameraPreviewImage(
            available = data.optBoolean("available", data.optBoolean("image_available", false)),
            timestampMs = data.optLong("timestamp_ms", data.optLong("image_timestamp_ms", 0L)),
            sequence = data.optInt("sequence", data.optInt("image_sequence", 0)),
            width = data.optInt("width", data.optInt("image_width", 0)),
            height = data.optInt("height", data.optInt("image_height", 0)),
            pixelFormat = data.optString("pixel_format")
                .ifBlank { data.optString("image_pixel_format") }
                .takeIf { it.isNotBlank() },
            encoding = data.optString("encoding")
                .ifBlank { data.optString("image_encoding") }
                .takeIf { it.isNotBlank() },
            source = data.optString("source").takeIf { it.isNotBlank() },
            dataBase64 = data.optString("data_base64").takeIf { it.isNotBlank() },
            quality = data.optDoubleOrNull("quality")
                ?: data.optDoubleOrNull("quality_score")
                ?: data.optJSONObject("quality")?.optDoubleOrNull("quality_score")
        )
    }

    private fun toJsonValue(value: Any): Any {
        return when (value) {
            is Iterable<*> -> JSONArray().apply { value.forEach { put(it) } }
            is Array<*> -> JSONArray().apply { value.forEach { put(it) } }
            else -> value
        }
    }

    private fun JSONObject.optDoubleOrNull(name: String): Double? {
        if (!has(name) || isNull(name)) return null
        return optDouble(name).takeIf { !it.isNaN() && it.isFinite() }
    }

    private fun JSONObject.optIntOrNull(name: String): Int? {
        return if (has(name) && !isNull(name)) optInt(name) else null
    }

    private fun JSONObject.optLongOrNull(name: String): Long? {
        return if (has(name) && !isNull(name)) optLong(name) else null
    }

    private fun JSONObject.optBooleanOrNull(name: String): Boolean? {
        return if (has(name) && !isNull(name)) optBoolean(name) else null
    }

    private fun JSONObject.optBatteryPercent(name: String): Int {
        val value = optIntOrNull(name) ?: return 67
        return value.takeIf { it in 0..100 } ?: 67
    }

    private fun JSONObject.optStringList(name: String): List<String> {
        val array = optJSONArray(name) ?: return emptyList()
        return buildList {
            for (index in 0 until array.length()) {
                array.optString(index).takeIf { it.isNotBlank() }?.let { add(it) }
            }
        }
    }

    private fun JSONObject.optJsonString(name: String): String? {
        if (!has(name) || isNull(name)) return null
        val value = opt(name) ?: return null
        return when (value) {
            is JSONObject, is JSONArray -> value.toString()
            else -> value.toString().takeIf { it.isNotBlank() }
        }
    }

    private fun maskToChannels(mask: Int, allowSound: Boolean): Set<String> {
        return buildSet {
            if (mask and 1 != 0) add("screen")
            if (mask and 2 != 0) add("band")
            if (allowSound && mask and 4 != 0) add("sound")
        }
    }

    private fun channelMask(channels: Set<String>): Int {
        var mask = 0
        if ("screen" in channels) mask = mask or 1
        if ("band" in channels) mask = mask or 2
        if ("sound" in channels) mask = mask or 4
        return mask
    }
}
