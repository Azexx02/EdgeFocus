package com.edgefocus.app.data.local.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.edgefocus.app.data.local.entity.AppUsage
import kotlinx.coroutines.flow.Flow

@Dao
interface AppUsageDao {
    @Query("SELECT * FROM app_usage WHERE sessionId = :sessionId")
    fun getAppUsageBySession(sessionId: String): Flow<List<AppUsage>>

    @Query("SELECT * FROM app_usage WHERE sessionId = :sessionId")
    suspend fun getAppUsageBySessionOnce(sessionId: String): List<AppUsage>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertAppUsage(appUsage: AppUsage)

    @Query("DELETE FROM app_usage WHERE sessionId = :sessionId")
    suspend fun deleteAppUsageBySession(sessionId: String)
}
