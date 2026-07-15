package com.edgefocus.app.data.local.entity

import androidx.room.Entity
import androidx.room.PrimaryKey

@Entity(tableName = "settings")
data class Settings(
    @PrimaryKey val id: Int = 1,
    val pomodoroDuration: Int = 25,
    val useEdgeFocusBoard: Boolean = true,
    val edgeFocusMacAddress: String = "",
    val screenSaverEnabled: Boolean = true,
    val screenSaverTimeoutSec: Int = 120,
    val appScreenAlertsEnabled: Boolean = true,
    val appSoundAlertsEnabled: Boolean = true
)
