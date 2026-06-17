package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.domain.model.SessionType
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

data class TimerUiState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0,
    val sessionMode: SessionMode = SessionMode.LOCAL,
    val isBoardConnected: Boolean = false,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0
)

@HiltViewModel
class HomeViewModel @Inject constructor(
    application: Application,
    private val settingsRepository: ISettingsRepository,
    private val sessionRepository: ISessionRepository,
    private val timerStateHolder: TimerStateHolder
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

        // 从 TimerStateHolder 同步状态
        viewModelScope.launch {
            var prevTimerState = TimerState.IDLE
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
                        distractionCount = sharedState.distractionCount,
                        reminderCount = sharedState.reminderCount,
                        phoneDistractionCount = sharedState.phoneDistractionCount
                    )
                }

                // 板端同步启动会话时，自动启动 AppMonitorService
                if (prevTimerState == TimerState.IDLE && sharedState.timerState == TimerState.RUNNING
                    && sharedState.sessionMode == SessionMode.EDGEFOCUS) {
                    if (currentSessionId == null) {
                        currentSessionId = "session_${System.currentTimeMillis()}"
                        sessionStartTime = System.currentTimeMillis()
                        startAppMonitor()
                    }
                }
                prevTimerState = sharedState.timerState

                // 会话结束时处理休息
                if (sharedState.timerState == TimerState.FINISHED) {
                    handleSessionFinished()
                }
            }
        }
    }

    /**
     * 启动 EdgeFocus 模式会话
     * 必须已连接设备
     */
    fun startEdgeFocusSession() {
        if (!timerStateHolder.isBoardConnected()) {
            return
        }

        currentSessionId = "session_${System.currentTimeMillis()}"
        sessionStartTime = System.currentTimeMillis()
        startAppMonitor()
        timerStateHolder.startEdgeFocusSession(workDurationMinutes)
    }

    /**
     * 启动本地模式会话
     * 不需要连接设备
     */
    fun startLocalSession() {
        currentSessionId = "session_${System.currentTimeMillis()}"
        sessionStartTime = System.currentTimeMillis()
        startAppMonitor()
        timerStateHolder.startLocalSession(workDurationMinutes)
    }

    /**
     * 根据设置自动选择模式启动
     */
    fun startTimer(useBoard: Boolean) {
        val currentState = _uiState.value.timerState

        if (currentState == TimerState.IDLE || currentState == TimerState.FINISHED) {
            if (useBoard) {
                startEdgeFocusSession()
            } else {
                startLocalSession()
            }
        } else if (currentState == TimerState.PAUSED) {
            timerStateHolder.resumeSession()
        }
    }

    fun pauseTimer() {
        val state = _uiState.value.timerState
        if (state == TimerState.RUNNING) {
            timerStateHolder.pauseSession()
        }
    }

    fun stopTimer() {
        // 手动停止时保存会话记录
        if (_uiState.value.timerState == TimerState.RUNNING && sessionStartTime > 0) {
            handleSessionFinished()
        }

        stopAppMonitor()

        // 本地模式结束后，尝试同步记录给设备
        if (_uiState.value.sessionMode == SessionMode.LOCAL && timerStateHolder.isBoardConnected()) {
            viewModelScope.launch {
                val durationMinutes = timerStateHolder.getTargetDurationMinutes()
                timerStateHolder.syncSessionToBoard(durationMinutes)
            }
        }

        timerStateHolder.stopSession()
    }

    private fun handleSessionFinished() {
        val ui = _uiState.value
        val isEdgeFocus = ui.sessionMode == SessionMode.EDGEFOCUS

        // EdgeFocus 模式：如果有板端数据就保存，否则等 push_session_end
        // 本地模式：直接保存
        if (!isEdgeFocus || currentSessionId != null) {
            val elapsedMs = ui.totalTime - ui.timeRemaining
            viewModelScope.launch {
                val session = Session(
                    id = currentSessionId ?: "session_${System.currentTimeMillis()}",
                    startTime = if (sessionStartTime > 0) sessionStartTime else System.currentTimeMillis(),
                    endTime = System.currentTimeMillis(),
                    duration = (elapsedMs / 1000).toInt(),
                    focusDuration = (elapsedMs / 1000).toInt(),
                    distractionCount = if (isEdgeFocus) timerStateHolder.getBoardSessionDistractionCount() else 0,
                    reminderCount = if (isEdgeFocus) timerStateHolder.getBoardSessionReminderCount() else 0,
                    phoneDistractionCount = ui.phoneDistractionCount,
                    feedback = null,
                    boardSessionId = currentSessionId,
                    source = if (isEdgeFocus) "EDGEFOCUS" else "LOCAL",
                    synced = false
                )
                sessionRepository.insertSession(session)
            }

            currentSessionId = null
            sessionStartTime = 0L
        }
    }

    private fun startAppMonitor() {
        try {
            val context = getApplication<Application>()
            AppMonitorService.startService(context, currentSessionId)
        } catch (e: Exception) {
            android.util.Log.e("HomeViewModel", "Failed to start AppMonitorService: ${e.message}")
        }
    }

    private fun stopAppMonitor() {
        try {
            AppMonitorService.stopService(getApplication())
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onCleared() {
        super.onCleared()
        stopAppMonitor()
    }
}
