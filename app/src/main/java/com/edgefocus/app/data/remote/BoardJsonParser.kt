package com.edgefocus.app.data.remote

import org.json.JSONArray
import org.json.JSONObject

enum class BoardSessionPhase(val wireValue: String) {
    IDLE("idle"),
    PRECHECK("precheck"),
    FOCUSING("focusing"),
    PAUSED("paused");

    companion object {
        fun fromWire(value: String?): BoardSessionPhase {
            return entries.firstOrNull { it.wireValue == value } ?: IDLE
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
    val elapsedMs: Long = 0L,
    val remainingMs: Long = 0L,
    val targetDurationMs: Long = 0L,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val calibrationCompleted: Boolean = false,
    val sessionPhase: BoardSessionPhase = BoardSessionPhase.IDLE,
    val precheck: BoardPrecheck? = null,
    val outputCapability: OutputCapability? = null,
    val reminder: String? = null,
    val precheckGuidance: String? = null,
    val cameraPreview: CameraPreviewImage? = null,
    val focusReason: String? = null,
    val behaviorContext: String? = null
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
    val angleY: Double? = null
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
    val lastError: String? = null,
    val enabled: Boolean = false
)

data class PushAlert(
    val level: Int = 0,
    val message: String = "",
    val phoneActions: List<String> = emptyList(),
    val soundVolume: Float? = null,
    val focusState: Int? = null,
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
    val awayTimeoutMin: Int = 15,
    val defaultFocusDurationMin: Int = 25,
    val vibrationStrength: Float = 0.5f,
    val ledBrightness: Int = 75,
    val reminderSensitivity: Float = 0.5f,
    val soundVolume: Float = 0.7f,
    val normalChannels: Set<String> = setOf("screen", "band", "sound"),
    val silentChannels: Set<String> = setOf("screen", "band"),
    val screenOutputRoute: String = "board",
    val soundOutputRoute: String = "board",
    val gimbalEnabled: Boolean = true
)

object BoardJsonParser {

    fun buildCommand(command: String): JSONObject {
        return JSONObject().apply {
            put("cmd", command)
        }
    }

    fun buildSessionControl(action: String, targetDurationMin: Int? = null): JSONObject {
        val data = JSONObject().apply {
            put("action", action)
            targetDurationMin?.let { put("target_duration_min", it) }
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

    fun parseSessionState(jsonString: String): BoardSessionState {
        val root = JSONObject(jsonString)
        val data = root.optJSONObject("data") ?: JSONObject()
        return parseSessionState(data)
    }

    fun parseSessionState(data: JSONObject): BoardSessionState {
        val phase = BoardSessionPhase.fromWire(
            data.optString("session_phase").takeIf { it.isNotBlank() }
        )
        return BoardSessionState(
            success = data.optBoolean("success", true),
            alreadyActive = data.optBoolean("already_active", false),
            sessionId = data.optString("session_id").takeIf { it.isNotBlank() },
            active = data.optBoolean("active", false),
            paused = data.optBoolean("paused", false),
            focusState = data.optInt("focus_state", 0),
            elapsedMs = data.optLong("elapsed_ms", 0L),
            remainingMs = data.optLong("remaining_ms", 0L),
            targetDurationMs = data.optLong("target_duration_ms", 0L),
            distractionCount = data.optInt("distraction_count", 0),
            reminderCount = data.optInt("reminder_count", 0),
            phoneDistractionCount = data.optInt("phone_distraction_count", 0),
            calibrationCompleted = data.optBoolean("calibration_completed", false),
            sessionPhase = phase,
            precheck = parsePrecheck(data.optJSONObject("precheck")),
            outputCapability = parseOutputCapability(data.optJSONObject("output_capability")),
            reminder = data.optJsonString("reminder"),
            precheckGuidance = data.optJsonString("precheck_guidance")
                ?: data.optJSONObject("precheck")?.optJSONObject("camera")?.optJsonString("guidance"),
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
        return GimbalStatus(
            connected = data.optBoolean("connected", false),
            online = data.optBoolean("online", false),
            safe = data.optBoolean("safe", false),
            xhome = data.optBoolean("xhome", false),
            yhome = data.optBoolean("yhome", false),
            calibrated = data.optBoolean("calibrated", false),
            angleX = data.optDoubleOrNull("angle_x"),
            angleY = data.optDoubleOrNull("angle_y"),
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
            focusState = data.optIntOrNull("focus_state"),
            reminderCount = data.optIntOrNull("reminder_count"),
            riskScore = data.optDoubleOrNull("risk_score"),
            phoneDistractionLevel = data.optIntOrNull("phone_distraction_level"),
            visionSignal = data.optJsonString("vision_signal"),
            phoneSignal = data.optJsonString("phone_signal"),
            reason = data.optJsonString("reason"),
            outputCapability = parseOutputCapability(data.optJSONObject("output_capability"))
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
        return BoardPreferences(
            systemMode = data.optInt("system_mode", defaults.systemMode),
            awayTimeoutMin = data.optInt("away_timeout_min", defaults.awayTimeoutMin),
            defaultFocusDurationMin = data.optInt(
                "default_focus_duration_min",
                defaults.defaultFocusDurationMin
            ),
            vibrationStrength = data.optDouble("vibration_strength", defaults.vibrationStrength.toDouble())
                .toFloat(),
            ledBrightness = data.optInt("led_brightness", defaults.ledBrightness),
            reminderSensitivity = data.optDouble(
                "reminder_sensitivity",
                defaults.reminderSensitivity.toDouble()
            ).toFloat(),
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

    private fun parsePrecheck(json: JSONObject?): BoardPrecheck? {
        if (json == null) return null
        return BoardPrecheck(
            camera = json.optJSONObject("camera")?.let {
                PrecheckCamera(
                    required = it.optBoolean("required", false),
                    guidance = it.optJSONObject("guidance")?.toString()
                )
            },
            gimbal = json.optJSONObject("gimbal")?.let {
                PrecheckGimbal(
                    enabled = it.optBoolean("enabled", false),
                    online = it.optBoolean("online", false),
                    safe = it.optBoolean("safe", false),
                    angleX = it.optDoubleOrNull("angle_x"),
                    angleY = it.optDoubleOrNull("angle_y")
                )
            }
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
