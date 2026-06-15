package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "settings")
data class Settings(
    @PrimaryKey val id: Int = 1,
    val pomodoroDuration: Int = 25,
    val shortBreakDuration: Int = 5,
    val longBreakDuration: Int = 15,
    val sessionsBeforeLongBreak: Int = 4,
    val useEdgeFocusBoard: Boolean = true,
    val autoRotateNFC: Boolean = true,
    val notificationOnExit: Boolean = true
)
