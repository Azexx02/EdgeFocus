package com.edgefocus.app.data.local.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.edgefocus.app.data.local.entity.SyncOutboxEntity

@Dao
interface SyncOutboxDao {
    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun put(item: SyncOutboxEntity): Long

    @Query("SELECT * FROM sync_outbox WHERE nextAttemptAtMs <= :nowMs ORDER BY createdAtMs ASC")
    suspend fun due(nowMs: Long): List<SyncOutboxEntity>

    @Query("SELECT * FROM sync_outbox WHERE sessionId = :sessionId LIMIT 1")
    suspend fun getForSession(sessionId: String): SyncOutboxEntity?

    @Query(
        """
        UPDATE sync_outbox
        SET attemptCount = attemptCount + 1,
            nextAttemptAtMs = :nextAttemptAtMs,
            lastError = :error
        WHERE sessionId = :sessionId
        """
    )
    suspend fun recordFailure(sessionId: String, nextAttemptAtMs: Long, error: String)

    @Query("DELETE FROM sync_outbox WHERE sessionId = :sessionId")
    suspend fun deleteForSession(sessionId: String)
}
