package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.util.TimeUtils
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

data class TimerUiState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0
)

@HiltViewModel
class HomeViewModel @Inject constructor() : ViewModel() {

    private val _uiState = MutableStateFlow(TimerUiState())
    val uiState: StateFlow<TimerUiState> = _uiState.asStateFlow()

    private var countdownJob: Job? = null

    companion object {
        const val WORK_DURATION_MINUTES = 25
        const val SHORT_BREAK_MINUTES = 5
        const val LONG_BREAK_MINUTES = 15
        const val SESSIONS_BEFORE_LONG_BREAK = 4
    }

    fun startTimer() {
        val currentState = _uiState.value.timerState
        if (currentState == TimerState.IDLE || currentState == TimerState.PAUSED) {
            _uiState.update { it.copy(timerState = TimerState.RUNNING) }
            startCountdown()
        }
    }

    fun pauseTimer() {
        if (_uiState.value.timerState == TimerState.RUNNING) {
            countdownJob?.cancel()
            _uiState.update { it.copy(timerState = TimerState.PAUSED) }
        }
    }

    fun stopTimer() {
        countdownJob?.cancel()
        resetToIdle()
    }

    private fun startCountdown() {
        countdownJob?.cancel()
        countdownJob = viewModelScope.launch {
            while (_uiState.value.timeRemaining > 0) {
                delay(1000)
                _uiState.update { currentState ->
                    val newTime = currentState.timeRemaining - 1000
                    if (newTime <= 0) {
                        currentState.copy(timeRemaining = 0)
                    } else {
                        currentState.copy(timeRemaining = newTime)
                    }
                }
            }
            onTimerFinished()
        }
    }

    private fun onTimerFinished() {
        val currentState = _uiState.value
        when (currentState.sessionType) {
            SessionType.WORK -> {
                val newCompletedSessions = currentState.completedSessions + 1
                _uiState.update {
                    it.copy(
                        completedSessions = newCompletedSessions,
                        timerState = TimerState.FINISHED
                    )
                }
                startBreak(newCompletedSessions)
            }
            SessionType.SHORT_BREAK, SessionType.LONG_BREAK -> {
                _uiState.update {
                    it.copy(
                        sessionType = SessionType.WORK,
                        timerState = TimerState.FINISHED
                    )
                }
                resetToIdle()
            }
        }
    }

    private fun startBreak(completedSessions: Int) {
        val isLongBreak = completedSessions % SESSIONS_BEFORE_LONG_BREAK == 0
        val breakDuration = if (isLongBreak) {
            TimeUtils.minutesToMillis(LONG_BREAK_MINUTES)
        } else {
            TimeUtils.minutesToMillis(SHORT_BREAK_MINUTES)
        }
        val breakType = if (isLongBreak) SessionType.LONG_BREAK else SessionType.SHORT_BREAK

        _uiState.update {
            it.copy(
                sessionType = breakType,
                timerState = TimerState.BREAK,
                timeRemaining = breakDuration,
                totalTime = breakDuration
            )
        }
        startCountdown()
    }

    private fun resetToIdle() {
        val workDuration = TimeUtils.minutesToMillis(WORK_DURATION_MINUTES)
        _uiState.update {
            it.copy(
                timerState = TimerState.IDLE,
                sessionType = SessionType.WORK,
                timeRemaining = workDuration,
                totalTime = workDuration
            )
        }
    }

    override fun onCleared() {
        super.onCleared()
        countdownJob?.cancel()
    }
}