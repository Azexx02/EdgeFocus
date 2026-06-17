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
    LOCAL,    // 鏈湴妯″紡锛氫笉涓庤澶囧悓姝?
    EDGEFOCUS // EdgeFocus妯″紡锛氳澶囦负鏍稿績锛屾墜鏈哄悓姝?
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
    val sessionId: String? = null,
    val sessionStartTime: Long = 0L,
    // 鏉跨鎺ㄩ€佺殑浼氳瘽缁撴潫鏁版嵁
    val boardTotalDuration: Int? = null,
    val boardFocusDuration: Int? = null,
    val boardSessionDistractionCount: Int? = null,
    val boardSessionReminderCount: Int? = null,
    val boardSessionPhoneDistractionCount: Int? = null
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
        private const val CALIBRATION_THRESHOLD_MS = 5000L // 5绉掑樊寮傞槇鍊?
        private const val MANUAL_STOP_REPORT_TIMEOUT_MS = 10_000L
    }

    private val _state = MutableStateFlow(SharedTimerState())
    val state: StateFlow<SharedTimerState> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var localCountdownJob: Job? = null
    private var boardSyncJob: Job? = null

    init {
        // 鐩戝惉璁剧疆鍙樺寲锛屽疄鏃舵洿鏂版ā寮?
        scope.launch {
            settingsDao.getSettings().collect { settings ->
                settings?.let {
                    val mode = if (it.useEdgeFocusBoard) SessionMode.EDGEFOCUS else SessionMode.LOCAL
                    if (_state.value.sessionMode != mode && _state.value.timerState == TimerState.IDLE) {
                        _state.update { state -> state.copy(sessionMode = mode) }
                        // 璁剧疆鍙樻洿鍚庯紝濡傛灉宸茶繛鎺ヤ笖鍒囨崲鍒?EdgeFocus锛屾鏌ユ澘绔細璇?
                        if (mode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
                            scope.launch { checkBoardSession() }
                        }
                    }
                }
            }
        }

        // 鐩戝惉鏉跨杩炴帴鐘舵€?
        scope.launch {
            bleManager.connectionState.collect { connectionState ->
                val isConnected = connectionState == ConnectionState.CONNECTED
                _state.update { it.copy(isBoardConnected = isConnected) }

                // 杩炴帴鏃讹紝鏍规嵁璁剧疆鍐冲畾妯″紡
                if (isConnected && _state.value.timerState == TimerState.IDLE) {
                    val settings = settingsDao.getSettingsOnce()
                    val mode = if (settings?.useEdgeFocusBoard == true) SessionMode.EDGEFOCUS else SessionMode.LOCAL
                    _state.update { it.copy(sessionMode = mode) }
                    appEventLog.log("BLE_CONNECT", "妯″紡: $mode")

                    // EdgeFocus妯″紡涓嬶紝妫€鏌ユ澘绔槸鍚︽湁杩涜涓殑浼氳瘽
                    if (mode == SessionMode.EDGEFOCUS) {
                        checkBoardSession()
                    }
                }

                // 杩愯涓繛鎺ワ紝鍚姩鍚屾
                if (isConnected && _state.value.sessionMode == SessionMode.EDGEFOCUS
                    && _state.value.timerState == TimerState.RUNNING) {
                    startBoardSync()
                }

                // 鏂紑杩炴帴鏃跺仠姝㈠悓姝ヤ换鍔★紙鏈湴缁х画鍊掕鏃讹級
                if (!isConnected) {
                    stopBoardSync()
                }
            }
        }

        // 鍦烘櫙3锛氱洃鍚澘绔帹閫佷簨浠讹紙鏉跨涓诲姩鍙戣捣浼氳瘽鏃堕€氱煡鎵嬫満锛?
        bleManager.setPushEventHandler(object : PushEventHandler {
            override fun onPushState(state: FocusState) {
                // 鏉跨鐘舵€佸彉鍖栵紝濡傛灉鏉跨鍦ㄨ繍琛屼笖鎵嬫満绌洪棽锛屽悓姝ユ澘绔椂闂?
                if (state.state == com.edgefocus.app.domain.model.TimerState.RUNNING
                    && _state.value.timerState == TimerState.IDLE
                    && _state.value.sessionMode == SessionMode.EDGEFOCUS) {
                    appEventLog.log("PUSH_STATE", "鏉跨杩涘叆涓撴敞锛岃嚜鍔ㄥ悓姝?)
                    scope.launch { checkBoardSession() }
                }
            }
            override fun onPushAlert(alert: AlertInfo) {}
            override fun onPushSessionEnd(summary: String) {
                appEventLog.log("PUSH_SESSION_END", summary)
                try {
                    val json = JSONObject(summary)
                    finishFromBoardReport(json)
                } catch (e: Exception) {
                    Log.e(TAG, "Failed to parse push_session_end: ${e.message}")
                    _state.update { it.copy(timerState = TimerState.FINISHED, isSyncingWithBoard = false) }
                }
            }
            override fun onPushTimerComplete(timerComplete: com.edgefocus.app.data.remote.TimerComplete) {
                appEventLog.log("PUSH_TIMER_COMPLETE", "distraction=${timerComplete.distractionCount}, reminder=${timerComplete.reminderCount}")
                _state.update {
                    val completedSessions = if (it.timerState == TimerState.FINISHED) {
                        it.completedSessions
                    } else {
                        it.completedSessions + 1
                    }
                    it.copy(
                        timerState = TimerState.FINISHED,
                        completedSessions = completedSessions,
                        isSyncingWithBoard = false,
                        timeRemaining = 0L,
                        boardTotalDuration = (timerComplete.targetDurationMs / 1000).toInt(),
                        boardFocusDuration = (timerComplete.elapsedMs / 1000).toInt(),
                        distractionCount = timerComplete.distractionCount,
                        reminderCount = timerComplete.reminderCount,
                        boardSessionDistractionCount = timerComplete.distractionCount,
                        boardSessionReminderCount = timerComplete.reminderCount
                    )
                }
            }
        })
    }

    // ==================== 浼氳瘽鎺у埗 ====================

    /**
     * 鍚姩 EdgeFocus 妯″紡浼氳瘽
     * 蹇呴』宸茶繛鎺ヨ澶囨墠鑳借皟鐢?
     * 
     * 鍘熷垯锛欵dgeFocus 璁惧鏄牳蹇冿紝鎵嬫満鏄仴鎺у櫒
     * 閫昏緫锛氬厛閫氱煡鏉跨寮€濮嬶紝绛夊緟鏉跨鍝嶅簲鍚庢墠璁剧疆鐘舵€?
     * 鏈湴璁℃椂鍙槸淇濆簳锛岄槻姝腑閫旇摑鐗欐柇杩?
     */
    fun startEdgeFocusSession(
        targetDurationMinutes: Int,
        sessionId: String = "session_${System.currentTimeMillis()}",
        sessionStartTime: Long = System.currentTimeMillis()
    ) {
        if (!bleManager.isConnected()) {
            Log.w(TAG, "Cannot start EdgeFocus session: device not connected")
            return
        }

        // 鍏堣缃熀纭€鐘舵€侊紙涓嶅惎鍔ㄥ€掕鏃讹級
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)
        _state.update {
            it.copy(
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = true,
                distractionCount = 0,
                reminderCount = 0,
                phoneDistractionCount = 0,
                sessionId = sessionId,
                sessionStartTime = sessionStartTime,
                boardTotalDuration = null,
                boardFocusDuration = null,
                boardSessionDistractionCount = null,
                boardSessionReminderCount = null,
                boardSessionPhoneDistractionCount = null
            )
        }

        // 閫氱煡鏉跨寮€濮嬩細璇濓紝绛夊緟鍝嶅簲
        scope.launch {
            try {
                appEventLog.log("SESSION_CONTROL", "start, duration=${targetDurationMinutes}min")
                val result = bleManager.sessionControl("start", targetDurationMinutes)
                result.onSuccess { data ->
                    // 鏉跨鍝嶅簲鎴愬姛
                    val json = String(data, Charsets.UTF_8)
                    val jsonObj = JSONObject(json)
                    val responseData = jsonObj.optJSONObject("data")
                    val success = responseData?.optBoolean("success", true) ?: true

                    if (!success) {
                        Log.e(TAG, "Board rejected session start")
                        _state.update {
                            it.copy(
                                timerState = TimerState.IDLE,
                                isSyncingWithBoard = false
                            )
                        }
                        return@onSuccess
                    }
                    
                    val boardRemainingMs = responseData?.optLong("remaining_ms", 0) ?: 0
                    val boardTargetMs = responseData?.optLong("target_duration_ms", 0) ?: 0
                    
                    if (boardRemainingMs > 0 && boardTargetMs > 0) {
                        // 鏉跨鏈夋椂闂存暟鎹紝璺熼殢鏉跨
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
                        // 鏉跨鏃犳椂闂存暟鎹紝浣跨敤鏈湴鏃堕棿
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
                    
                    // 鍚姩鏉跨鍚屾鍜屾湰鍦板€掕鏃?
                    startBoardSync()
                    startLocalCountdown()
                    
                }.onFailure { error ->
                    Log.e(TAG, "Failed to start session on board: ${error.message}")
                    // 鏉跨閫氫俊澶辫触锛屽洖閫€鍒扮┖闂茬姸鎬?
                    _state.update {
                        it.copy(
                            timerState = TimerState.IDLE,
                            isSyncingWithBoard = false
                        )
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error starting session on board: ${e.message}")
                // 寮傚父锛屽洖閫€鍒扮┖闂茬姸鎬?
                _state.update {
                    it.copy(
                        timerState = TimerState.IDLE,
                        isSyncingWithBoard = false
                    )
                }
            }
        }

        Log.d(TAG, "EdgeFocus session starting: waiting for board response...")
    }

    /**
     * 鍚姩鏈湴妯″紡浼氳瘽
     * 涓嶉渶瑕佽繛鎺ヨ澶?
     */
    fun startLocalSession(
        targetDurationMinutes: Int,
        sessionId: String = "session_${System.currentTimeMillis()}",
        sessionStartTime: Long = System.currentTimeMillis()
    ) {
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)

        _state.update {
            it.copy(
                timerState = TimerState.RUNNING,
                sessionType = SessionType.WORK,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                targetDurationMinutes = targetDurationMinutes,
                sessionMode = SessionMode.LOCAL,
                isSyncingWithBoard = false,
                distractionCount = 0,
                reminderCount = 0,
                phoneDistractionCount = 0,
                sessionId = sessionId,
                sessionStartTime = sessionStartTime,
                boardTotalDuration = null,
                boardFocusDuration = null,
                boardSessionDistractionCount = null,
                boardSessionReminderCount = null,
                boardSessionPhoneDistractionCount = null
            )
        }

        // 鍙惎鍔ㄦ湰鍦板€掕鏃?
        startLocalCountdown()

        Log.d(TAG, "Local session started: duration=${targetDurationMinutes}min")
    }

    fun pauseSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        _state.update { it.copy(timerState = TimerState.PAUSED) }

        // EdgeFocus妯″紡涓嬮€氱煡鏉跨
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "pause")
            scope.launch { bleManager.sessionControl("pause") }
        }
    }

    fun resumeSession() {
        // EdgeFocus妯″紡涓嬶紝鍏堥€氱煡鏉跨锛岀瓑鍝嶅簲鍚庡啀鎭㈠
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "resume")
            scope.launch {
                try {
                    val result = bleManager.sessionControl("resume")
                    result.onSuccess { data ->
                        // 鏉跨鍝嶅簲鎴愬姛锛屾鏌?success 瀛楁
                        val json = String(data, Charsets.UTF_8)
                        val jsonObj = JSONObject(json)
                        val responseData = jsonObj.optJSONObject("data")
                        val success = responseData?.optBoolean("success", false) ?: false
                        
                        if (success) {
                            // 鏉跨鎭㈠鎴愬姛锛屾仮澶嶆湰鍦板€掕鏃跺拰鏉跨鍚屾
                            Log.d(TAG, "Board resume successful")
                            _state.update { it.copy(timerState = TimerState.RUNNING) }
                            startLocalCountdown()
                            startBoardSync()
                        } else {
                            // 鏉跨鎭㈠澶辫触
                            val message = responseData?.optString("message", "unknown error") ?: "unknown error"
                            Log.e(TAG, "Board resume failed: $message")
                        }
                    }.onFailure { error ->
                        Log.e(TAG, "Failed to resume session on board: ${error.message}")
                        // 鏉跨閫氫俊澶辫触锛屼笉鎭㈠
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Error resuming session on board: ${e.message}")
                }
            }
        } else {
            // 鏈湴妯″紡锛岀洿鎺ユ仮澶?
            _state.update { it.copy(timerState = TimerState.RUNNING) }
            startLocalCountdown()
        }
    }

    fun stopSession() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        // EdgeFocus妯″紡涓嬮€氱煡鏉跨
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "stop")
            _state.update { it.copy(isSyncingWithBoard = false) }
            scope.launch {
                val result = bleManager.sessionControl("stop")
                result.onSuccess { data ->
                    var finishedByReport = false
                    try {
                        val response = JSONObject(String(data, Charsets.UTF_8))
                        val report = response.optJSONObject("data")
                        if (report != null && report.has("total_duration")) {
                            finishFromBoardReport(report)
                            finishedByReport = true
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "Failed to parse stop response: ${e.message}")
                    }
                    if (!finishedByReport) {
                        delay(MANUAL_STOP_REPORT_TIMEOUT_MS)
                        if (_state.value.timerState != TimerState.FINISHED) {
                            Log.w(TAG, "Board stop report timeout, saving with local fallback")
                            completeSession()
                        }
                    }
                }.onFailure { error ->
                    Log.e(TAG, "Failed to stop session on board: ${error.message}")
                    completeSession()
                }
            }
            return
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
            val completedSessions = if (it.timerState == TimerState.FINISHED) {
                it.completedSessions
            } else {
                it.completedSessions + 1
            }
            it.copy(
                completedSessions = completedSessions,
                timerState = TimerState.FINISHED,
                isSyncingWithBoard = false
            )
        }
    }

    /**
     * 鍚屾鏈湴浼氳瘽璁板綍缁欒澶?
     * 鐢ㄤ簬鏈湴妯″紡缁撴潫鍚庡皢璁板綍鍚屾
     */
    suspend fun syncSessionToBoard(durationSeconds: Int) {
        if (bleManager.isConnected()) {
            val usage = com.edgefocus.app.data.remote.PhoneUsage(
                isInUse = false,
                appCategory = "other",
                distractionLevel = 3,
                appName = "local_session_sync",
                durationSec = durationSeconds
            )
            bleManager.phoneSync(usage)
            Log.d(TAG, "Local session synced to board: duration=${durationSeconds}s")
        }
    }

    private fun finishFromBoardReport(report: JSONObject) {
        _state.update {
            val completedSessions = if (it.timerState == TimerState.FINISHED) {
                it.completedSessions
            } else {
                it.completedSessions + 1
            }
            it.copy(
                timerState = TimerState.FINISHED,
                isSyncingWithBoard = false,
                completedSessions = completedSessions,
                timeRemaining = 0L,
                boardTotalDuration = report.optInt("total_duration", 0),
                boardFocusDuration = report.optInt("focus_duration", 0),
                boardSessionDistractionCount = report.optInt("distraction_count", 0),
                boardSessionReminderCount = report.optInt("reminder_count", 0),
                boardSessionPhoneDistractionCount = report.optInt("phone_distraction_count", it.phoneDistractionCount)
            )
        }
    }

    // ==================== 妫€鏌ヨ澶囦細璇?====================

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
                    // 璁惧鏈夎繘琛屼腑鐨勪細璇濓紝鍚屾
                    Log.d(TAG, "Board has active session, syncing: remaining=${remainingMs}ms")
                    _state.update {
                        val syncedSessionId = it.sessionId ?: "session_${System.currentTimeMillis()}"
                        val syncedSessionStartTime = it.sessionStartTime.takeIf { time -> time > 0L } ?: System.currentTimeMillis()
                        it.copy(
                            timerState = TimerState.RUNNING,
                            sessionType = SessionType.WORK,
                            timeRemaining = remainingMs,
                            totalTime = if (targetDurationMs > 0) targetDurationMs else it.totalTime,
                            targetDurationMinutes = if (targetDurationMs > 0) (targetDurationMs / 60000).toInt() else it.targetDurationMinutes,
                            sessionMode = SessionMode.EDGEFOCUS,
                            isSyncingWithBoard = true,
                            sessionId = syncedSessionId,
                            sessionStartTime = syncedSessionStartTime
                        )
                    }
                    startLocalCountdown()
                    startBoardSync()
                } else {
                    // 璁惧绌洪棽锛岀瓑寰呯敤鎴锋搷浣?
                    Log.d(TAG, "Board is idle, waiting for user action")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Check board session failed: ${e.message}")
        }
    }

    // ==================== 鏈湴鍊掕鏃?====================

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

            // 鏃堕棿鍒?
            if (_state.value.timeRemaining <= 0 && _state.value.timerState == TimerState.RUNNING) {
                if (_state.value.sessionMode == SessionMode.LOCAL || !bleManager.isConnected()) {
                    completeSession()
                } else {
                    appEventLog.log("TIMER_ZERO", "EdgeFocus mode waits for board completion push")
                    _state.update { it.copy(timeRemaining = 0L) }
                }
            }
        }
    }

    // ==================== 鏉跨鍚屾 ====================

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
        // 鏈湴缁х画鍊掕鏃?
    }

    private fun calibrateFromBoard(jsonString: String) {
        try {
            val json = JSONObject(jsonString)
            val data = json.getJSONObject("data")

            val boardRemainingMs = data.optLong("remaining_ms", 0)
            val boardTargetDurationMs = data.optLong("target_duration_ms", 0)

            if (boardTargetDurationMs > 0) {
                if (boardRemainingMs <= 0) {
                    appEventLog.log("BOARD_TIME_ZERO", "Waiting for board report push")
                    _state.update {
                        it.copy(
                            timeRemaining = 0L,
                            totalTime = boardTargetDurationMs
                        )
                    }
                    return
                }

                val localRemaining = _state.value.timeRemaining
                val diff = kotlin.math.abs(localRemaining - boardRemainingMs)

                // 宸紓瓒呰繃闃堝€硷紝鐢ㄦ澘绔暟鎹牎鍑?
                if (diff > CALIBRATION_THRESHOLD_MS) {
                    Log.d(TAG, "Calibrating: local=${localRemaining}ms, board=${boardRemainingMs}ms, diff=${diff}ms")
                    _state.update {
                        it.copy(
                            timeRemaining = boardRemainingMs,
                            totalTime = boardTargetDurationMs
                        )
                    }
                }

                // 鏉跨鎶ュ憡鏃堕棿鍒?
            }
        } catch (e: Exception) {
            Log.e(TAG, "Calibrate failed: ${e.message}")
        }
    }

    // ==================== 鐘舵€佹煡璇?====================

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

    fun getBoardTotalDuration(): Int = _state.value.boardTotalDuration ?: 0
    fun getBoardFocusDuration(): Int = _state.value.boardFocusDuration ?: 0
    fun getBoardSessionDistractionCount(): Int = _state.value.boardSessionDistractionCount ?: 0
    fun getBoardSessionReminderCount(): Int = _state.value.boardSessionReminderCount ?: 0
    fun getBoardSessionPhoneDistractionCount(): Int = _state.value.boardSessionPhoneDistractionCount
        ?: _state.value.phoneDistractionCount
    fun hasBoardSessionReport(): Boolean = _state.value.boardTotalDuration != null

    fun incrementPhoneDistraction() {
        _state.update { it.copy(phoneDistractionCount = it.phoneDistractionCount + 1) }
    }
}
