package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.nfc.NfcAdapter
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.PushEventHandler
import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.FocusState
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.util.TimeUtils
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import javax.inject.Inject

enum class SystemMode {
    NORMAL, SILENT
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
    val totalFocusMinutes: Int = 0,
    val distractionCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val vibrationStrength: Float = 0.5f,
    val ledBrightness: Int = 75,
    val homeMode: Boolean = false
)

@HiltViewModel
class ControlPanelViewModel @Inject constructor(
    private val application: Application,
    private val settingsRepository: ISettingsRepository,
    private val timerStateHolder: TimerStateHolder,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(ControlPanelUiState())
    val uiState: StateFlow<ControlPanelUiState> = _uiState.asStateFlow()

    private val _alertEvent = MutableSharedFlow<AlertInfo>()
    val alertEvent: SharedFlow<AlertInfo> = _alertEvent.asSharedFlow()

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

        // 监听 BLE 推送的走神事件
        bleManager.setPushEventHandler(object : PushEventHandler {
            override fun onPushState(state: FocusState) {}
            override fun onPushAlert(alert: AlertInfo) {
                viewModelScope.launch {
                    _alertEvent.emit(alert)
                }
                if (alert.type == com.edgefocus.app.domain.model.AlertType.DISTRACTION) {
                    _uiState.update { it.copy(distractionCount = it.distractionCount + 1) }
                }
            }
            override fun onPushSessionEnd(summary: String) {}
            override fun onPushTimerComplete(timerComplete: com.edgefocus.app.data.remote.TimerComplete) {
                _uiState.update {
                    it.copy(
                        distractionCount = timerComplete.distractionCount,
                        phoneDistractionCount = timerComplete.reminderCount
                    )
                }
            }
        })
    }

    fun updateSelectedDuration(minutes: Int) {
        val clamped = minutes.coerceIn(5, 120)
        _uiState.update { it.copy(selectedDuration = clamped) }
    }

    fun acknowledgeReminder(alertId: String) {
        viewModelScope.launch {
            bleManager.reminderAck(alertId)
        }
    }

    fun setSystemMode(mode: Int) {
        val newMode = when (mode) {
            0 -> SystemMode.NORMAL
            1 -> SystemMode.SILENT
            else -> SystemMode.NORMAL
        }
        _uiState.update { it.copy(systemMode = newMode) }
        sendPreferences("system_mode", mode.toString())
    }

    fun setVibrationStrength(value: Float) {
        val clamped = value.coerceIn(0f, 1f)
        _uiState.update { it.copy(vibrationStrength = clamped) }
        sendPreferences("vibration_strength", clamped.toString())
    }

    fun setLedBrightness(value: Int) {
        val clamped = value.coerceIn(0, 100)
        _uiState.update { it.copy(ledBrightness = clamped) }
        sendPreferences("led_brightness", clamped.toString())
    }

    fun setReminderSensitivity(value: Float) {
        val clamped = value.coerceIn(0f, 1f)
        _uiState.update { it.copy(reminderSensitivity = clamped) }
        sendPreferences("reminder_sensitivity", clamped.toString())
    }

    fun setAwayTimeout(minutes: Int) {
        val clamped = minutes.coerceIn(5, 60)
        _uiState.update { it.copy(awayTimeout = clamped) }
        sendPreferences("away_timeout_min", clamped.toString())
    }

    fun setHomeMode(enabled: Boolean) {
        _uiState.update { it.copy(homeMode = enabled) }
        val mode = if (enabled) 2 else 0
        sendPreferences("system_mode", mode.toString())
    }

    private fun sendPreferences(key: String, value: String) {
        viewModelScope.launch {
            bleManager.updatePreferences(key, value)
        }
    }

    fun startSession() {
        val duration = _uiState.value.selectedDuration
        _uiState.update { it.copy(distractionCount = 0, phoneDistractionCount = 0) }
        timerStateHolder.startEdgeFocusSession(duration)
    }

    fun pauseSession() {
        timerStateHolder.pauseSession()
    }

    fun stopSession() {
        timerStateHolder.stopSession()
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
