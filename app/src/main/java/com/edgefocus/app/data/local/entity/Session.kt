package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "sessions")
data class Session(
    @PrimaryKey val id: String,
    val startTime: Long,
    val endTime: Long?,
    val duration: Int,
    val focusDuration: Int,
    val distractionCount: Int,
    val reminderCount: Int,
    val feedback: String?,
    val boardSessionId: String?
)
