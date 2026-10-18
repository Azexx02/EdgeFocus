package com.edgefocus.app.data.local.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.edgefocus.app.data.local.entity.ProtocolCheckpointEntity

@Dao
interface ProtocolCheckpointDao {
    @Query("SELECT * FROM protocol_checkpoints WHERE sessionId = :sessionId")
    suspend fun get(sessionId: String): ProtocolCheckpointEntity?

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun put(checkpoint: ProtocolCheckpointEntity)

    @Query("DELETE FROM protocol_checkpoints WHERE sessionId != :activeSessionId")
    suspend fun deleteExcept(activeSessionId: String)
}
