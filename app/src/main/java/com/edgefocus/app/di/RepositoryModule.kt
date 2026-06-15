package com.edgefocus.app.di

import com.edgefocus.app.data.repository.SettingsRepository
import com.edgefocus.app.domain.repository.ISettingsRepository
import dagger.Binds
import dagger.Module
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
abstract class RepositoryModule {
    @Binds
    @Singleton
    abstract fun bindSettingsRepository(
        settingsRepository: SettingsRepository
    ): ISettingsRepository
}
