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
import com.edgefocus.app.data.remote.NotificationMode
import com.edgefocus.app.data.remote.OutputCapability
import com.edgefocus.app.data.remote.PowerProfile
import com.edgefocus.app.data.remote.PushAlert
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISessionRepository
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.domain.session.AppSessionCoordinator
import com.edgefocus.app.domain.alert.AlertDeliveryCoordinator
import com.edgefocus.app.domain.precheck.PrecheckController
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.model.ExternalPowerState
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
    val nfcStatus: NfcStatus = NfcStatus.CHECKING,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val isBoardConnected: Boolean = false,
    val isSyncingWithBoard: Boolean = false,
    val arePreferencesLoaded: Boolean = false,
    val sessionId: String? = null,
    val boardSessionActive: Boolean = false,
    val boardSessionPaused: Boolean = false,
    val calibrationCompleted: Boolean = false,
    val sessionPhase: BoardSessionPhase = BoardSessionPhase.IDLE,
    val focusState: Int = 0,
    val phoneSignal: Boolean = false,
    val phoneUsageState: String = "unknown",
    val phoneDistractionLevel: Int = -1,
    val riskScore: Int = 0,
    val commandLevel: Int = 0,
    val boardBatteryPercent: Int = 67,
    val boardBatteryCharging: Boolean = false,
    val boardPowerSource: String = "battery",
    val boardPowerProfile: PowerProfile = PowerProfile.GO,
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
    val silentChannels: Set<String> = setOf("screen", "band"),
    val externalPowerState: ExternalPowerState = ExternalPowerState.UNKNOWN,
    val braceletConnected: Boolean? = null,
    val bandControlAvailable: Boolean = false
)

@HiltViewModel
class ControlPanelViewModel @Inject constructor(
    private val application: Application,
    private val settingsRepository: ISettingsRepository,
    private val sessionRepository: ISessionRepository,
    private val timerStateHolder: TimerStateHolder,
    private val bleManager: BleManager,
    private val sessionCoordinator: AppSessionCoordinator,
    private val alertDeliveryCoordinator: AlertDeliveryCoordinator,
    private val precheckController: PrecheckController,
    private val connectionSupervisor: ConnectionSupervisor
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
            connectionSupervisor.snapshot.collect { snapshot ->
                _uiState.update { state ->
                    state.copy(
                        externalPowerState = snapshot.hostState?.power?.state ?: ExternalPowerState.UNKNOWN,
                        braceletConnected = snapshot.hostState?.braceletConnected,
                        bandControlAvailable = snapshot.hello?.supports("band_proxy_control_v1") == true,
                        boardPowerProfile = when (snapshot.hostState?.power?.profile) {
                            "home" -> PowerProfile.HOME
                            "go" -> PowerProfile.GO
                            else -> state.boardPowerProfile
                        }
                    )
                }
            }
        }

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
                        phoneSignal = sharedState.phoneSignal,
                        phoneUsageState = sharedState.phoneUsageState,
                        phoneDistractionLevel = sharedState.phoneDistractionLevel,
                        riskScore = sharedState.riskScore,
                        commandLevel = sharedState.commandLevel,
                        boardBatteryPercent = sharedState.boardBatteryPercent,
                        boardBatteryCharging = sharedState.boardBatteryCharging,
                        boardPowerSource = sharedState.boardPowerSource,
                        boardPowerProfile = sharedState.boardPowerProfile,
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

                if (sharedState.isBoardConnected && !wasBoardConnected) {
                    viewModelScope.launch { loadBoardPreferences() }
                } else if (!sharedState.isBoardConnected && wasBoardConnected) {
                    _uiState.update { it.copy(arePreferencesLoaded = false) }
                }
                wasBoardConnected = sharedState.isBoardConnected
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
        sendPreferences("notification_mode", if (newMode == SystemMode.SILENT) "silent" else "normal")
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

    fun commitAwayTimeout() {
        sendPreferences("away_timeout_min", _uiState.value.awayTimeout.toString())
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
                    notificationMode = if (it.systemMode == SystemMode.SILENT) {
                        NotificationMode.SILENT
                    } else {
                        NotificationMode.NORMAL
                    },
                    awayTimeoutMin = it.awayTimeout,
                    defaultFocusDurationMin = it.defaultFocusDurationMin,
                    soundVolume = it.soundVolume,
                    normalChannels = it.normalChannels,
                    silentChannels = it.silentChannels
                )
            )
            it.copy(
                systemMode = if (prefs.notificationMode == NotificationMode.SILENT) {
                    SystemMode.SILENT
                } else {
                    SystemMode.NORMAL
                },
                awayTimeout = prefs.awayTimeoutMin.coerceIn(5, 60),
                defaultFocusDurationMin = prefs.defaultFocusDurationMin.coerceIn(5, 120),
                soundVolume = prefs.soundVolume.coerceIn(0f, 1f),
                normalChannels = prefs.normalChannels,
                silentChannels = prefs.silentChannels,
                arePreferencesLoaded = true
            )
        }
    }

    fun startSession() {
        val duration = _uiState.value.selectedDuration
        _uiState.update { it.copy(distractionCount = 0, phoneDistractionCount = 0) }
        if (timerStateHolder.getSessionMode() == SessionMode.EDGEFOCUS) {
            sessionCoordinator.startEdgeFocus(duration)
        } else {
            sessionCoordinator.startLocal(duration)
        }
    }

    fun pauseSession() {
        sessionCoordinator.pause()
    }

    fun resumeSession() {
        sessionCoordinator.resume()
    }

    fun stopSession() {
        sessionCoordinator.stop()
    }

    fun skipPrecheck() {
        runPrecheckCommand { precheckController.confirmPositionAndContinue() }
    }

    fun skipCalibration() {
        runPrecheckCommand { precheckController.skipAutoAdjustAndContinue() }
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
        val direction = when (action) {
            "nudge_left" -> "LEFT"
            "nudge_right" -> "RIGHT"
            "nudge_up" -> "UP"
            else -> "DOWN"
        }
        runPrecheckCommand { precheckController.nudge(direction) }
    }

    fun stopGimbal() {
        runPrecheckCommand { precheckController.stop() }
    }

    fun centerGimbal() {
        runPrecheckCommand { precheckController.center() }
    }

    private fun runPrecheckCommand(block: suspend () -> Result<Unit>) {
        viewModelScope.launch {
            block().onFailure { error ->
                _uiState.update { it.copy(boardErrorMessage = error.message ?: "主机预检操作失败") }
            }
        }
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
        }.coerceAtLeast(0)
        val focusSeconds = if (isEdgeFocus && timerStateHolder.getBoardFocusDuration() > 0) {
            timerStateHolder.getBoardFocusDuration()
        } else {
            elapsedSeconds
        }.coerceAtLeast(0)
        val localDistractionCount = maxOf(state.distractionCount, state.phoneDistractionCount)

        viewModelScope.launch {
            sessionRepository.insertSession(
                Session(
                    id = sessionId,
                    startTime = if (sessionStartTime > 0) sessionStartTime else System.currentTimeMillis(),
                    endTime = System.currentTimeMillis(),
                    duration = elapsedSeconds,
                    focusDuration = focusSeconds,
                    distractionCount = if (isEdgeFocus) timerStateHolder.getBoardSessionDistractionCount() else localDistractionCount,
                    reminderCount = if (isEdgeFocus) timerStateHolder.getBoardSessionReminderCount() else 0,
                    phoneDistractionCount = if (isEdgeFocus) {
                        timerStateHolder.getBoardSessionPhoneDistractionCount()
                    } else {
                        state.phoneDistractionCount
                    },
                    feedback = null,
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
        if (alertDeliveryCoordinator.activeAlert.value != null) {
            alertDeliveryCoordinator.userAcknowledgeActiveAlert()
        } else {
            timerStateHolder.clearAlert()
        }
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
