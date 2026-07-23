package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.remote.LocalSessionUpload
import com.edgefocus.app.data.remote.PowerProfile
import com.edgefocus.app.data.remote.BoardPrecheck
import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.CameraPreviewImage
import com.edgefocus.app.data.remote.GimbalStatus
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISessionRepository
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.domain.session.AppSessionCoordinator
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
import java.util.concurrent.CancellationException
import javax.inject.Inject

data class TimerUiState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0,
    val sessionMode: SessionMode = SessionMode.LOCAL,
    val isBoardConnected: Boolean = false,
    val isSyncingWithBoard: Boolean = false,
    val sessionId: String? = null,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val boardBatteryPercent: Int = 67,
    val boardBatteryCharging: Boolean = false,
    val boardPowerProfile: PowerProfile = PowerProfile.GO,
    val externalPowerState: ExternalPowerState = ExternalPowerState.UNKNOWN,
    val precheck: BoardPrecheck? = null,
    val preparationState: BoardPreparationState = BoardPreparationState.NONE,
    val preparationError: String? = null,
    val cameraPreview: CameraPreviewImage? = null,
    val gimbalStatus: GimbalStatus? = null,
    val precheckGuidance: String? = null,
    val boardErrorMessage: String? = null,
    val isPrecheckConfirming: Boolean = false
)

@HiltViewModel
class HomeViewModel @Inject constructor(
    application: Application,
    private val settingsRepository: ISettingsRepository,
    private val sessionRepository: ISessionRepository,
    private val timerStateHolder: TimerStateHolder,
    private val sessionCoordinator: AppSessionCoordinator,
    private val connectionSupervisor: ConnectionSupervisor,
    private val precheckController: PrecheckController
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(TimerUiState())
    val uiState: StateFlow<TimerUiState> = _uiState.asStateFlow()

    private var workDurationMinutes = 25

    fun setWorkDuration(minutes: Int) {
        workDurationMinutes = minutes.coerceIn(1, 120)
        timerStateHolder.updateIdleDuration(workDurationMinutes)
    }

    fun getWorkDurationMinutes(): Int = workDurationMinutes

    private var currentSessionId: String? = null
    private var sessionStartTime: Long = 0L
    private var hasSavedCurrentSession = false
    private var isAppMonitorRunning = false

    init {
        viewModelScope.launch {
            connectionSupervisor.snapshot.collect { snapshot ->
                _uiState.update { state ->
                    state.copy(
                        externalPowerState = snapshot.hostState?.power?.state ?: ExternalPowerState.UNKNOWN,
                        boardPowerProfile = when (snapshot.hostState?.power?.profile) {
                            "home" -> PowerProfile.HOME
                            "go" -> PowerProfile.GO
                            else -> state.boardPowerProfile
                        }
                    )
                }
            }
        }
        // 加载设置
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    workDurationMinutes = it.pomodoroDuration.coerceIn(1, 120)
                    timerStateHolder.updateIdleDuration(workDurationMinutes)

                    // 空闲状态时更新显示时间
                    if (_uiState.value.timerState == TimerState.IDLE) {
                        val workDuration = TimeUtils.minutesToMillis(workDurationMinutes)
                        _uiState.update { state ->
                            state.copy(
                                timeRemaining = workDuration,
                                totalTime = workDuration
                            )
                        }
                    }
                }
            }
        }

        viewModelScope.launch {
            timerStateHolder.state.collect { sharedState ->
                if (sharedState.timerState == TimerState.IDLE || sharedState.timerState == TimerState.FINISHED) {
                    workDurationMinutes = sharedState.targetDurationMinutes.coerceIn(1, 120)
                }
                _uiState.update { state ->
                    state.copy(
                        timerState = sharedState.timerState,
                        sessionType = sharedState.sessionType,
                        timeRemaining = sharedState.timeRemaining,
                        totalTime = sharedState.totalTime,
                        completedSessions = sharedState.completedSessions,
                        sessionMode = sharedState.sessionMode,
                        isBoardConnected = sharedState.isBoardConnected,
                        isSyncingWithBoard = sharedState.isSyncingWithBoard,
                        sessionId = sharedState.sessionId,
                        distractionCount = sharedState.distractionCount,
                        reminderCount = sharedState.reminderCount,
                        phoneDistractionCount = sharedState.phoneDistractionCount,
                        boardBatteryPercent = sharedState.boardBatteryPercent,
                        boardBatteryCharging = sharedState.boardBatteryCharging,
                        boardPowerProfile = sharedState.boardPowerProfile,
                        precheck = sharedState.precheck,
                        preparationState = sharedState.preparationState,
                        preparationError = sharedState.preparationError,
                        cameraPreview = sharedState.cameraPreview,
                        gimbalStatus = sharedState.gimbalStatus,
                        precheckGuidance = sharedState.precheckGuidance,
                        boardErrorMessage = sharedState.boardErrorMessage
                    )
                }

                // 板端同步启动会话时，自动启动 AppMonitorService
            }
        }
        viewModelScope.launch {
            precheckController.isConfirming.collect { confirming ->
                _uiState.update { it.copy(isPrecheckConfirming = confirming) }
            }
        }
    }

    /**
     * 启动 EdgeFocus 模式会话
     */
    fun startEdgeFocusSession() {
        if (!timerStateHolder.isBoardConnected()) {
            return
        }

        sessionCoordinator.startEdgeFocus(workDurationMinutes)
    }

    /**
     * 启动本地模式会话
     */
    fun startLocalSession() {
        sessionCoordinator.startLocal(workDurationMinutes)
    }

    /**
     * 根据设置自动选择模式启动
     */
    fun startTimer(useBoard: Boolean) {
        when (HomeStartActionPolicy.decide(_uiState.value.timerState, useBoard)) {
            HomeStartAction.START_EDGEFOCUS -> startEdgeFocusSession()
            HomeStartAction.START_LOCAL -> startLocalSession()
            HomeStartAction.SKIP_PRECHECK -> timerStateHolder.skipPrecheck()
            HomeStartAction.RESUME -> timerStateHolder.resumeSession()
            HomeStartAction.NONE -> Unit
        }
    }

    fun pauseTimer() {
        val state = _uiState.value.timerState
        if (state == TimerState.RUNNING) {
            sessionCoordinator.pause()
        }
    }

    fun stopTimer() {
        val ui = _uiState.value

        // 本地模式手动停止时直接保存；EdgeFocus 模式等待板端 push_session_end
        if (HomeSessionCompletionPolicy.shouldSaveLocalOnStop(
                sessionMode = ui.sessionMode,
                timerState = ui.timerState,
                sessionStartTime = sessionStartTime,
                currentSessionId = currentSessionId,
                hasSavedCurrentSession = hasSavedCurrentSession
            )
        ) {
            handleSessionFinished()
        }
        if (HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(ui.timerState)) {
            stopAppMonitor()
        }

        sessionCoordinator.stop()
    }

    fun refreshCameraPreview() = timerStateHolder.refreshCameraPreview()

    fun confirmPrecheck() {
        runPrecheckCommand { precheckController.confirmPositionAndContinue() }
    }

    fun skipPrecheckAutoAdjust() {
        runPrecheckCommand { precheckController.skipAutoAdjust() }
    }

    fun nudgeGimbal(direction: String, fineTune: Boolean) {
        runPrecheckCommand { precheckController.nudge(direction, fineTune) }
    }

    fun centerGimbal() {
        runPrecheckCommand { precheckController.center() }
    }

    fun stopGimbal() {
        runPrecheckCommand { precheckController.stop() }
    }

    private fun runPrecheckCommand(block: suspend () -> Result<Unit>) {
        viewModelScope.launch {
            block()
                .onSuccess { _uiState.update { it.copy(boardErrorMessage = null) } }
                .onFailure { error ->
                    if (error !is CancellationException) {
                        _uiState.update {
                            it.copy(boardErrorMessage = error.message ?: "主机预检操作失败")
                        }
                    }
                }
        }
    }

    private fun handleSessionFinished() {
        if (hasSavedCurrentSession) return

        val ui = _uiState.value
        val isEdgeFocus = ui.sessionMode == SessionMode.EDGEFOCUS
        val hasBoardReport = timerStateHolder.hasBoardSessionReport()
        val sessionId = currentSessionId ?: return

        // Local mode saves immediately; EdgeFocus waits until the board report marks the session finished.
        if (!isEdgeFocus || ui.timerState == TimerState.FINISHED) {
            val elapsedSeconds = HomeSessionCompletionPolicy.elapsedSeconds(
                isEdgeFocus = isEdgeFocus,
                boardTotalDuration = timerStateHolder.getBoardTotalDuration(),
                totalTimeMs = ui.totalTime,
                timeRemainingMs = ui.timeRemaining
            )
            val focusSeconds = HomeSessionCompletionPolicy.focusSeconds(
                isEdgeFocus = isEdgeFocus,
                boardFocusDuration = timerStateHolder.getBoardFocusDuration(),
                elapsedSeconds = elapsedSeconds
            )
            val startTime = if (sessionStartTime > 0) sessionStartTime else System.currentTimeMillis()
            val endTime = System.currentTimeMillis()
            val session = Session(
                id = sessionId,
                startTime = startTime,
                endTime = endTime,
                duration = elapsedSeconds,
                focusDuration = focusSeconds,
                distractionCount = HomeSessionCompletionPolicy.distractionCountForSession(
                    isEdgeFocus = isEdgeFocus,
                    boardDistractionCount = timerStateHolder.getBoardSessionDistractionCount(),
                    localDistractionCount = ui.distractionCount,
                    phoneDistractionCount = ui.phoneDistractionCount
                ),
                reminderCount = if (isEdgeFocus) timerStateHolder.getBoardSessionReminderCount() else 0,
                phoneDistractionCount = HomeSessionCompletionPolicy.phoneDistractionCountForSession(
                    isEdgeFocus = isEdgeFocus,
                    boardPhoneDistractionCount = timerStateHolder.getBoardSessionPhoneDistractionCount(),
                    localPhoneDistractionCount = ui.phoneDistractionCount
                ),
                feedback = HomeSessionCompletionPolicy.defaultFeedback(),
                boardSessionId = if (isEdgeFocus) sessionId else null,
                source = if (isEdgeFocus) "EDGEFOCUS" else "LOCAL",
                synced = isEdgeFocus && hasBoardReport
            )
            val shouldUploadLocalSession = !isEdgeFocus && ui.isBoardConnected
            viewModelScope.launch {
                sessionRepository.insertSession(session)
                if (shouldUploadLocalSession) {
                    timerStateHolder.syncSessionToBoard(session.toLocalSessionUpload())
                }
            }

            hasSavedCurrentSession = true
            currentSessionId = null
            sessionStartTime = 0L
        }
    }

    private fun startAppMonitor() {
        if (isAppMonitorRunning) return

        try {
            val context = getApplication<Application>()
            AppMonitorService.startService(context, currentSessionId)
            isAppMonitorRunning = true
        } catch (e: Exception) {
            android.util.Log.e("HomeViewModel", "Failed to start AppMonitorService: ${e.message}")
        }
    }

    private fun stopAppMonitor() {
        if (!isAppMonitorRunning) return

        try {
            AppMonitorService.stopService(getApplication())
            isAppMonitorRunning = false
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    private fun Session.toLocalSessionUpload(): LocalSessionUpload =
        LocalSessionUpload(
            sessionId = id,
            startTimeMs = startTime,
            endTimeMs = endTime ?: System.currentTimeMillis(),
            totalDurationSec = duration.coerceAtLeast(0),
            focusDurationSec = focusDuration.coerceAtLeast(0),
            distractionCount = distractionCount,
            reminderCount = reminderCount,
            phoneDistractionCount = phoneDistractionCount
        )

}
