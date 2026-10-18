package com.edgefocus.app.di

import android.content.Context
import androidx.room.Room
import com.edgefocus.app.data.local.AppDatabase
import com.edgefocus.app.data.local.MIGRATION_2_3
import com.edgefocus.app.data.local.MIGRATION_3_4
import com.edgefocus.app.data.local.MIGRATION_4_5
import com.edgefocus.app.data.local.MIGRATION_5_6
import com.edgefocus.app.data.local.MIGRATION_6_7
import com.edgefocus.app.data.local.dao.AppUsageDao
import com.edgefocus.app.data.local.dao.SessionDao
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.dao.HostEventDao
import com.edgefocus.app.data.local.dao.SyncOutboxDao
import com.edgefocus.app.data.local.dao.ProtocolCheckpointDao
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object DatabaseModule {
    @Provides
    @Singleton
    fun provideAppDatabase(@ApplicationContext context: Context): AppDatabase {
        return Room.databaseBuilder(
            context,
            AppDatabase::class.java,
            "edgefocus_db"
        )
            .addMigrations(
                MIGRATION_2_3,
                MIGRATION_3_4,
                MIGRATION_4_5,
                MIGRATION_5_6,
                MIGRATION_6_7
            )
            .fallbackToDestructiveMigration()
            .build()
    }

    @Provides
    fun provideSessionDao(database: AppDatabase): SessionDao {
        return database.sessionDao()
    }

    @Provides
    fun provideSettingsDao(database: AppDatabase): SettingsDao {
        return database.settingsDao()
    }

    @Provides
    fun provideAppUsageDao(database: AppDatabase): AppUsageDao {
        return database.appUsageDao()
    }

    @Provides
    fun provideHostEventDao(database: AppDatabase): HostEventDao = database.hostEventDao()

    @Provides
    fun provideSyncOutboxDao(database: AppDatabase): SyncOutboxDao = database.syncOutboxDao()

    @Provides
    fun provideProtocolCheckpointDao(database: AppDatabase): ProtocolCheckpointDao =
        database.protocolCheckpointDao()
}
