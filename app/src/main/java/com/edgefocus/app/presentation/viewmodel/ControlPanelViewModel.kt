package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.nfc.NfcAdapter
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.BoardPreferences
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BoardPrecheck
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.CameraPreviewImage
import com.edgefocus.app.data.remote.GimbalStatus
import com.edgefocus.app.data.remote.OutputCapability
import com.edgefocus.app.data.remote.PushAlert
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISessionRepository
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.service.AppMonitorService
import com.edgefocus.app.util.TimeUtils
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
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
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val vibrationStrength: Float = 0.5f,
    val ledBrightness: Int = 75,
    val isBoardConnected: Boolean = false,
    val isSyncingWithBoard: Boolean = false,
    val arePreferencesLoaded: Boolean = false,
    val sessionId: String? = null,
    val boardSessionActive: Boolean = false,
    val boardSessionPaused: Boolean = false,
    val calibrationCompleted: Boolean = false,
    val sessionPhase: BoardSessionPhase = BoardSessionPhase.IDLE,
    val focusState: Int = 0,
    val elapsedMs: Long = 0L,
    val precheck: BoardPrecheck? = null,
    val outputCapability: OutputCapability? = null,
    val cameraPreview: CameraPreviewImage? = null,
    val gimbalStatus: GimbalStatus? = null,
    val lastAlert: PushAlert? = null,
    val boardErrorMessage: String? = null,
    val reminderSnapshot: String? = null,
    val precheckGuidance: String? = null,
    val focusReason: String? = null,
    val behaviorContext: String? = null,
    val soundVolume: Float = 0.7f,
    val defaultFocusDurationMin: Int = 25,
    val normalChannels: Set<String> = setOf("screen", "band", "sound"),
    val silentChannels: Set<String> = setOf("screen", "band")
)

@HiltViewModel
class ControlPanelViewModel @Inject constructor(
    private val application: Application,
    private val settingsRepository: ISettingsRepository,
    private val sessionRepository: ISessionRepository,
    private val timerStateHolder: TimerStateHolder,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(ControlPanelUiState())
    val uiState: StateFlow<ControlPanelUiState> = _uiState.asStateFlow()

    private var currentSessionId: String? = null
    private var sessionStartTime: Long = 0L
    private var hasSavedCurrentSession = false
    private var isAppMonitorRunning = false

    init {
        timerStateHolder.enterControlPanelMode()

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

        viewModelScope.launch {
            var prevTimerState = TimerState.IDLE
            var wasBoardConnected = false
            timerStateHolder.state.collect { sharedState ->
                _uiState.update { state ->
                    state.copy(
                        timerState = sharedState.timerState,
                        timeRemaining = sharedState.timeRemaining,
                        totalTime = sharedState.totalTime,
                        selectedDuration = sharedState.targetDurationMinutes,
                        isBoardConnected = sharedState.isBoardConnected,
                        isSyncingWithBoard = sharedState.isSyncingWithBoard,
                        sessionId = sharedState.sessionId,
                        boardSessionActive = sharedState.boardSessionActive,
                        boardSessionPaused = sharedState.boardSessionPaused,
                        calibrationCompleted = sharedState.calibrationCompleted,
                        distractionCount = sharedState.distractionCount,
                        reminderCount = sharedState.reminderCount,
                        phoneDistractionCount = sharedState.phoneDistractionCount,
                        sessionPhase = sharedState.sessionPhase,
                        focusState = sharedState.focusState,
                        elapsedMs = sharedState.elapsedMs,
                        precheck = sharedState.precheck,
                        outputCapability = sharedState.outputCapability,
                        cameraPreview = sharedState.cameraPreview,
                        gimbalStatus = sharedState.gimbalStatus,
                        lastAlert = sharedState.lastAlert,
                        boardErrorMessage = sharedState.boardErrorMessage,
                        reminderSnapshot = sharedState.reminderSnapshot,
                        precheckGuidance = sharedState.precheckGuidance,
                        focusReason = sharedState.focusReason,
                        behaviorContext = sharedState.behaviorContext
                    )
                }

                if ((prevTimerState == TimerState.IDLE || prevTimerState == TimerState.FINISHED) &&
                    (sharedState.timerState == TimerState.RUNNING ||
                        sharedState.timerState == TimerState.PRECHECK ||
                        sharedState.timerState == TimerState.PAUSED)) {
                    currentSessionId = sharedState.sessionId ?: currentSessionId ?: "session_${System.currentTimeMillis()}"
                    sessionStartTime = sharedState.sessionStartTime.takeIf { it > 0L }
                        ?: sessionStartTime.takeIf { it > 0L }
                        ?: System.currentTimeMillis()
                    hasSavedCurrentSession = false
                }

                if (prevTimerState != TimerState.RUNNING &&
                    sharedState.timerState == TimerState.RUNNING) {
                    startAppMonitor()
                }

                if (prevTimerState != TimerState.FINISHED && sharedState.timerState == TimerState.FINISHED) {
                    saveFinishedSession()
                    stopAppMonitor()
                }
                if (sharedState.isBoardConnected && !wasBoardConnected) {
                    viewModelScope.launch { loadBoardPreferences() }
                } else if (!sharedState.isBoardConnected && wasBoardConnected) {
                    _uiState.update { it.copy(arePreferencesLoaded = false) }
                }
                wasBoardConnected = sharedState.isBoardConnected
                prevTimerState = sharedState.timerState
            }
        }

        checkNfcStatus()

    }

    fun updateSelectedDuration(minutes: Int) {
        val clamped = minutes.coerceIn(5, 120)
        _uiState.update { it.copy(selectedDuration = clamped) }
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

    fun setVibrationStrength(value: Float, persist: Boolean = true) {
        val clamped = value.coerceIn(0f, 1f)
        _uiState.update { it.copy(vibrationStrength = clamped) }
        if (persist) sendPreferences("vibration_strength", clamped.toString())
    }

    fun setLedBrightness(value: Int, persist: Boolean = true) {
        val clamped = value.coerceIn(0, 100)
        _uiState.update { it.copy(ledBrightness = clamped) }
        if (persist) sendPreferences("led_brightness", clamped.toString())
    }

    fun setReminderSensitivity(value: Float, persist: Boolean = true) {
        val clamped = value.coerceIn(0f, 1f)
        _uiState.update { it.copy(reminderSensitivity = clamped) }
        if (persist) sendPreferences("reminder_sensitivity", clamped.toString())
    }

    fun setAwayTimeout(minutes: Int, persist: Boolean = true) {
        val clamped = minutes.coerceIn(5, 60)
        _uiState.update { it.copy(awayTimeout = clamped) }
        if (persist) sendPreferences("away_timeout_min", clamped.toString())
    }

    fun setSoundVolume(value: Float, persist: Boolean = true) {
        val clamped = value.coerceIn(0f, 1f)
        _uiState.update { it.copy(soundVolume = clamped) }
        if (persist) sendPreferences("sound_volume", clamped.toString())
    }

    fun setDefaultFocusDuration(minutes: Int, persist: Boolean = true) {
        val clamped = minutes.coerceIn(5, 120)
        _uiState.update { it.copy(defaultFocusDurationMin = clamped) }
        if (persist) sendPreferences("default_focus_duration_min", clamped.toString())
    }

    fun setNormalChannel(channel: String, enabled: Boolean) {
        val channels = _uiState.value.normalChannels.toMutableSet()
        if (enabled) channels.add(channel) else channels.remove(channel)
        _uiState.update { it.copy(normalChannels = channels) }
        sendPreferences(mapOf("normal_channels" to channels.toList()))
    }

    fun setSilentChannel(channel: String, enabled: Boolean) {
        if (channel == "sound") return
        val channels = _uiState.value.silentChannels.toMutableSet()
        if (enabled) channels.add(channel) else channels.remove(channel)
        _uiState.update { it.copy(silentChannels = channels) }
        sendPreferences(mapOf("silent_channels" to channels.toList()))
    }

    private fun sendPreferences(key: String, value: String) {
        if (!_uiState.value.isBoardConnected || !_uiState.value.arePreferencesLoaded) return
        viewModelScope.launch {
            val result = bleManager.updatePreferences(key, value)
            if (result.isFailure) loadBoardPreferences()
        }
    }

    private fun sendPreferences(preferences: Map<String, Any?>) {
        if (!_uiState.value.isBoardConnected || !_uiState.value.arePreferencesLoaded) return
        viewModelScope.launch {
            val result = bleManager.updatePreferences(preferences)
            if (result.isFailure) loadBoardPreferences()
        }
    }

    fun commitReminderSensitivity() {
        sendPreferences("reminder_sensitivity", _uiState.value.reminderSensitivity.toString())
    }

    fun commitAwayTimeout() {
        sendPreferences("away_timeout_min", _uiState.value.awayTimeout.toString())
    }

    fun commitVibrationStrength() {
        sendPreferences("vibration_strength", _uiState.value.vibrationStrength.toString())
    }

    fun commitLedBrightness() {
        sendPreferences("led_brightness", _uiState.value.ledBrightness.toString())
    }

    fun commitSoundVolume() {
        sendPreferences("sound_volume", _uiState.value.soundVolume.toString())
    }

    fun commitDefaultFocusDuration() {
        sendPreferences("default_focus_duration_min", _uiState.value.defaultFocusDurationMin.toString())
    }

    fun refreshBoardPreferences() {
        if (!_uiState.value.isBoardConnected) return
        viewModelScope.launch { loadBoardPreferences() }
    }

    private suspend fun loadBoardPreferences() {
        _uiState.update { it.copy(arePreferencesLoaded = false) }
        val response = bleManager.getPreferences().getOrNull() ?: return
        val data = runCatching {
            org.json.JSONObject(String(response, Charsets.UTF_8)).getJSONObject("data")
        }.getOrNull() ?: return
        if (!bleManager.isConnected()) return
        _uiState.update {
            val prefs = BoardJsonParser.parsePreferences(
                data,
                BoardPreferences(
                    systemMode = if (it.systemMode == SystemMode.SILENT) 1 else 0,
                    awayTimeoutMin = it.awayTimeout,
                    defaultFocusDurationMin = it.defaultFocusDurationMin,
                    vibrationStrength = it.vibrationStrength,
                    ledBrightness = it.ledBrightness,
                    reminderSensitivity = it.reminderSensitivity,
                    soundVolume = it.soundVolume,
                    normalChannels = it.normalChannels,
                    silentChannels = it.silentChannels
                )
            )
            it.copy(
                systemMode = if (prefs.systemMode == 1) SystemMode.SILENT else SystemMode.NORMAL,
                awayTimeout = prefs.awayTimeoutMin.coerceIn(5, 60),
                defaultFocusDurationMin = prefs.defaultFocusDurationMin.coerceIn(5, 120),
                vibrationStrength = prefs.vibrationStrength.coerceIn(0f, 1f),
                ledBrightness = prefs.ledBrightness.coerceIn(0, 100),
                reminderSensitivity = prefs.reminderSensitivity.coerceIn(0f, 1f),
                soundVolume = prefs.soundVolume.coerceIn(0f, 1f),
                normalChannels = prefs.normalChannels,
                silentChannels = prefs.silentChannels,
                arePreferencesLoaded = true
            )
        }
    }

    fun startSession() {
        val duration = _uiState.value.selectedDuration
        currentSessionId = "session_${System.currentTimeMillis()}"
        sessionStartTime = System.currentTimeMillis()
        hasSavedCurrentSession = false
        _uiState.update { it.copy(distractionCount = 0, phoneDistractionCount = 0) }
        timerStateHolder.startEdgeFocusSession(duration, currentSessionId!!, sessionStartTime)
    }

    fun pauseSession() {
        timerStateHolder.pauseSession()
    }

    fun resumeSession() {
        timerStateHolder.resumeSession()
    }

    fun stopSession() {
        stopAppMonitor()
        timerStateHolder.stopSession()
    }

    fun skipPrecheck() {
        timerStateHolder.skipPrecheck()
    }

    fun skipCalibration() {
        timerStateHolder.skipCalibration()
    }

    fun refreshCameraPreview() {
        timerStateHolder.refreshCameraPreview()
    }

    fun refreshGimbalStatus() {
        timerStateHolder.refreshGimbalStatus()
    }

    fun moveGimbal(deltaX: Int, deltaY: Int) {
        val action = when {
            deltaX < 0 -> "nudge_left"
            deltaX > 0 -> "nudge_right"
            deltaY < 0 -> "nudge_up"
            deltaY > 0 -> "nudge_down"
            else -> return
        }
        val step = maxOf(kotlin.math.abs(deltaX), kotlin.math.abs(deltaY)).coerceIn(1, 8)
        timerStateHolder.moveGimbal(action = action, stepDeg = step)
    }

    fun stopGimbal() {
        timerStateHolder.moveGimbal(action = "stop", speed = 10)
    }

    fun centerGimbal() {
        timerStateHolder.moveGimbal(action = "center_x", speed = 10)
    }

    private fun saveFinishedSession() {
        if (hasSavedCurrentSession) return

        val sessionId = currentSessionId ?: return
        val state = timerStateHolder.state.value
        val isEdgeFocus = state.sessionMode == SessionMode.EDGEFOCUS
        val hasBoardReport = timerStateHolder.hasBoardSessionReport()
        val elapsedSeconds = if (isEdgeFocus && timerStateHolder.getBoardTotalDuration() > 0) {
            timerStateHolder.getBoardTotalDuration()
        } else {
            ((state.totalTime - state.timeRemaining) / 1000).toInt()
        }
        val focusSeconds = if (isEdgeFocus && timerStateHolder.getBoardFocusDuration() > 0) {
            timerStateHolder.getBoardFocusDuration()
        } else {
            elapsedSeconds
        }

        viewModelScope.launch {
            sessionRepository.insertSession(
                Session(
                    id = sessionId,
                    startTime = if (sessionStartTime > 0) sessionStartTime else System.currentTimeMillis(),
                    endTime = System.currentTimeMillis(),
                    duration = elapsedSeconds,
                    focusDuration = focusSeconds,
                    distractionCount = if (isEdgeFocus) timerStateHolder.getBoardSessionDistractionCount() else 0,
                    reminderCount = if (isEdgeFocus) timerStateHolder.getBoardSessionReminderCount() else 0,
                    phoneDistractionCount = if (isEdgeFocus) {
                        timerStateHolder.getBoardSessionPhoneDistractionCount()
                    } else {
                        state.phoneDistractionCount
                    },
                    feedback = "just_right",
                    boardSessionId = if (isEdgeFocus) sessionId else null,
                    source = if (isEdgeFocus) "EDGEFOCUS" else "LOCAL",
                    synced = isEdgeFocus && hasBoardReport
                )
            )
        }

        hasSavedCurrentSession = true
        currentSessionId = null
        sessionStartTime = 0L
    }

    private fun startAppMonitor() {
        if (isAppMonitorRunning) return

        try {
            AppMonitorService.startService(application, currentSessionId)
            isAppMonitorRunning = true
        } catch (e: Exception) {
            android.util.Log.e("ControlPanelViewModel", "Failed to start AppMonitorService: ${e.message}")
        }
    }

    private fun stopAppMonitor() {
        if (!isAppMonitorRunning) return

        try {
            AppMonitorService.stopService(application)
            isAppMonitorRunning = false
        } catch (e: Exception) {
            android.util.Log.e("ControlPanelViewModel", "Failed to stop AppMonitorService: ${e.message}")
        }
    }

    fun dismissAlert() {
        timerStateHolder.clearAlert()
    }

    override fun onCleared() {
        timerStateHolder.leaveControlPanelMode()
        super.onCleared()
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
