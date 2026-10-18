package com.edgefocus.app.data.local

import androidx.room.Database
import androidx.room.RoomDatabase
import com.edgefocus.app.data.local.dao.AppUsageDao
import com.edgefocus.app.data.local.dao.SessionDao
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.dao.HostEventDao
import com.edgefocus.app.data.local.dao.SyncOutboxDao
import com.edgefocus.app.data.local.dao.ProtocolCheckpointDao
import com.edgefocus.app.data.local.entity.AppUsage
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.data.local.entity.HostEventEntity
import com.edgefocus.app.data.local.entity.SyncOutboxEntity
import com.edgefocus.app.data.local.entity.ProtocolCheckpointEntity

@Database(
    entities = [
        Session::class,
        Settings::class,
        AppUsage::class,
        HostEventEntity::class,
        SyncOutboxEntity::class,
        ProtocolCheckpointEntity::class
    ],
    version = 8,
    exportSchema = true
)
abstract class AppDatabase : RoomDatabase() {
    abstract fun sessionDao(): SessionDao
    abstract fun settingsDao(): SettingsDao
    abstract fun appUsageDao(): AppUsageDao
    abstract fun hostEventDao(): HostEventDao
    abstract fun syncOutboxDao(): SyncOutboxDao
    abstract fun protocolCheckpointDao(): ProtocolCheckpointDao
}
