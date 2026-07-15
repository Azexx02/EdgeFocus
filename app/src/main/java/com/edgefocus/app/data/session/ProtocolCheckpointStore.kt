package com.edgefocus.app.data.session

import androidx.room.withTransaction
import com.edgefocus.app.data.local.AppDatabase
import com.edgefocus.app.data.local.entity.ProtocolCheckpointEntity
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class ProtocolCheckpointStore @Inject constructor(
    private val database: AppDatabase
) {
    suspend fun allocatePhoneSequence(
        sessionId: String,
        runtimeSequence: Long?,
        lastAcceptedSequence: Long?,
        nowMs: Long = System.currentTimeMillis()
    ): Long = database.withTransaction {
        val dao = database.protocolCheckpointDao()
        val current = dao.get(sessionId)
        val next = SequencePolicy.next(current?.phoneSequence, runtimeSequence, lastAcceptedSequence)
        dao.put(
            ProtocolCheckpointEntity(
                sessionId = sessionId,
                phoneSequence = next,
                gimbalSequence = current?.gimbalSequence ?: 0L,
                updatedAtMs = nowMs
            )
        )
        next
    }

    suspend fun allocateGimbalSequence(
        sessionId: String,
        hostSequence: Long? = null,
        nowMs: Long = System.currentTimeMillis()
    ): Long = database.withTransaction {
        val dao = database.protocolCheckpointDao()
        val current = dao.get(sessionId)
        val next = SequencePolicy.next(current?.gimbalSequence, hostSequence, null)
        dao.put(
            ProtocolCheckpointEntity(
                sessionId = sessionId,
                phoneSequence = current?.phoneSequence ?: 0L,
                gimbalSequence = next,
                updatedAtMs = nowMs
            )
        )
        next
    }

    suspend fun retainOnly(activeSessionId: String) {
        database.protocolCheckpointDao().deleteExcept(activeSessionId)
    }
}
