package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.nfc.NfcAdapter
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
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
import org.json.JSONObject
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
    val totalFocusMinutes: Int = 0,
    // 板端数据
    val batteryPercent: Int = -1,
    val focusStateStr: String = "",
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val calibrationCompleted: Boolean = false,
    val braceletConnected: Boolean = false,
    val cameraOk: Boolean = false,
    val todayTotalSeconds: Int = 0,
    val todayFocusSeconds: Int = 0,
    val todaySessions: Int = 0,
    val todayEvents: Int = 0
)

@HiltViewModel
class ControlPanelViewModel @Inject constructor(
    private val application: Application,
    private val settingsRepository: ISettingsRepository,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(ControlPanelUiState())
    val uiState: StateFlow<ControlPanelUiState> = _uiState.asStateFlow()

    private var countdownJob: Job? = null
    private var consoleDataPollingJob: Job? = null

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
        startConsoleDataPolling()
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
            val nfcAdapter = NfcAdapter.getDefaultAdapter(application)
            val status = if (nfcAdapter?.isEnabled == true) NfcStatus.AVAILABLE else NfcStatus.UNAVAILABLE
            _uiState.update { it.copy(nfcStatus = status) }
        }
    }

    private fun startConsoleDataPolling() {
        consoleDataPollingJob?.cancel()
        consoleDataPollingJob = viewModelScope.launch {
            while (true) {
                if (bleManager.isConnected()) {
                    fetchConsoleData()
                }
                delay(2000) // 每2秒轮询一次
            }
        }
    }

    private suspend fun fetchConsoleData() {
        try {
            val result = bleManager.getConsoleData()
            result.onSuccess { data ->
                parseConsoleData(String(data, Charsets.UTF_8))
            }
        } catch (e: Exception) {
            // 忽略错误，继续轮询
        }
    }

    private fun parseConsoleData(jsonString: String) {
        try {
            val json = JSONObject(jsonString)
            val data = json.getJSONObject("data")

            // 解析session数据
            val session = data.optJSONObject("session")
            session?.let {
                val focusState = it.optInt("focus_state", 0)
                val focusStateStr = it.optString("focus_state_str", "")
                val elapsedMs = it.optLong("elapsed_ms", 0)
                val targetDurationMs = it.optLong("target_duration_ms", 0)
                val remainingMs = it.optLong("remaining_ms", 0)
                val distractionCount = it.optInt("distraction_count", 0)
                val reminderCount = it.optInt("reminder_count", 0)
                val calibrationCompleted = it.optBoolean("calibration_completed", false)
                val isActive = it.optBoolean("active", false)

                _uiState.update { state ->
                    state.copy(
                        focusStateStr = focusStateStr,
                        distractionCount = distractionCount,
                        reminderCount = reminderCount,
                        calibrationCompleted = calibrationCompleted,
                        timeRemaining = if (isActive && remainingMs > 0) remainingMs else state.timeRemaining,
                        totalTime = if (targetDurationMs > 0) targetDurationMs else state.totalTime
                    )
                }
            }

            // 解析device数据
            val device = data.optJSONObject("device")
            device?.let {
                val batteryPercent = it.optInt("battery_percent", -1)
                val systemMode = it.optInt("system_mode", 0)
                val cameraOk = it.optBoolean("camera_ok", false)
                val braceletConnected = it.optBoolean("bracelet_connected", false)

                _uiState.update { state ->
                    state.copy(
                        batteryPercent = batteryPercent,
                        systemMode = when (systemMode) {
                            0 -> SystemMode.NORMAL
                            1 -> SystemMode.SILENT
                            2 -> SystemMode.HOME
                            else -> SystemMode.NORMAL
                        },
                        cameraOk = cameraOk,
                        braceletConnected = braceletConnected
                    )
                }
            }

            // 解析today数据
            val today = data.optJSONObject("today")
            today?.let {
                val totalSeconds = it.optInt("total_seconds", 0)
                val focusSeconds = it.optInt("focus_seconds", 0)
                val sessions = it.optInt("sessions", 0)
                val events = it.optInt("events", 0)

                _uiState.update { state ->
                    state.copy(
                        todayTotalSeconds = totalSeconds,
                        todayFocusSeconds = focusSeconds,
                        todaySessions = sessions,
                        todayEvents = events
                    )
                }
            }
        } catch (e: Exception) {
            // JSON解析错误，忽略
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
        consoleDataPollingJob?.cancel()
    }
}
