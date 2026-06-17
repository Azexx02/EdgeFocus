package com.edgefocus.app.domain.repository

import com.edgefocus.app.data.local.entity.Session
import kotlinx.coroutines.flow.Flow

interface ISessionRepository {
    fun getAllSessions(): Flow<List<Session>>
    fun getSessionsAfter(timestamp: Long): Flow<List<Session>>
    fun getSessionById(id: String): Flow<Session?>
    suspend fun insertSession(session: Session)
    suspend fun updateSession(session: Session)
    suspend fun deleteSession(id: String)
    suspend fun updateFeedback(id: String, feedback: String)
    suspend fun getUnsyncedSessions(): List<Session>
    suspend fun markSynced(id: String)
}
