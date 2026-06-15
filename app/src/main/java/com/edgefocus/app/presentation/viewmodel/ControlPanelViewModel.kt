package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISettingsRepository
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

enum class SystemMode {
    NORMAL, SILENT, HOME
}

enum class NfcStatus {
    CHECKING, AVAILABLE, UNAVAILABLE
}

data class ControlPanelUiState(
    val timerState: TimerState = TimerState.IDLE,
    val selectedDuration: Int = 25,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val systemMode: SystemMode = SystemMode.NORMAL,
    val awayTimeout: Int = 15,
    val reminderSensitivity: Float = 0.5f,
    val nfcStatus: NfcStatus = NfcStatus.CHECKING,
    val completedSessions: Int = 0,
    val totalFocusMinutes: Int = 0
)

@HiltViewModel
class ControlPanelViewModel @Inject constructor(
    private val settingsRepository: ISettingsRepository
) : ViewModel() {

    private val _uiState = MutableStateFlow(ControlPanelUiState())
    val uiState: StateFlow<ControlPanelUiState> = _uiState.asStateFlow()

    private var countdownJob: Job? = null

    init {
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    if (_uiState.value.timerState == TimerState.IDLE) {
                        val duration = it.pomodoroDuration.coerceIn(5, 120)
                        _uiState.update { state ->
                            state.copy(
                                selectedDuration = duration,
                                timeRemaining = TimeUtils.minutesToMillis(duration),
                                totalTime = TimeUtils.minutesToMillis(duration)
                            )
                        }
                    }
                }
            }
        }
        checkNfcStatus()
    }

    fun updateSelectedDuration(minutes: Int) {
        val clamped = minutes.coerceIn(5, 120)
        _uiState.update {
            it.copy(
                selectedDuration = clamped,
                timeRemaining = TimeUtils.minutesToMillis(clamped),
                totalTime = TimeUtils.minutesToMillis(clamped)
            )
        }
    }

    fun updateSystemMode(mode: SystemMode) {
        _uiState.update { it.copy(systemMode = mode) }
    }

    fun updateAwayTimeout(minutes: Int) {
        _uiState.update { it.copy(awayTimeout = minutes.coerceIn(5, 60)) }
    }

    fun updateReminderSensitivity(value: Float) {
        _uiState.update { it.copy(reminderSensitivity = value.coerceIn(0f, 1f)) }
    }

    fun startSession() {
        val state = _uiState.value
        if (state.timerState == TimerState.IDLE) {
            _uiState.update { it.copy(timerState = TimerState.RUNNING) }
            startCountdown()
        } else if (state.timerState == TimerState.PAUSED) {
            _uiState.update { it.copy(timerState = TimerState.RUNNING) }
            startCountdown()
        }
    }

    fun pauseSession() {
        if (_uiState.value.timerState == TimerState.RUNNING) {
            countdownJob?.cancel()
            _uiState.update { it.copy(timerState = TimerState.PAUSED) }
        }
    }

    fun stopSession() {
        countdownJob?.cancel()
        resetToIdle()
    }

    fun enableNfcScreenRotation() {
        viewModelScope.launch {
            val current = settingsRepository.getSettingsOnce()
            current?.let {
                settingsRepository.updateSettings(it.copy(autoRotateNFC = true))
            }
        }
    }

    private fun checkNfcStatus() {
        viewModelScope.launch {
            _uiState.update { it.copy(nfcStatus = NfcStatus.CHECKING) }
            delay(500)
            _uiState.update { it.copy(nfcStatus = NfcStatus.AVAILABLE) }
        }
    }

    private fun startCountdown() {
        countdownJob?.cancel()
        countdownJob = viewModelScope.launch {
            while (_uiState.value.timeRemaining > 0) {
                delay(1000)
                _uiState.update { currentState ->
                    val newTime = currentState.timeRemaining - 1000
                    currentState.copy(timeRemaining = if (newTime <= 0) 0 else newTime)
                }
            }
            onTimerFinished()
        }
    }

    private fun onTimerFinished() {
        val state = _uiState.value
        val focusMinutes = state.selectedDuration
        _uiState.update {
            it.copy(
                timerState = TimerState.FINISHED,
                completedSessions = it.completedSessions + 1,
                totalFocusMinutes = it.totalFocusMinutes + focusMinutes
            )
        }
    }

    private fun resetToIdle() {
        val duration = _uiState.value.selectedDuration
        _uiState.update {
            it.copy(
                timerState = TimerState.IDLE,
                timeRemaining = TimeUtils.minutesToMillis(duration),
                totalTime = TimeUtils.minutesToMillis(duration)
            )
        }
    }

    override fun onCleared() {
        super.onCleared()
        countdownJob?.cancel()
    }
}
