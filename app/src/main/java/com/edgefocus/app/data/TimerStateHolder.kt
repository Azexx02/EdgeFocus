package com.edgefocus.app.data

import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.util.TimeUtils
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import javax.inject.Inject
import javax.inject.Singleton

data class SharedTimerState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0,
    val targetDurationMinutes: Int = 25
)

@Singleton
class TimerStateHolder @Inject constructor() {

    private val _state = MutableStateFlow(SharedTimerState())
    val state: StateFlow<SharedTimerState> = _state.asStateFlow()

    fun updateState(newState: SharedTimerState) {
        _state.value = newState
    }

    fun updateFromTimerState(
        timerState: TimerState,
        sessionType: SessionType,
        timeRemaining: Long,
        totalTime: Long,
        completedSessions: Int,
        targetDurationMinutes: Int
    ) {
        _state.update {
            it.copy(
                timerState = timerState,
                sessionType = sessionType,
                timeRemaining = timeRemaining,
                totalTime = totalTime,
                completedSessions = completedSessions,
                targetDurationMinutes = targetDurationMinutes
            )
        }
    }

    fun updateBoardData(
        remainingMs: Long,
        targetDurationMs: Long,
        distractionCount: Int,
        reminderCount: Int,
        focusStateStr: String
    ) {
        _state.update {
            it.copy(
                timeRemaining = if (remainingMs > 0) remainingMs else it.timeRemaining,
                totalTime = if (targetDurationMs > 0) targetDurationMs else it.totalTime
            )
        }
    }
}
