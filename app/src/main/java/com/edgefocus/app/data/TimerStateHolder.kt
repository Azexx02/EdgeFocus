package com.edgefocus.app.data

import android.util.Log
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.util.TimeUtils
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

data class SharedTimerState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0,
    val targetDurationMinutes: Int = 25,
    val isBoardConnected: Boolean = false
)

@Singleton
class TimerStateHolder @Inject constructor(
    private val bleManager: BleManager
) {
    companion object {
        private const val TAG = "TimerStateHolder"
        private const val LOCAL_TICK_INTERVAL = 1000L // 1秒本地倒计时
        private const val BOARD_SYNC_INTERVAL = 2000L // 2秒从板端校准
    }

    private val _state = MutableStateFlow(SharedTimerState())
    val state: StateFlow<SharedTimerState> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var localCountdownJob: Job? = null
    private var boardSyncJob: Job? = null

    init {
        // 监听板端连接状态
        scope.launch {
            bleManager.connectionState.collect { connectionState ->
                val isConnected = connectionState == ConnectionState.CONNECTED
                _state.update { it.copy(isBoardConnected = isConnected) }

                // 会话运行中时，板端连接/断开时管理校准任务
                if (_state.value.timerState == TimerState.RUNNING) {
                    if (isConnected) {
                        startBoardSync()
                    } else {
                        stopBoardSync()
                    }
                }
            }
        }
    }

    // ==================== 会话控制 ====================

    fun startSession(targetDurationMinutes: Int) {
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.RUNNING,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                targetDurationMinutes = targetDurationMinutes
            )
        }

        // 始终启动本地倒计时
        startLocalCountdown()

        // 如果板端连接，启动板端校准
        if (bleManager.isConnected()) {
            startBoardSync()
        }

        Log.d(TAG, "Session started: duration=${targetDurationMinutes}min, boardConnected=${bleManager.isConnected()}")
    }

    fun pauseSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        _state.update { it.copy(timerState = TimerState.PAUSED) }
    }

    fun resumeSession() {
        _state.update { it.copy(timerState = TimerState.RUNNING) }

        // 恢复本地倒计时
        startLocalCountdown()

        // 如果板端连接，恢复校准
        if (bleManager.isConnected()) {
            startBoardSync()
        }
    }

    fun stopSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        val targetDurationMinutes = _state.value.targetDurationMinutes
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.IDLE,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs
            )
        }
    }

    fun startBreak(completedSessions: Int, shortBreakMinutes: Int, longBreakMinutes: Int, sessionsBeforeLongBreak: Int) {
        val isLongBreak = completedSessions % sessionsBeforeLongBreak == 0
        val breakDuration = if (isLongBreak) longBreakMinutes else shortBreakMinutes
        val breakType = if (isLongBreak) SessionType.LONG_BREAK else SessionType.SHORT_BREAK
        val breakDurationMs = TimeUtils.minutesToMillis(breakDuration)

        _state.update {
            it.copy(
                timerState = TimerState.BREAK,
                sessionType = breakType,
                timeRemaining = breakDurationMs,
                totalTime = breakDurationMs
            )
        }

        // 休息时间本地倒计时
        startLocalCountdown()
    }

    fun completeSession() {
        boardSyncJob?.cancel()
        localCountdownJob?.cancel()

        _state.update {
            it.copy(
                completedSessions = it.completedSessions + 1,
                timerState = TimerState.FINISHED
            )
        }
    }

    // ==================== 本地倒计时 ====================

    private fun startLocalCountdown() {
        localCountdownJob?.cancel()
        localCountdownJob = scope.launch {
            while (_state.value.timeRemaining > 0 && _state.value.timerState == TimerState.RUNNING) {
                delay(LOCAL_TICK_INTERVAL)
                _state.update { currentState ->
                    val newTime = currentState.timeRemaining - LOCAL_TICK_INTERVAL
                    currentState.copy(timeRemaining = if (newTime <= 0) 0 else newTime)
                }
            }

            // 时间到
            if (_state.value.timeRemaining <= 0 && _state.value.timerState == TimerState.RUNNING) {
                completeSession()
            }
        }
    }

    // ==================== 板端校准 ====================

    private fun startBoardSync() {
        boardSyncJob?.cancel()
        boardSyncJob = scope.launch {
            while (_state.value.timerState == TimerState.RUNNING && bleManager.isConnected()) {
                try {
                    val result = bleManager.getCurrentState()
                    result.onSuccess { data ->
                        calibrateFromBoard(String(data, Charsets.UTF_8))
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Board sync failed: ${e.message}")
                }
                delay(BOARD_SYNC_INTERVAL)
            }
        }
    }

    private fun stopBoardSync() {
        boardSyncJob?.cancel()
        // 本地继续倒计时，不做任何处理
    }

    private fun calibrateFromBoard(jsonString: String) {
        try {
            val json = JSONObject(jsonString)
            val data = json.getJSONObject("data")

            val boardRemainingMs = data.optLong("remaining_ms", 0)
            val boardTargetDurationMs = data.optLong("target_duration_ms", 0)
            val boardElapsedMs = data.optLong("elapsed_ms", 0)

            if (boardRemainingMs > 0 && boardTargetDurationMs > 0) {
                val localRemaining = _state.value.timeRemaining

                // 计算本地和板端的差异
                val diff = kotlin.math.abs(localRemaining - boardRemainingMs)

                // 如果差异超过5秒，使用板端数据校准
                if (diff > 5000) {
                    Log.d(TAG, "Calibrating from board: local=${localRemaining}ms, board=${boardRemainingMs}ms, diff=${diff}ms")
                    _state.update {
                        it.copy(
                            timeRemaining = boardRemainingMs,
                            totalTime = boardTargetDurationMs
                        )
                    }
                }

                // 如果板端报告时间到
                if (boardRemainingMs <= 0) {
                    completeSession()
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Calibrate from board failed: ${e.message}")
        }
    }

    // ==================== 板端命令 ====================

    suspend fun sendStartToBoard(targetDurationMinutes: Int) {
        if (bleManager.isConnected()) {
            bleManager.sessionControl("start", targetDurationMinutes)
        }
    }

    suspend fun sendPauseToBoard() {
        if (bleManager.isConnected()) {
            bleManager.sessionControl("pause")
        }
    }

    suspend fun sendResumeToBoard() {
        if (bleManager.isConnected()) {
            bleManager.sessionControl("resume")
        }
    }

    suspend fun sendStopToBoard() {
        if (bleManager.isConnected()) {
            bleManager.sessionControl("stop")
        }
    }

    // ==================== 状态查询 ====================

    fun getCurrentTimerState(): TimerState = _state.value.timerState

    fun getCurrentSessionType(): SessionType = _state.value.sessionType

    fun getRemainingTimeMillis(): Long = _state.value.timeRemaining

    fun getTotalTimeMillis(): Long = _state.value.totalTime

    fun getTargetDurationMinutes(): Int = _state.value.targetDurationMinutes

    fun getCompletedSessions(): Int = _state.value.completedSessions

    fun isBoardConnected(): Boolean = _state.value.isBoardConnected
}
