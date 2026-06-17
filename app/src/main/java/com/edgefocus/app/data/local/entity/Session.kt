package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.Index
import androidx.room.PrimaryKey

@Entity(tableName = "sessions", indices = [Index("startTime")])
data class Session(
    @PrimaryKey val id: String,
    val startTime: Long,
    val endTime: Long?,
    val duration: Int,
    val focusDuration: Int,
    val distractionCount: Int,
    val reminderCount: Int,
    val phoneDistractionCount: Int = 0,
    val feedback: String? = null,
    val boardSessionId: String? = null,
    val source: String = "LOCAL",
    val synced: Boolean = false
)
