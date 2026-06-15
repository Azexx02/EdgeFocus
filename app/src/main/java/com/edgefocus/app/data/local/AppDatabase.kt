package com.edgefocus.app.data.local

import androidx.room.Database
import androidx.room.RoomDatabase
import com.edgefocus.app.data.local.dao.AppUsageDao
import com.edgefocus.app.data.local.dao.SessionDao
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.entity.AppUsage
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.local.entity.Settings

@Database(
    entities = [Session::class, Settings::class, AppUsage::class],
    version = 1,
    exportSchema = false
)
abstract class AppDatabase : RoomDatabase() {
    abstract fun sessionDao(): SessionDao
    abstract fun settingsDao(): SettingsDao
    abstract fun appUsageDao(): AppUsageDao
}
