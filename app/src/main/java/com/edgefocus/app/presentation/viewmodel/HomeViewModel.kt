package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
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
    val completedSessions: Int = 0
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

                    // 如果是空闲状态，更新显示时间
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
                        completedSessions = sharedState.completedSessions
                    )
                }

                // 检查会话是否结束，启动休息
                if (sharedState.timerState == TimerState.FINISHED) {
                    handleSessionFinished()
                }
            }
        }
    }

    fun startTimer(useBoard: Boolean = true) {
        val currentState = _uiState.value.timerState

        if (currentState == TimerState.IDLE || currentState == TimerState.FINISHED) {
            // 开始新会话
            currentSessionId = "session_${System.currentTimeMillis()}"
            startAppMonitor()

            // 通过 TimerStateHolder 启动会话
            timerStateHolder.startSession(workDurationMinutes, useBoard)

            // 如果连接板端，发送开始命令
            if (useBoard && timerStateHolder.isBoardConnected()) {
                viewModelScope.launch {
                    timerStateHolder.sendStartToBoard(workDurationMinutes)
                }
            }
        } else if (currentState == TimerState.PAUSED) {
            // 恢复会话
            timerStateHolder.resumeSession()

            // 如果连接板端，发送恢复命令
            if (timerStateHolder.isBoardSession()) {
                viewModelScope.launch {
                    timerStateHolder.sendResumeToBoard()
                }
            }
        }
    }

    fun pauseTimer() {
        val state = _uiState.value.timerState
        if (state == TimerState.RUNNING || state == TimerState.BREAK) {
            timerStateHolder.pauseSession()

            // 如果连接板端，发送暂停命令
            if (timerStateHolder.isBoardSession()) {
                viewModelScope.launch {
                    timerStateHolder.sendPauseToBoard()
                }
            }
        }
    }

    fun stopTimer() {
        stopAppMonitor()

        // 如果连接板端，发送停止命令
        if (timerStateHolder.isBoardSession()) {
            viewModelScope.launch {
                timerStateHolder.sendStopToBoard()
            }
        }

        timerStateHolder.stopSession()
    }

    private fun handleSessionFinished() {
        val completedSessions = timerStateHolder.getCompletedSessions()

        // 启动休息
        timerStateHolder.startBreak(
            completedSessions = completedSessions,
            shortBreakMinutes = shortBreakMinutes,
            longBreakMinutes = longBreakMinutes,
            sessionsBeforeLongBreak = sessionsBeforeLongBreak
        )
    }

    // 获取当前会话的目标时长（分钟）
    fun getTargetDurationMinutes(): Int {
        return timerStateHolder.getTargetDurationMinutes()
    }

    // 获取当前会话的剩余时间（毫秒）
    fun getRemainingTimeMillis(): Long {
        return timerStateHolder.getRemainingTimeMillis()
    }

    // 获取当前会话的总时长（毫秒）
    fun getTotalTimeMillis(): Long {
        return timerStateHolder.getTotalTimeMillis()
    }

    // 获取当前会话状态
    fun getCurrentTimerState(): TimerState {
        return timerStateHolder.getCurrentTimerState()
    }

    // 获取当前会话类型
    fun getCurrentSessionType(): SessionType {
        return timerStateHolder.getCurrentSessionType()
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
