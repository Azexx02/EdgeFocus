package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
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
    val isBoardConnected: Boolean = false
)

@HiltViewModel
class HomeViewModel @Inject constructor(
    application: Application,
    private val settingsRepository: ISettingsRepository,
    private val timerStateHolder: TimerStateHolder
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(TimerUiState())
    val uiState: StateFlow<TimerUiState> = _uiState.asStateFlow()

    private var workDurationMinutes = 25
    private var shortBreakMinutes = 5
    private var longBreakMinutes = 15
    private var sessionsBeforeLongBreak = 4

    private var currentSessionId: String? = null

    init {
        // 加载设置
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    workDurationMinutes = it.pomodoroDuration
                    shortBreakMinutes = it.shortBreakDuration
                    longBreakMinutes = it.longBreakDuration
                    sessionsBeforeLongBreak = it.sessionsBeforeLongBreak

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
            timerStateHolder.state.collect { sharedState ->
                _uiState.update { state ->
                    state.copy(
                        timerState = sharedState.timerState,
                        sessionType = sharedState.sessionType,
                        timeRemaining = sharedState.timeRemaining,
                        totalTime = sharedState.totalTime,
                        completedSessions = sharedState.completedSessions,
                        sessionMode = sharedState.sessionMode,
                        isBoardConnected = sharedState.isBoardConnected
                    )
                }

                // 会话结束时处理休息
                if (sharedState.timerState == TimerState.FINISHED) {
                    handleSessionFinished()
                }
            }
        }
    }

    /**
     * 启动 EdgeFocus 模式会话
     * 会尝试连接设备，但不强制要求连接
     */
    fun startEdgeFocusSession() {
        currentSessionId = "session_${System.currentTimeMillis()}"
        startAppMonitor()
        timerStateHolder.startEdgeFocusSession(workDurationMinutes)
    }

    /**
     * 启动本地模式会话
     * 不需要连接设备
     */
    fun startLocalSession() {
        currentSessionId = "session_${System.currentTimeMillis()}"
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
        if (state == TimerState.RUNNING || state == TimerState.BREAK) {
            timerStateHolder.pauseSession()
        }
    }

    fun stopTimer() {
        stopAppMonitor()

        // 本地模式结束后，尝试同步记录给设备
        if (_uiState.value.sessionMode == SessionMode.LOCAL && timerStateHolder.isBoardConnected()) {
            viewModelScope.launch {
                val durationMinutes = timerStateHolder.getTargetDurationMinutes()
                timerStateHolder.syncSessionToBoard(durationMinutes, durationMinutes, 0)
            }
        }

        timerStateHolder.stopSession()
    }

    private fun handleSessionFinished() {
        val completedSessions = timerStateHolder.getCompletedSessions()
        timerStateHolder.startBreak(
            completedSessions = completedSessions,
            shortBreakMinutes = shortBreakMinutes,
            longBreakMinutes = longBreakMinutes,
            sessionsBeforeLongBreak = sessionsBeforeLongBreak
        )
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
