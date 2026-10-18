package com.edgefocus.app.data.repository

import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.domain.repository.ISettingsRepository
import kotlinx.coroutines.flow.Flow
import javax.inject.Inject

class SettingsRepository @Inject constructor(
    private val settingsDao: SettingsDao
) : ISettingsRepository {

    override fun getSettings(): Flow<Settings?> = settingsDao.getSettings()

    override suspend fun getSettingsOnce(): Settings? = settingsDao.getSettingsOnce()

    override suspend fun updateSettings(settings: Settings) = settingsDao.updateSettings(settings)
}
