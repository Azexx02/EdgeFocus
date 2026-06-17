package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.domain.repository.ISessionRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
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
    private val sessionRepository: ISessionRepository
) : ViewModel() {

    private val _uiState = MutableStateFlow(DashboardUiState())
    val uiState: StateFlow<DashboardUiState> = _uiState.asStateFlow()

    init {
        loadSessions()
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
        viewModelScope.launch {
            sessionRepository.updateFeedback(sessionId, feedback)
        }
    }
}
