package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(
    tableName = "sync_outbox",
    indices = [Index(value = ["sessionId"], unique = true), Index("nextAttemptAtMs")]
)
data class SyncOutboxEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val sessionId: String,
    val payloadJson: String,
    val createdAtMs: Long,
    val attemptCount: Int = 0,
    val nextAttemptAtMs: Long = createdAtMs,
    val lastError: String? = null
)
