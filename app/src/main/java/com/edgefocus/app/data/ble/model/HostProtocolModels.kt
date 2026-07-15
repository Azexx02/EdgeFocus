package com.edgefocus.app.data.ble.model

enum class HostConnectionPhase {
    DISCONNECTED,
    CONNECTING,
    SUBSCRIBED,
    NEGOTIATING,
    READY,
    DEGRADED
}

enum class HostSessionPhase {
    UNKNOWN,
    IDLE,
    PRECHECK,
    RUNNING,
    PAUSED
}

enum class ExternalPowerState {
    PRESENT,
    ABSENT,
    UNKNOWN
}

enum class PhoneUsageState(val wireValue: String) {
    LEARNING("learning"),
    PLAYING("playing"),
    USING("using")
}

enum class EventAckStage(val wireValue: String) {
    RECEIVED("received"),
    PRESENTED("presented"),
    USER_ACK("user_ack")
}

enum class ReliableEventType(val command: String) {
    ALERT("push_alert"),
    SESSION_END("push_session_end")
}

data class ClientHelloResult(
    val schemaVersion: String,
    val reliablePush: Boolean,
    val phoneFreshForMs: Long,
    val serverCapabilities: Set<String>
) {
    fun supports(capability: String): Boolean = capability in serverCapabilities
}

data class HostSessionSnapshot(
    val sessionId: String?,
    val phase: HostSessionPhase,
    val runtimeActive: Boolean,
    val formalActive: Boolean,
    val elapsedMs: Long?,
    val remainingMs: Long?,
    val targetDurationMs: Long?,
    val phoneSequence: Long?,
    val stale: Boolean = false
) {
    val isApplicable: Boolean
        get() = phase != HostSessionPhase.UNKNOWN &&
            (phase == HostSessionPhase.IDLE || !sessionId.isNullOrBlank())
}

data class HostPowerSnapshot(
    val state: ExternalPowerState,
    val present: Boolean?,
    val sampleAgeMs: Long?,
    val profile: String?,
    val profileSource: String?,
    val manualOverrideActive: Boolean
)

data class HostStateSnapshot(
    val session: HostSessionSnapshot,
    val power: HostPowerSnapshot,
    val braceletConnected: Boolean?,
    val rawJson: String
)

data class EventAckResult(
    val status: String,
    val eventId: String,
    val stage: String
) {
    val isSuccessful: Boolean
        get() = status == "accepted" || status == "duplicate"
}

data class PhoneSyncResult(
    val accepted: Boolean,
    val lastAcceptedSequence: Long?,
    val freshForMs: Long?,
    val errorCode: String?
)

data class ReliableEventEnvelope(
    val type: ReliableEventType,
    val eventId: String,
    val sessionId: String,
    val createdAtMs: Long?,
    val ttlMs: Long,
    val remainingTtlMs: Long,
    val payloadJson: String
)

data class BusinessAcceptance(
    val accepted: Boolean,
    val operation: String? = null,
    val error: String? = null
)

data class HostConnectionSnapshot(
    val phase: HostConnectionPhase = HostConnectionPhase.DISCONNECTED,
    val connectionEpoch: Long = 0,
    val hello: ClientHelloResult? = null,
    val hostState: HostStateSnapshot? = null,
    val error: String? = null
) {
    val isReady: Boolean get() = phase == HostConnectionPhase.READY
}
