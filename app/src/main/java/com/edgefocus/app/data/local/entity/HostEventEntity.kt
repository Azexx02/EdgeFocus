package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(
    tableName = "host_event_inbox",
    indices = [Index("sessionId"), Index("dispatchState"), Index("localDeadlineElapsedMs")]
)
data class HostEventEntity(
    @PrimaryKey val eventId: String,
    val sessionId: String,
    val eventType: String,
    val payloadJson: String,
    val hostCreatedAtMs: Long?,
    val ttlMs: Long,
    val receivedElapsedMs: Long,
    val localDeadlineElapsedMs: Long,
    val receivedBootCount: Int,
    val receivedWallTimeMs: Long,
    val dispatchState: String = DispatchState.PENDING,
    val localAckStage: Int = 0,
    val confirmedAckStage: Int = 0,
    val lastAckStatus: String? = null,
    val lastError: String? = null
) {
    object DispatchState {
        const val PENDING = "PENDING"
        const val CLAIMED = "CLAIMED"
        const val EFFECT_STARTED = "EFFECT_STARTED"
        const val PRESENTED = "PRESENTED"
        const val WAITING_REPLAY = "WAITING_REPLAY"
        const val DELIVERY_UNCERTAIN = "DELIVERY_UNCERTAIN"
        const val EXPIRED = "EXPIRED"
        const val COMPLETED = "COMPLETED"
    }
}
