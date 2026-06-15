package com.edgefocus.app.domain.repository

import com.edgefocus.app.data.local.entity.Settings
import kotlinx.coroutines.flow.Flow

interface ISettingsRepository {
    fun getSettings(): Flow<Settings?>
    suspend fun getSettingsOnce(): Settings?
    suspend fun updateSettings(settings: Settings)
}
