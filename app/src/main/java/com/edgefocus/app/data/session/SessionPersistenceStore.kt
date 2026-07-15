package com.edgefocus.app.data.session

import androidx.room.withTransaction
import com.edgefocus.app.data.local.AppDatabase
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.local.entity.SyncOutboxEntity
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class SessionPersistenceStore @Inject constructor(
    private val database: AppDatabase
) {
    suspend fun saveLocalCompletion(
        session: Session,
        uploadPayloadJson: String,
        nowMs: Long = System.currentTimeMillis()
    ) = database.withTransaction {
        val local = session.copy(source = "LOCAL", synced = false)
        database.sessionDao().insertSession(local)
        database.syncOutboxDao().put(
            SyncOutboxEntity(
                sessionId = local.id,
                payloadJson = uploadPayloadJson,
                createdAtMs = nowMs,
                nextAttemptAtMs = nowMs
            )
        )
    }

    suspend fun markUploaded(sessionId: String, boardSessionId: String) = database.withTransaction {
        database.sessionDao().markSynced(sessionId, boardSessionId)
        database.syncOutboxDao().deleteForSession(sessionId)
    }

    suspend fun saveEdgeFocusCompletion(session: Session) {
        database.sessionDao().insertSession(session.copy(source = "EDGEFOCUS"))
    }

    suspend fun dueUploads(nowMs: Long = System.currentTimeMillis()): List<SyncOutboxEntity> =
        database.syncOutboxDao().due(nowMs)

    suspend fun recordUploadFailure(
        sessionId: String,
        nextAttemptAtMs: Long,
        error: String
    ) = database.syncOutboxDao().recordFailure(sessionId, nextAttemptAtMs, error)
}
