package com.edgefocus.app.data.repository

import com.edgefocus.app.data.local.dao.SessionDao
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.domain.repository.ISessionRepository
import kotlinx.coroutines.flow.Flow
import javax.inject.Inject

class SessionRepository @Inject constructor(
    private val sessionDao: SessionDao
) : ISessionRepository {

    override fun getAllSessions(): Flow<List<Session>> = sessionDao.getAllSessions()

    override fun getSessionsAfter(timestamp: Long): Flow<List<Session>> =
        sessionDao.getSessionsAfter(timestamp)

    override fun getSessionById(id: String): Flow<Session?> = sessionDao.getSessionById(id)

    override suspend fun insertSession(session: Session) = sessionDao.insertSession(session)

    override suspend fun updateSession(session: Session) = sessionDao.updateSession(session)

    override suspend fun deleteSession(id: String) = sessionDao.deleteSession(id)
}
