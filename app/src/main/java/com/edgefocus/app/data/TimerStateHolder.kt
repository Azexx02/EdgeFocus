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

enum class SessionMode {
    LOCAL,    // 本地模式：不与设备同步
    EDGEFOCUS // EdgeFocus模式：设备为核心，手机同步
}

data class SharedTimerState(
    val timerState: TimerState = TimerState.IDLE,
    val sessionType: SessionType = SessionType.WORK,
    val timeRemaining: Long = TimeUtils.minutesToMillis(25),
    val totalTime: Long = TimeUtils.minutesToMillis(25),
    val completedSessions: Int = 0,
    val targetDurationMinutes: Int = 25,
    val isBoardConnected: Boolean = false,
    val sessionMode: SessionMode = SessionMode.LOCAL,
    val isSyncingWithBoard: Boolean = false // 是否正在与设备同步
)

@Singleton
class TimerStateHolder @Inject constructor(
    private val bleManager: BleManager
) {
    companion object {
        private const val TAG = "TimerStateHolder"
        private const val LOCAL_TICK_INTERVAL = 1000L
        private const val BOARD_SYNC_INTERVAL = 2000L
        private const val CALIBRATION_THRESHOLD_MS = 5000L // 5秒差异阈值
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

                val currentState = _state.value

                // EdgeFocus模式下，连接后检查设备会话
                if (isConnected && currentState.sessionMode == SessionMode.EDGEFOCUS) {
                    if (currentState.timerState == TimerState.IDLE) {
                        // 空闲状态连接，检查设备是否有进行中的会话
                        checkBoardSession()
                    } else if (currentState.timerState == TimerState.RUNNING) {
                        // 会话运行中连接，启动同步
                        startBoardSync()
                    }
                }

                // 断开连接时停止同步任务（本地继续倒计时）
                if (!isConnected) {
                    stopBoardSync()
                }
            }
        }
    }

    // ==================== 会话控制 ====================

    /**
     * 启动 EdgeFocus 模式会话
     * 必须已连接设备才能调用
     */
    fun startEdgeFocusSession(targetDurationMinutes: Int) {
        if (!bleManager.isConnected()) {
            Log.w(TAG, "Cannot start EdgeFocus session: device not connected")
            return
        }

        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.RUNNING,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                targetDurationMinutes = targetDurationMinutes,
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = true
            )
        }

        // 启动本地倒计时（保底）
        startLocalCountdown()

        // 启动板端同步
        startBoardSync()

        // 通知板端开始会话
        scope.launch {
            bleManager.sessionControl("start", targetDurationMinutes)
        }

        Log.d(TAG, "EdgeFocus session started: duration=${targetDurationMinutes}min")
    }

    /**
     * 启动本地模式会话
     * 不需要连接设备
     */
    fun startLocalSession(targetDurationMinutes: Int) {
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.RUNNING,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                targetDurationMinutes = targetDurationMinutes,
                sessionMode = SessionMode.LOCAL,
                isSyncingWithBoard = false
            )
        }

        // 只启动本地倒计时
        startLocalCountdown()

        Log.d(TAG, "Local session started: duration=${targetDurationMinutes}min")
    }

    fun pauseSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        _state.update { it.copy(timerState = TimerState.PAUSED) }

        // EdgeFocus模式下通知板端
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            scope.launch { bleManager.sessionControl("pause") }
        }
    }

    fun resumeSession() {
        _state.update { it.copy(timerState = TimerState.RUNNING) }

        // 恢复本地倒计时
        startLocalCountdown()

        // EdgeFocus模式下恢复同步
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            startBoardSync()
            scope.launch { bleManager.sessionControl("resume") }
        }
    }

    fun stopSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        // EdgeFocus模式下通知板端
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            scope.launch { bleManager.sessionControl("stop") }
        }

        val targetDurationMinutes = _state.value.targetDurationMinutes
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.IDLE,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                isSyncingWithBoard = false
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
                timerState = TimerState.FINISHED,
                isSyncingWithBoard = false
            )
        }
    }

    /**
     * 同步本地会话记录给设备
     * 用于本地模式结束后将记录同步
     */
    suspend fun syncSessionToBoard(durationMinutes: Int, focusMinutes: Int, distractionCount: Int) {
        if (bleManager.isConnected()) {
            // 通过 phone_sync 上报会话记录
            val usage = com.edgefocus.app.data.remote.PhoneUsage(
                isInUse = false,
                appCategory = "learning",
                distractionLevel = 0,
                appName = "EdgeFocus",
                durationSec = durationMinutes * 60
            )
            bleManager.phoneSync(usage)
            Log.d(TAG, "Session synced to board: duration=${durationMinutes}min")
        }
    }

    // ==================== 检查设备会话 ====================

    private suspend fun checkBoardSession() {
        try {
            val result = bleManager.getCurrentState()
            result.onSuccess { data ->
                val json = String(data, Charsets.UTF_8)
                val jsonObj = JSONObject(json)
                val stateData = jsonObj.getJSONObject("data")

                val isActive = stateData.optBoolean("active", false)
                val remainingMs = stateData.optLong("remaining_ms", 0)
                val targetDurationMs = stateData.optLong("target_duration_ms", 0)
                val elapsedMs = stateData.optLong("elapsed_ms", 0)

                if (isActive && remainingMs > 0) {
                    // 设备有进行中的会话，同步
                    Log.d(TAG, "Board has active session, syncing: remaining=${remainingMs}ms")
                    _state.update {
                        it.copy(
                            timerState = TimerState.RUNNING,
                            sessionType = SessionType.WORK,
                            timeRemaining = remainingMs,
                            totalTime = if (targetDurationMs > 0) targetDurationMs else it.totalTime,
                            targetDurationMinutes = if (targetDurationMs > 0) (targetDurationMs / 60000).toInt() else it.targetDurationMinutes,
                            sessionMode = SessionMode.EDGEFOCUS,
                            isSyncingWithBoard = true
                        )
                    }
                    startLocalCountdown()
                    startBoardSync()
                } else {
                    // 设备空闲，等待用户操作
                    Log.d(TAG, "Board is idle, waiting for user action")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Check board session failed: ${e.message}")
        }
    }

    // ==================== 本地倒计时 ====================

    private fun startLocalCountdown() {
        localCountdownJob?.cancel()
        localCountdownJob = scope.launch {
            while (_state.value.timeRemaining > 0 &&
                   (_state.value.timerState == TimerState.RUNNING || _state.value.timerState == TimerState.BREAK)) {
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

    // ==================== 板端同步 ====================

    private fun startBoardSync() {
        boardSyncJob?.cancel()
        boardSyncJob = scope.launch {
            while (_state.value.timerState == TimerState.RUNNING &&
                   _state.value.sessionMode == SessionMode.EDGEFOCUS &&
                   bleManager.isConnected()) {
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
        _state.update { it.copy(isSyncingWithBoard = false) }
        // 本地继续倒计时
    }

    private fun calibrateFromBoard(jsonString: String) {
        try {
            val json = JSONObject(jsonString)
            val data = json.getJSONObject("data")

            val boardRemainingMs = data.optLong("remaining_ms", 0)
            val boardTargetDurationMs = data.optLong("target_duration_ms", 0)

            if (boardRemainingMs > 0 && boardTargetDurationMs > 0) {
                val localRemaining = _state.value.timeRemaining
                val diff = kotlin.math.abs(localRemaining - boardRemainingMs)

                // 差异超过阈值，用板端数据校准
                if (diff > CALIBRATION_THRESHOLD_MS) {
                    Log.d(TAG, "Calibrating: local=${localRemaining}ms, board=${boardRemainingMs}ms, diff=${diff}ms")
                    _state.update {
                        it.copy(
                            timeRemaining = boardRemainingMs,
                            totalTime = boardTargetDurationMs
                        )
                    }
                }

                // 板端报告时间到
                if (boardRemainingMs <= 0) {
                    completeSession()
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Calibrate failed: ${e.message}")
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
    fun getSessionMode(): SessionMode = _state.value.sessionMode
    fun isSyncingWithBoard(): Boolean = _state.value.isSyncingWithBoard
}
