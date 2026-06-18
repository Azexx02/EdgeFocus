package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.repository.ISessionRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.util.Calendar
import javax.inject.Inject

data class DashboardUiState(
    val sessions: List<Session> = emptyList(),
    val todayFocusSeconds: Int = 0,
    val todaySessionCount: Int = 0,
    val weekFocusSeconds: Int = 0
)

@HiltViewModel
class DashboardViewModel @Inject constructor(
    application: Application,
    private val sessionRepository: ISessionRepository,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    private val pendingFeedbackPrefs = application.getSharedPreferences(
        "pending_session_feedback",
        Context.MODE_PRIVATE
    )

    private val _uiState = MutableStateFlow(DashboardUiState())
    val uiState: StateFlow<DashboardUiState> = _uiState.asStateFlow()

    init {
        loadSessions()
        viewModelScope.launch {
            bleManager.connectionState.collect { state ->
                if (state == ConnectionState.CONNECTED && bleManager.isConnected()) {
                    syncPendingFeedback()
                    syncBoardHistory()
                }
            }
        }
    }

    private fun loadSessions() {
        viewModelScope.launch {
            sessionRepository.getAllSessions().collect { sessions ->
                val todayStart = getTodayStart()
                val weekStart = getWeekStart()

                val todaySessions = sessions.filter { it.startTime >= todayStart }
                val weekSessions = sessions.filter { it.startTime >= weekStart }

                _uiState.update {
                    it.copy(
                        sessions = sessions,
                        todayFocusSeconds = todaySessions.sumOf { s -> s.focusDuration },
                        todaySessionCount = todaySessions.size,
                        weekFocusSeconds = weekSessions.sumOf { s -> s.focusDuration }
                    )
                }
            }
        }
    }

    private fun getTodayStart(): Long {
        val calendar = Calendar.getInstance()
        calendar.set(Calendar.HOUR_OF_DAY, 0)
        calendar.set(Calendar.MINUTE, 0)
        calendar.set(Calendar.SECOND, 0)
        calendar.set(Calendar.MILLISECOND, 0)
        return calendar.timeInMillis
    }

    private fun getWeekStart(): Long {
        val calendar = Calendar.getInstance()
        calendar.set(Calendar.DAY_OF_WEEK, calendar.firstDayOfWeek)
        calendar.set(Calendar.HOUR_OF_DAY, 0)
        calendar.set(Calendar.MINUTE, 0)
        calendar.set(Calendar.SECOND, 0)
        calendar.set(Calendar.MILLISECOND, 0)
        return calendar.timeInMillis
    }

    fun updateSessionFeedback(sessionId: String, feedback: String) {
        if (feedback !in VALID_FEEDBACK_VALUES) return
        viewModelScope.launch {
            sessionRepository.updateFeedback(sessionId, feedback)
            val session = _uiState.value.sessions.firstOrNull { it.id == sessionId }
            val boardSessionId = session?.boardSessionId
            if (session?.source == "EDGEFOCUS" && !boardSessionId.isNullOrBlank()) {
                queueFeedback(boardSessionId, feedback)
                syncPendingFeedback()
            }
        }
    }

    private fun queueFeedback(sessionId: String, feedback: String) {
        pendingFeedbackPrefs.edit().putString(sessionId, feedback).apply()
    }

    private suspend fun syncPendingFeedback() {
        if (!bleManager.isConnected()) return

        pendingFeedbackPrefs.all.forEach { (sessionId, value) ->
            val feedback = value as? String ?: return@forEach
            bleManager.sessionFeedback(sessionId, feedback).onSuccess {
                if (pendingFeedbackPrefs.getString(sessionId, null) == feedback) {
                    pendingFeedbackPrefs.edit().remove(sessionId).apply()
                }
            }
        }
    }

    private suspend fun syncBoardHistory() {
        if (!bleManager.isConnected()) return

        var cursor: String? = null
        repeat(MAX_HISTORY_PAGES) {
            val response = bleManager.getSessionHistory(cursor, HISTORY_PAGE_SIZE).getOrNull()
                ?: return
            val data = runCatching {
                JSONObject(String(response, Charsets.UTF_8)).getJSONObject("data")
            }.getOrNull() ?: return
            val sessions = data.optJSONArray("sessions") ?: return

            for (index in 0 until sessions.length()) {
                val item = sessions.optJSONObject(index) ?: continue
                val sessionId = item.optString("session_id")
                val startTime = item.optLong("start_time_ms")
                if (sessionId.isBlank() || startTime <= 0L) continue
                val pendingFeedback = pendingFeedbackPrefs.getString(sessionId, null)

                sessionRepository.insertSession(
                    Session(
                        id = sessionId,
                        startTime = startTime,
                        endTime = item.optLong("end_time_ms").takeIf { value -> value > 0L },
                        duration = item.optInt("total_duration").coerceAtLeast(0),
                        focusDuration = item.optInt("focus_duration").coerceAtLeast(0),
                        distractionCount = item.optInt("distraction_count").coerceAtLeast(0),
                        reminderCount = item.optInt("reminder_count").coerceAtLeast(0),
                        phoneDistractionCount = item.optInt("phone_distraction_count").coerceAtLeast(0),
                        feedback = pendingFeedback ?: item.optString("feedback", "just_right"),
                        boardSessionId = sessionId,
                        source = "EDGEFOCUS",
                        synced = true
                    )
                )
            }

            val nextCursor = data.optString("next_cursor").takeIf { value -> value.isNotBlank() }
            val hasMore = data.optBoolean("has_more", false)
            if (!hasMore || nextCursor == null || nextCursor == cursor) return
            cursor = nextCursor
        }
    }

    private companion object {
        val VALID_FEEDBACK_VALUES = setOf("too_much", "too_little", "just_right")
        const val HISTORY_PAGE_SIZE = 1
        const val MAX_HISTORY_PAGES = 1000
    }
}
