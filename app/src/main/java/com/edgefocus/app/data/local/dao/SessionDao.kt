package com.edgefocus.app.data.local.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import androidx.room.Update
import com.edgefocus.app.data.local.entity.Session
import kotlinx.coroutines.flow.Flow

@Dao
interface SessionDao {
    @Query("SELECT * FROM sessions ORDER BY startTime DESC")
    fun getAllSessions(): Flow<List<Session>>

    @Query("SELECT * FROM sessions WHERE id = :id")
    fun getSessionById(id: String): Flow<Session?>

    @Query("SELECT * FROM sessions WHERE id = :id")
    suspend fun getSessionByIdOnce(id: String): Session?

    @Query("SELECT * FROM sessions WHERE startTime >= :timestamp ORDER BY startTime DESC")
    fun getSessionsAfter(timestamp: Long): Flow<List<Session>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insertSession(session: Session)

    @Update
    suspend fun updateSession(session: Session)

    @Query("DELETE FROM sessions WHERE id = :id")
    suspend fun deleteSession(id: String)
}
