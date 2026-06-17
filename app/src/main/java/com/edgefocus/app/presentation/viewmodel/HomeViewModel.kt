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
    private var hasSavedCurrentSession = false
    private var isAppMonitorRunning = false

    init {
        // 鍔犺浇璁剧疆
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let {
                    workDurationMinutes = it.pomodoroDuration

                    // 绌洪棽鐘舵€佹椂鏇存柊鏄剧ず鏃堕棿
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

        // 浠?TimerStateHolder 鍚屾鐘舵€?
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

                // 鏉跨鍚屾鍚姩浼氳瘽鏃讹紝鑷姩鍚姩 AppMonitorService
                if (prevTimerState == TimerState.IDLE && sharedState.timerState == TimerState.RUNNING
                    && sharedState.sessionMode == SessionMode.EDGEFOCUS) {
                    currentSessionId = sharedState.sessionId ?: currentSessionId ?: "session_${System.currentTimeMillis()}"
                    sessionStartTime = sharedState.sessionStartTime.takeIf { it > 0L }
                        ?: sessionStartTime.takeIf { it > 0L }
                        ?: System.currentTimeMillis()
                    hasSavedCurrentSession = false
                    startAppMonitor()
                }

                if (prevTimerState != TimerState.FINISHED && sharedState.timerState == TimerState.FINISHED) {
                    val elapsedSeconds = ((sharedState.totalTime - sharedState.timeRemaining) / 1000).toInt()
                    handleSessionFinished()
                    stopAppMonitor()
                    if (sharedState.sessionMode == SessionMode.LOCAL && sharedState.isBoardConnected) {
                        syncLocalSessionToBoard(elapsedSeconds)
                    }
                }
                prevTimerState = sharedState.timerState
            }
        }
    }

    /**
     * 鍚姩 EdgeFocus 妯″紡浼氳瘽
     * 蹇呴』宸茶繛鎺ヨ澶?
     */
    fun startEdgeFocusSession() {
        if (!timerStateHolder.isBoardConnected()) {
            return
        }

        currentSessionId = "session_${System.currentTimeMillis()}"
        sessionStartTime = System.currentTimeMillis()
        hasSavedCurrentSession = false
        timerStateHolder.startEdgeFocusSession(workDurationMinutes, currentSessionId!!, sessionStartTime)
    }

    /**
     * 鍚姩鏈湴妯″紡浼氳瘽
     * 涓嶉渶瑕佽繛鎺ヨ澶?
     */
    fun startLocalSession() {
        currentSessionId = "session_${System.currentTimeMillis()}"
        sessionStartTime = System.currentTimeMillis()
        hasSavedCurrentSession = false
        startAppMonitor()
        timerStateHolder.startLocalSession(workDurationMinutes, currentSessionId!!, sessionStartTime)
    }

    /**
     * 鏍规嵁璁剧疆鑷姩閫夋嫨妯″紡鍚姩
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
        val isLocalMode = _uiState.value.sessionMode == SessionMode.LOCAL

        // 鏈湴妯″紡鎵嬪姩鍋滄鏃剁洿鎺ヤ繚瀛橈紱EdgeFocus 妯″紡绛夊緟鏉跨 push_session_end
        if (isLocalMode && _uiState.value.timerState == TimerState.RUNNING && sessionStartTime > 0) {
            handleSessionFinished()
            stopAppMonitor()
        } else if (!isLocalMode) {
            stopAppMonitor()
        }

        // Local mode: sync finished record to board when a board is connected.
        if (isLocalMode && timerStateHolder.isBoardConnected()) {
            val elapsedSeconds = ((_uiState.value.totalTime - _uiState.value.timeRemaining) / 1000).toInt()
            syncLocalSessionToBoard(elapsedSeconds)
        }

        timerStateHolder.stopSession()
    }

    private fun handleSessionFinished() {
        if (hasSavedCurrentSession) return

        val ui = _uiState.value
        val isEdgeFocus = ui.sessionMode == SessionMode.EDGEFOCUS
        val hasBoardReport = timerStateHolder.hasBoardSessionReport()
        val sessionId = currentSessionId ?: return

        // EdgeFocus 妯″紡锛氬鏋滄湁鏉跨鏁版嵁灏变繚瀛橈紝鍚﹀垯绛?push_session_end
        // Local mode saves immediately; EdgeFocus waits until the board report marks the session finished.
        if (!isEdgeFocus || ui.timerState == TimerState.FINISHED) {
            val elapsedSeconds = if (isEdgeFocus && timerStateHolder.getBoardTotalDuration() > 0) {
                timerStateHolder.getBoardTotalDuration()
            } else {
                ((ui.totalTime - ui.timeRemaining) / 1000).toInt()
            }
            val focusSeconds = if (isEdgeFocus && timerStateHolder.getBoardFocusDuration() > 0) {
                timerStateHolder.getBoardFocusDuration()
            } else {
                elapsedSeconds
            }
            viewModelScope.launch {
                val session = Session(
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
                        ui.phoneDistractionCount
                    },
                    feedback = null,
                    boardSessionId = if (isEdgeFocus) sessionId else null,
                    source = if (isEdgeFocus) "EDGEFOCUS" else "LOCAL",
                    synced = isEdgeFocus && hasBoardReport
                )
                sessionRepository.insertSession(session)
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

    private fun syncLocalSessionToBoard(elapsedSeconds: Int) {
        viewModelScope.launch {
            timerStateHolder.syncSessionToBoard(elapsedSeconds.coerceAtLeast(1))
        }
    }

    override fun onCleared() {
        super.onCleared()
        stopAppMonitor()
    }
}
