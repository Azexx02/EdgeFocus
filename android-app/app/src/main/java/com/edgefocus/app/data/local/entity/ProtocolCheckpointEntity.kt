package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "protocol_checkpoints")
data class ProtocolCheckpointEntity(
    @PrimaryKey val sessionId: String,
    val phoneSequence: Long = 0,
    val gimbalSequence: Long = 0,
    val updatedAtMs: Long
)
