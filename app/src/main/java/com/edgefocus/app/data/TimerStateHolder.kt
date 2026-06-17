package com.edgefocus.app.data

import android.util.Log
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.remote.AppEventLog
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.PushEventHandler
import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.FocusState
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
    val isSyncingWithBoard: Boolean = false,
    val distractionCount: Int = 0,
    val reminderCount: Int = 0,
    val phoneDistractionCount: Int = 0,
    // 板端推送的会话结束数据
    val boardTotalDuration: Int? = null,
    val boardFocusDuration: Int? = null,
    val boardSessionDistractionCount: Int? = null,
    val boardSessionReminderCount: Int? = null
)

@Singleton
class TimerStateHolder @Inject constructor(
    private val bleManager: BleManager,
    private val appEventLog: AppEventLog,
    private val settingsDao: SettingsDao
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
        // 监听设置变化，实时更新模式
        scope.launch {
            settingsDao.getSettings().collect { settings ->
                settings?.let {
                    val mode = if (it.useEdgeFocusBoard) SessionMode.EDGEFOCUS else SessionMode.LOCAL
                    if (_state.value.sessionMode != mode && _state.value.timerState == TimerState.IDLE) {
                        _state.update { state -> state.copy(sessionMode = mode) }
                        // 设置变更后，如果已连接且切换到 EdgeFocus，检查板端会话
                        if (mode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
                            scope.launch { checkBoardSession() }
                        }
                    }
                }
            }
        }

        // 监听板端连接状态
        scope.launch {
            bleManager.connectionState.collect { connectionState ->
                val isConnected = connectionState == ConnectionState.CONNECTED
                _state.update { it.copy(isBoardConnected = isConnected) }

                // 连接时，根据设置决定模式
                if (isConnected && _state.value.timerState == TimerState.IDLE) {
                    val settings = settingsDao.getSettingsOnce()
                    val mode = if (settings?.useEdgeFocusBoard == true) SessionMode.EDGEFOCUS else SessionMode.LOCAL
                    _state.update { it.copy(sessionMode = mode) }
                    appEventLog.log("BLE_CONNECT", "模式: $mode")

                    // EdgeFocus模式下，检查板端是否有进行中的会话
                    if (mode == SessionMode.EDGEFOCUS) {
                        checkBoardSession()
                    }
                }

                // 运行中连接，启动同步
                if (isConnected && _state.value.sessionMode == SessionMode.EDGEFOCUS
                    && _state.value.timerState == TimerState.RUNNING) {
                    startBoardSync()
                }

                // 断开连接时停止同步任务（本地继续倒计时）
                if (!isConnected) {
                    stopBoardSync()
                }
            }
        }

        // 场景3：监听板端推送事件（板端主动发起会话时通知手机）
        bleManager.setPushEventHandler(object : PushEventHandler {
            override fun onPushState(state: FocusState) {
                // 板端状态变化，如果板端在运行且手机空闲，同步板端时间
                if (state.state == com.edgefocus.app.domain.model.TimerState.RUNNING
                    && _state.value.timerState == TimerState.IDLE
                    && _state.value.sessionMode == SessionMode.EDGEFOCUS) {
                    appEventLog.log("PUSH_STATE", "板端进入专注，自动同步")
                    scope.launch { checkBoardSession() }
                }
            }
            override fun onPushAlert(alert: AlertInfo) {}
            override fun onPushSessionEnd(summary: String) {
                appEventLog.log("PUSH_SESSION_END", summary)
                try {
                    val json = JSONObject(summary)
                    _state.update {
                        it.copy(
                            timerState = TimerState.FINISHED,
                            isSyncingWithBoard = false,
                            boardTotalDuration = json.optInt("total_duration", 0),
                            boardFocusDuration = json.optInt("focus_duration", 0),
                            boardSessionDistractionCount = json.optInt("distraction_count", 0),
                            boardSessionReminderCount = json.optInt("reminder_count", 0)
                        )
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to parse push_session_end: ${e.message}")
                    _state.update { it.copy(timerState = TimerState.FINISHED, isSyncingWithBoard = false) }
                }
            }
            override fun onPushTimerComplete(timerComplete: com.edgefocus.app.data.remote.TimerComplete) {
                appEventLog.log("PUSH_TIMER_COMPLETE", "distraction=${timerComplete.distractionCount}, reminder=${timerComplete.reminderCount}")
                _state.update {
                    it.copy(
                        distractionCount = timerComplete.distractionCount,
                        reminderCount = timerComplete.reminderCount
                    )
                }
            }
        })
    }

    // ==================== 会话控制 ====================

    /**
     * 启动 EdgeFocus 模式会话
     * 必须已连接设备才能调用
     * 
     * 原则：EdgeFocus 设备是核心，手机是遥控器
     * 逻辑：先通知板端开始，等待板端响应后才设置状态
     * 本地计时只是保底，防止中途蓝牙断连
     */
    fun startEdgeFocusSession(targetDurationMinutes: Int) {
        if (!bleManager.isConnected()) {
            Log.w(TAG, "Cannot start EdgeFocus session: device not connected")
            return
        }

        // 先设置基础状态（不启动倒计时）
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)
        _state.update {
            it.copy(
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = true
            )
        }

        // 通知板端开始会话，等待响应
        scope.launch {
            try {
                appEventLog.log("SESSION_CONTROL", "start, duration=${targetDurationMinutes}min")
                val result = bleManager.sessionControl("start", targetDurationMinutes)
                result.onSuccess { data ->
                    // 板端响应成功
                    val json = String(data, Charsets.UTF_8)
                    val jsonObj = JSONObject(json)
                    val responseData = jsonObj.optJSONObject("data")
                    
                    val boardRemainingMs = responseData?.optLong("remaining_ms", 0) ?: 0
                    val boardTargetMs = responseData?.optLong("target_duration_ms", 0) ?: 0
                    
                    if (boardRemainingMs > 0 && boardTargetMs > 0) {
                        // 板端有时间数据，跟随板端
                        Log.d(TAG, "Board responded with time: remaining=${boardRemainingMs}ms, target=${boardTargetMs}ms")
                        _state.update {
                            it.copy(
                                timerState = TimerState.RUNNING,
                                sessionType = SessionType.WORK,
                                timeRemaining = boardRemainingMs,
                                totalTime = boardTargetMs,
                                targetDurationMinutes = targetDurationMinutes
                            )
                        }
                    } else {
                        // 板端无时间数据，使用本地时间
                        Log.d(TAG, "Board responded without time, using local time")
                        _state.update {
                            it.copy(
                                timerState = TimerState.RUNNING,
                                sessionType = SessionType.WORK,
                                timeRemaining = totalTimeMs,
                                totalTime = totalTimeMs,
                                targetDurationMinutes = targetDurationMinutes
                            )
                        }
                    }
                    
                    // 启动板端同步和本地倒计时
                    startBoardSync()
                    startLocalCountdown()
                    
                }.onFailure { error ->
                    Log.e(TAG, "Failed to start session on board: ${error.message}")
                    // 板端通信失败，回退到空闲状态
                    _state.update {
                        it.copy(
                            timerState = TimerState.IDLE,
                            sessionMode = SessionMode.LOCAL,
                            isSyncingWithBoard = false
                        )
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error starting session on board: ${e.message}")
                // 异常，回退到空闲状态
                _state.update {
                    it.copy(
                        timerState = TimerState.IDLE,
                        sessionMode = SessionMode.LOCAL,
                        isSyncingWithBoard = false
                    )
                }
            }
        }

        Log.d(TAG, "EdgeFocus session starting: waiting for board response...")
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
            appEventLog.log("SESSION_CONTROL", "pause")
            scope.launch { bleManager.sessionControl("pause") }
        }
    }

    fun resumeSession() {
        // EdgeFocus模式下，先通知板端，等响应后再恢复
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "resume")
            scope.launch {
                try {
                    val result = bleManager.sessionControl("resume")
                    result.onSuccess { data ->
                        // 板端响应成功，检查 success 字段
                        val json = String(data, Charsets.UTF_8)
                        val jsonObj = JSONObject(json)
                        val responseData = jsonObj.optJSONObject("data")
                        val success = responseData?.optBoolean("success", false) ?: false
                        
                        if (success) {
                            // 板端恢复成功，恢复本地倒计时和板端同步
                            Log.d(TAG, "Board resume successful")
                            _state.update { it.copy(timerState = TimerState.RUNNING) }
                            startLocalCountdown()
                            startBoardSync()
                        } else {
                            // 板端恢复失败
                            val message = responseData?.optString("message", "unknown error") ?: "unknown error"
                            Log.e(TAG, "Board resume failed: $message")
                        }
                    }.onFailure { error ->
                        Log.e(TAG, "Failed to resume session on board: ${error.message}")
                        // 板端通信失败，不恢复
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Error resuming session on board: ${e.message}")
                }
            }
        } else {
            // 本地模式，直接恢复
            _state.update { it.copy(timerState = TimerState.RUNNING) }
            startLocalCountdown()
        }
    }

    fun stopSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        // EdgeFocus模式下通知板端
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "stop")
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
    suspend fun syncSessionToBoard(durationMinutes: Int) {
        if (bleManager.isConnected()) {
            val usage = com.edgefocus.app.data.remote.PhoneUsage(
                isInUse = false,
                appCategory = "other",
                distractionLevel = 3,
                appName = "local_session_sync",
                durationSec = durationMinutes * 60
            )
            bleManager.phoneSync(usage)
            Log.d(TAG, "Local session synced to board: duration=${durationMinutes}min")
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
                   (_state.value.timerState == TimerState.RUNNING)) {
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
    fun getPhoneDistractionCount(): Int = _state.value.phoneDistractionCount

    fun getBoardSessionDistractionCount(): Int = _state.value.boardSessionDistractionCount ?: 0
    fun getBoardSessionReminderCount(): Int = _state.value.boardSessionReminderCount ?: 0

    fun incrementPhoneDistraction() {
        _state.update { it.copy(phoneDistractionCount = it.phoneDistractionCount + 1) }
    }
}
