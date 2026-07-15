package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.remote.LocalSessionUpload
import com.edgefocus.app.data.remote.PowerProfile
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.repository.ISessionRepository
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.domain.session.AppSessionCoordinator
import com.edgefocus.app.service.AppMonitorService
import com.edgefocus.app.util.TimeUtils
import dagger.hilt.android.lifecycle.HiltViewModel
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
    val completedSessions: Int = 0,
    val sessionMode: SessionMode = SessionMode.LOCAL,
    val isBoardConnected: Boolean = false,
    val isSyncingWithBoard: Boolean = false,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    val boardBatteryPercent: Int = 67,
    val boardBatteryCharging: Boolean = false,
    val boardPowerProfile: PowerProfile = PowerProfile.GO
)

@HiltViewModel
class HomeViewModel @Inject constructor(
    application: Application,
    private val settingsRepository: ISettingsRepository,
    private val sessionRepository: ISessionRepository,
    private val timerStateHolder: TimerStateHolder,
    private val sessionCoordinator: AppSessionCoordinator
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(TimerUiState())
    val uiState: StateFlow<TimerUiState> = _uiState.asStateFlow()

    private var workDurationMinutes = 25

    fun setWorkDuration(minutes: Int) {
        workDurationMinutes = minutes.coerceIn(1, 120)
        if (_uiState.value.timerState == TimerState.IDLE) {
            val duration = TimeUtils.minutesToMillis(workDurationMinutes)
            _uiState.update { it.copy(timeRemaining = duration, totalTime = duration) }
        }
    }

    fun getWorkDurationMinutes(): Int = workDurationMinutes

    private var currentSessionId: String? = null
    private var sessionStartTime: Long = 0L
    private var hasSavedCurrentSession = false
    private var isAppMonitorRunning = false

    init {
        // 加载设置
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    workDurationMinutes = it.pomodoroDuration

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
                        distractionCount = sharedState.distractionCount,
                        reminderCount = sharedState.reminderCount,
                        phoneDistractionCount = sharedState.phoneDistractionCount,
                        boardBatteryPercent = sharedState.boardBatteryPercent,
                        boardBatteryCharging = sharedState.boardBatteryCharging,
                        boardPowerProfile = sharedState.boardPowerProfile
                    )
                }

                // 板端同步启动会话时，自动启动 AppMonitorService
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
