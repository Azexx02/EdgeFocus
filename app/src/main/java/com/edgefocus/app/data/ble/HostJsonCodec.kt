package com.edgefocus.app.data.ble

import com.edgefocus.app.data.ble.model.BusinessAcceptance
import com.edgefocus.app.data.ble.model.ClientHelloResult
import com.edgefocus.app.data.ble.model.EventAckResult
import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.ExternalPowerState
import com.edgefocus.app.data.ble.model.GimbalCommandAcceptance
import com.edgefocus.app.data.ble.model.HostPowerSnapshot
import com.edgefocus.app.data.ble.model.HostSessionPhase
import com.edgefocus.app.data.ble.model.HostSessionSnapshot
import com.edgefocus.app.data.ble.model.HostStateSnapshot
import com.edgefocus.app.data.ble.model.PhoneSyncResult
import com.edgefocus.app.data.ble.model.PhoneUsageState
import com.edgefocus.app.data.ble.model.ReliableEventEnvelope
import com.edgefocus.app.data.ble.model.ReliableEventType
import org.json.JSONArray
import org.json.JSONObject

object HostJsonCodec {
    const val CAP_GIMBAL_ORIGIN = "gimbal_origin_v1"
    const val CAP_GIMBAL_FAULT_CLASS = "gimbal_fault_class_v1"
    const val CAP_GIMBAL_SESSION_CONTROL = "gimbal_session_control_v3"
    const val CAP_GIMBAL_MANUAL_LEASE = "gimbal_manual_lease_v1"
    const val CAP_GIMBAL_ASYNC_RESULT = "gimbal_async_result_v1"

    val clientCapabilities: Set<String> = setOf(
        "event_ack_v1",
        "event_dedupe_v1",
        "phone_heartbeat_v1"
    )

    fun clientHello(requestId: Int): JSONObject = request(
        command = "client_hello",
        requestId = requestId,
        data = JSONObject().put("capabilities", JSONArray(clientCapabilities.toList()))
    )

    fun getCurrentState(requestId: Int): JSONObject = request("get_current_state", requestId)

    fun sessionControl(
        requestId: Int,
        action: String,
        targetDurationMinutes: Int? = null,
        sessionId: String? = null
    ): JSONObject {
        val data = JSONObject().put("action", action)
        if (action == "start") {
            data.put("target_duration_min", (targetDurationMinutes ?: 25).coerceIn(5, 120))
            data.put("source_mode", "live_camera")
        }
        sessionId?.takeIf { it.isNotBlank() }?.let { data.put("session_id", it) }
        return request("session_control", requestId, data)
    }

    fun phoneSync(
        requestId: Int,
        sessionId: String,
        sequence: Long,
        timestampMs: Long,
        isInUse: Boolean,
        usageState: PhoneUsageState? = null
    ): JSONObject {
        require(sessionId.isNotBlank())
        require(sequence > 0)
        val data = JSONObject()
            .put("session_id", sessionId)
            .put("sequence", sequence)
            .put("timestamp_ms", timestampMs)
            .put("is_in_use", isInUse)
        if (isInUse) {
            data.put("usage_state", requireNotNull(usageState).wireValue)
        }
        return request("phone_sync", requestId, data)
    }

    fun eventAck(
        requestId: Int,
        eventId: String,
        sessionId: String,
        stage: EventAckStage
    ): JSONObject {
        require(eventId.isNotBlank())
        require(sessionId.isNotBlank())
        return request(
            command = "event_ack",
            requestId = requestId,
            data = JSONObject()
                .put("event_id", eventId)
                .put("session_id", sessionId)
                .put("stage", stage.wireValue)
        )
    }

    fun gimbalCommand(
        requestId: Int,
        command: String,
        sessionId: String,
        sequence: Long,
        direction: String? = null,
        ownerId: String? = null,
        leaseToken: String? = null,
        fineTune: Boolean? = null,
        confirmed: Boolean? = null
    ): JSONObject {
        require(command in setOf(
            "precheck_skip_auto_adjust",
            "gimbal_nudge",
            "gimbal_center",
            "gimbal_stop",
            "gimbal_recover",
            "precheck_confirm_position"
        ))
        require(sessionId.isNotBlank())
        require(sequence > 0)
        val data = JSONObject()
            .put("session_id", sessionId)
            .put("sequence", sequence)
        if (command != "gimbal_recover") {
            data.put("owner_id", requireNotNull(ownerId).also { require(it.isNotBlank()) })
        }
        if (command == "gimbal_nudge") {
            val normalized = requireNotNull(direction).uppercase()
            require(normalized in setOf("UP", "DOWN", "LEFT", "RIGHT"))
            data.put("direction", normalized)
            data.put("fine_tune", fineTune ?: true)
        }
        return request(command, requestId, data)
    }

    fun captureForwardOrigin(requestId: Int, sequence: Long): JSONObject {
        require(sequence > 0)
        return request(
            command = "gimbal_capture_forward",
            requestId = requestId,
            data = JSONObject()
                .put("sequence", sequence)
                .put("confirm", true)
        )
    }

    fun setPowerProfile(requestId: Int, profile: String): JSONObject {
        val normalized = profile.lowercase()
        require(normalized == "home" || normalized == "go")
        return request(
            "set_power_profile",
            requestId,
            JSONObject().put("power_profile", normalized)
        )
    }

    fun parseClientHello(json: String): ClientHelloResult {
        val data = successfulData(json)
        return ClientHelloResult(
            schemaVersion = data.requiredString("schema_version"),
            reliablePush = data.optBoolean("reliable_push", false),
            phoneFreshForMs = data.optLong("phone_fresh_for_ms", 5_000L),
            serverCapabilities = data.optJSONArray("server_capabilities").toStringSet()
        )
    }

    fun parseState(json: String): HostStateSnapshot {
        val data = successfulData(json)
        val runtime = data.optJSONObject("runtime_snapshot")
        val runtimeSession = runtime?.optJSONObject("session")
        val runtimeDevice = runtime?.optJSONObject("device")
        val outputCapability = data.optJSONObject("output_capability")

        val phase = parsePhase(
            runtimeSession?.nullableString("phase")
                ?: data.nullableString("session_phase")
        )
        val sessionId = runtimeSession?.nullableString("id")
            ?: data.nullableString("session_id")
        val runtimeActive = runtimeSession?.optBoolean("runtime_active")
            ?: (phase != HostSessionPhase.IDLE && phase != HostSessionPhase.UNKNOWN)
        val formalActive = runtimeSession?.optBoolean("active")
            ?: data.optBoolean("active", false)
        val elapsedMs = runtimeSession.nullableLong("elapsed_ms")
            ?: data.nullableLong("elapsed_ms")
        val remainingMs = runtimeSession.nullableLong("remaining_ms")
            ?: data.nullableLong("remaining_ms")
        val targetDurationMs = data.nullableLong("target_duration_ms")
        val phoneSequence = runtime?.optJSONObject("phone").nullableLong("sequence")

        val powerState = when (data.nullableString("external_power_state")?.uppercase()) {
            "PRESENT" -> ExternalPowerState.PRESENT
            "ABSENT" -> ExternalPowerState.ABSENT
            else -> ExternalPowerState.UNKNOWN
        }
        val present = if (data.has("external_power_present") && !data.isNull("external_power_present")) {
            data.optBoolean("external_power_present")
        } else {
            null
        }

        return HostStateSnapshot(
            session = HostSessionSnapshot(
                sessionId = sessionId,
                phase = phase,
                runtimeActive = runtimeActive,
                formalActive = formalActive,
                elapsedMs = elapsedMs,
                remainingMs = remainingMs,
                targetDurationMs = targetDurationMs,
                phoneSequence = phoneSequence
            ),
            power = HostPowerSnapshot(
                state = powerState,
                present = present,
                sampleAgeMs = data.nullableLong("external_power_sample_age_ms"),
                profile = data.nullableString("power_profile")?.lowercase(),
                profileSource = data.nullableString("power_profile_source"),
                manualOverrideActive = data.optBoolean("manual_override_active", false)
            ),
            braceletConnected = outputCapability?.nullableBoolean("band_available")
                ?: data.nullableBoolean("bracelet_connected")
                ?: runtimeDevice?.nullableBoolean("bracelet_connected"),
            rawJson = json
        )
    }

    fun parseEventAck(json: String): EventAckResult {
        val data = successfulData(json)
        return EventAckResult(
            status = data.requiredString("ack_status"),
            eventId = data.requiredString("event_id"),
            stage = data.requiredString("stage")
        )
    }

    fun parsePhoneSync(json: String): PhoneSyncResult {
        val data = successfulData(json)
        return PhoneSyncResult(
            accepted = data.optBoolean("accepted", false),
            lastAcceptedSequence = data.nullableLong("last_accepted_sequence"),
            freshForMs = data.nullableLong("fresh_for_ms"),
            errorCode = data.nullableString("error_code")
        )
    }

    fun parseBusinessAcceptance(json: String): BusinessAcceptance {
        val root = JSONObject(json)
        if (root.optString("status") == "error") {
            return BusinessAcceptance(
                accepted = false,
                error = root.optString("error", "host_error"),
                errorCode = root.nullableString("error_code"),
                retryable = root.optBoolean("retryable", false)
            )
        }
        val data = root.optJSONObject("data") ?: JSONObject()
        return BusinessAcceptance(
            accepted = data.optBoolean("accepted", data.optBoolean("success", false)),
            operation = data.nullableString("operation"),
            error = data.nullableString("message"),
            errorCode = data.nullableString("error_code"),
            retryable = data.optBoolean("retryable", false)
        )
    }

    fun parseGimbalAcceptance(json: String): GimbalCommandAcceptance {
        val root = JSONObject(json)
        if (root.optString("status").equals("error", ignoreCase = true)) {
            return GimbalCommandAcceptance(
                accepted = false,
                errorCode = root.nullableString("error_code"),
                retryable = root.optBoolean("retryable", false),
                error = root.nullableString("error") ?: root.nullableString("message")
            )
        }
        val data = root.optJSONObject("data") ?: JSONObject()
        return GimbalCommandAcceptance(
            accepted = data.optBoolean("accepted", data.optBoolean("success", true)),
            operation = data.nullableString("operation"),
            sessionId = data.nullableString("session_id"),
            sequence = data.nullableLong("sequence"),
            motionGeneration = data.nullableLong("motion_generation"),
            state = data.nullableString("state"),
            settled = data.nullableBoolean("settled"),
            manualLease = data.nullableBoolean("manual_lease"),
            ownerId = data.nullableString("owner_id") ?: data.nullableString("lease_owner_id"),
            leaseToken = data.nullableString("lease_token"),
            leaseEpoch = data.nullableLong("lease_epoch"),
            leaseExpiresAtMs = data.nullableLong("lease_expires_at"),
            firmwareAccepted = data.nullableBoolean("firmware_accepted"),
            ackedThroughSequence = data.nullableLong("acked_through_sequence"),
            currentYawDeg = data.nullableDouble("current_yaw_deg"),
            currentPitchDeg = data.nullableDouble("current_pitch_deg"),
            targetYawDeg = data.nullableDouble("target_yaw_deg"),
            targetPitchDeg = data.nullableDouble("target_pitch_deg"),
            positionConfirmed = data.nullableBoolean("position_confirmed"),
            originChanged = data.nullableBoolean("origin_changed"),
            originEpoch = data.nullableLong("origin_epoch"),
            errorCode = data.nullableString("error_code"),
            retryable = data.optBoolean("retryable", false),
            error = data.nullableString("message")
        )
    }

    fun parseReliableEvent(json: String): ReliableEventEnvelope? {
        val root = JSONObject(json)
        val type = when (root.optString("cmd")) {
            ReliableEventType.ALERT.command -> ReliableEventType.ALERT
            ReliableEventType.SESSION_END.command -> ReliableEventType.SESSION_END
            ReliableEventType.GIMBAL_COMMAND_RESULT.command -> ReliableEventType.GIMBAL_COMMAND_RESULT
            else -> return null
        }
        val data = root.optJSONObject("data") ?: return null
        val eventId = data.nullableString("event_id") ?: return null
        val sessionId = data.nullableString("session_id") ?: return null
        val ttlMs = data.nullableLong("ttl_ms") ?: return null
        val remainingTtl = data.nullableLong("remaining_ttl_ms") ?: ttlMs
        return ReliableEventEnvelope(
            type = type,
            eventId = eventId,
            sessionId = sessionId,
            createdAtMs = data.nullableLong("created_at_ms"),
            ttlMs = ttlMs.coerceAtLeast(0L),
            remainingTtlMs = remainingTtl.coerceIn(0L, ttlMs.coerceAtLeast(0L)),
            payloadJson = json
        )
    }

    private fun request(
        command: String,
        requestId: Int,
        data: JSONObject = JSONObject()
    ): JSONObject = JSONObject()
        .put("cmd", command)
        .put("id", requestId)
        .put("data", data)

    private fun successfulData(json: String): JSONObject {
        val root = JSONObject(json)
        require(root.optString("status") == "success") {
            root.optString("error", "Host returned non-success response")
        }
        return root.optJSONObject("data") ?: error("Missing response data")
    }

    private fun parsePhase(value: String?): HostSessionPhase = when (value?.uppercase()) {
        "IDLE" -> HostSessionPhase.IDLE
        "PRECHECK" -> HostSessionPhase.PRECHECK
        "FOCUSING", "RUNNING" -> HostSessionPhase.RUNNING
        "PAUSED" -> HostSessionPhase.PAUSED
        else -> HostSessionPhase.UNKNOWN
    }

    private fun JSONObject?.nullableLong(key: String): Long? {
        if (this == null || !has(key) || isNull(key)) return null
        return optLong(key)
    }

    private fun JSONObject?.nullableString(key: String): String? {
        if (this == null || !has(key) || isNull(key)) return null
        return optString(key).takeIf { it.isNotBlank() }
    }

    private fun JSONObject.nullableBoolean(key: String): Boolean? {
        if (!has(key) || isNull(key)) return null
        return optBoolean(key)
    }

    private fun JSONObject.nullableDouble(key: String): Double? {
        if (!has(key) || isNull(key)) return null
        return optDouble(key).takeIf { it.isFinite() && !it.isNaN() }
    }

    private fun JSONObject.requiredString(key: String): String =
        nullableString(key) ?: error("Missing $key")

    private fun JSONArray?.toStringSet(): Set<String> {
        if (this == null) return emptySet()
        return buildSet {
            for (index in 0 until length()) {
                optString(index).takeIf { it.isNotBlank() }?.let(::add)
            }
        }
    }
}
