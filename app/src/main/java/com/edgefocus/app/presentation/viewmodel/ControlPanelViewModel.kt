package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.nfc.NfcAdapter
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.TimerStateHolder
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
    private val application: Application,
    private val settingsRepository: ISettingsRepository,
    private val timerStateHolder: TimerStateHolder
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(ControlPanelUiState())
    val uiState: StateFlow<ControlPanelUiState> = _uiState.asStateFlow()

    init {
        // 加载设置
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    val duration = it.pomodoroDuration.coerceIn(5, 120)
                    _uiState.update { state ->
                        state.copy(selectedDuration = duration)
                    }
                }
            }
        }

        // 从 TimerStateHolder 同步状态
        viewModelScope.launch {
            timerStateHolder.state.collect { sharedState ->
                _uiState.update { state ->
                    state.copy(
                        timerState = sharedState.timerState,
                        timeRemaining = sharedState.timeRemaining,
                        totalTime = sharedState.totalTime,
                        completedSessions = sharedState.completedSessions,
                        selectedDuration = sharedState.targetDurationMinutes
                    )
                }
            }
        }

        checkNfcStatus()
    }

    fun updateSelectedDuration(minutes: Int) {
        val clamped = minutes.coerceIn(5, 120)
        _uiState.update { it.copy(selectedDuration = clamped) }
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
        val duration = _uiState.value.selectedDuration
        timerStateHolder.startSession(duration, true) // 使用板端
    }

    fun pauseSession() {
        timerStateHolder.pauseSession()
    }

    fun stopSession() {
        timerStateHolder.stopSession()
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
            val nfcAdapter = NfcAdapter.getDefaultAdapter(application)
            val status = if (nfcAdapter?.isEnabled == true) NfcStatus.AVAILABLE else NfcStatus.UNAVAILABLE
            _uiState.update { it.copy(nfcStatus = status) }
        }
    }
}
