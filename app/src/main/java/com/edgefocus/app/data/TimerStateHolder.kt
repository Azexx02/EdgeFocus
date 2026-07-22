package com.edgefocus.app.data

import android.util.Log
import android.os.SystemClock
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.model.HostConnectionPhase
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.remote.AppEventLog
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.BoardPrecheck
import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import com.edgefocus.app.data.remote.effectivePhase
import com.edgefocus.app.data.remote.representsActiveRuntime
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.CameraPreviewImage
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.GimbalStatus
import com.edgefocus.app.data.remote.LocalSessionUpload
import com.edgefocus.app.data.remote.OutputCapability
import com.edgefocus.app.data.remote.PowerProfile
import com.edgefocus.app.data.remote.PushAlert
import com.edgefocus.app.data.remote.PushEventHandler
import com.edgefocus.app.data.session.StartTransactionPolicy
import com.edgefocus.app.data.session.BoardSyncLaunchPolicy
import com.edgefocus.app.data.session.BoardRemainingTimePolicy
import com.edgefocus.app.domain.model.FocusState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.precheck.PrecheckConfirmationPolicy
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
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

enum class SessionMode {
    LOCAL,
    EDGEFOCUS
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
    val boardSessionActive: Boolean = false,
    val boardSessionPaused: Boolean = false,
    val calibrationCompleted: Boolean = false,
    val sessionPhase: BoardSessionPhase = BoardSessionPhase.IDLE,
    val preparationState: BoardPreparationState = BoardPreparationState.NONE,
    val preparationError: String? = null,
    val focusState: Int = 0,
    val phoneSignal: Boolean = false,
    val phoneUsageState: String = "unknown",
    val phoneDistractionLevel: Int = -1,
    val riskScore: Int = 0,
    val commandLevel: Int = 0,
    val boardBatteryPercent: Int = 67,
    val boardBatteryCharging: Boolean = false,
    val boardPowerSource: String = "battery",
    val boardPowerProfile: PowerProfile = PowerProfile.GO,
    val elapsedMs: Long = 0L,
    val precheck: BoardPrecheck? = null,
    val outputCapability: OutputCapability? = null,
    val cameraPreview: CameraPreviewImage? = null,
    val gimbalStatus: GimbalStatus? = null,
    val lastAlert: PushAlert? = null,
    val boardErrorMessage: String? = null,
    val reminderSnapshot: String? = null,
    val precheckGuidance: String? = null,
    val focusReason: String? = null,
    val behaviorContext: String? = null,
    // 板端推送的会话结束数据
    val boardTotalDuration: Int? = null,
    val boardFocusDuration: Int? = null,
    val boardSessionDistractionCount: Int? = null,
    val boardSessionReminderCount: Int? = null,
    val boardSessionPhoneDistractionCount: Int? = null
)

@Singleton
class TimerStateHolder @Inject constructor(
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor,
    private val appEventLog: AppEventLog,
    private val settingsDao: SettingsDao
) {
    companion object {
        private const val TAG = "TimerStateHolder"
        private const val LOCAL_TICK_INTERVAL = 1000L
        private const val BOARD_SYNC_INTERVAL = 2000L
        private const val CALIBRATION_THRESHOLD_MS = 5000L
        private const val MANUAL_STOP_REPORT_TIMEOUT_MS = 10_000L
        private const val BOARD_COMPLETION_GRACE_MS = 5_000L
        private const val START_RECONCILE_DEADLINE_MS = 30_000L
        private const val START_RECONCILE_INTERVAL_MS = 1_000L
        private const val PREVIEW_INTERVAL_MS = 1_200L
    }

    private val _state = MutableStateFlow(SharedTimerState())
    val state: StateFlow<SharedTimerState> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var localCountdownJob: Job? = null
    private var boardSyncJob: Job? = null
    private var boardCompletionFallbackJob: Job? = null
    private var previewJob: Job? = null
    private var pendingStartJob: Job? = null
    private val pendingStartInFlight = AtomicBoolean(false)
    private val startOperationEpoch = AtomicLong(0L)
    private val precheckExitInFlight = AtomicBoolean(false)
    private val precheckExitEpoch = AtomicLong(0L)
    @Volatile
    private var closedPrecheckSessionId: String? = null
    @Volatile
    private var isControlPanelActive = false

    init {
        scope.launch {
            state.map { it.timerState == TimerState.PRECHECK }
                .distinctUntilChanged().collect { previewReady ->
                if (previewReady) startPreviewLoop() else stopPreviewLoop()
            }
        }
        scope.launch {
            settingsDao.getSettings().collect { settings ->
                settings?.let {
                    updateIdleDuration(it.pomodoroDuration)
                    val mode = if (it.useEdgeFocusBoard) {
                        SessionMode.EDGEFOCUS
                    } else {
                        SessionMode.LOCAL
                    }
                    if (_state.value.sessionMode != mode && _state.value.timerState == TimerState.IDLE) {
                        _state.update { state -> state.copy(sessionMode = mode) }
                        if (mode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
                            scope.launch { checkBoardSession() }
                        }
                    }
                }
            }
        }

        scope.launch {
            bleManager.connectionState.collect { connectionState ->
                val isConnected = connectionState == ConnectionState.CONNECTED
                _state.update {
                    if (isConnected) {
                        it.copy(isBoardConnected = true)
                    } else {
                        it.copy(isBoardConnected = false)
                    }
                }

                // 连接时，根据设置决定模式
                if (isConnected && _state.value.timerState == TimerState.IDLE) {
                    val settings = settingsDao.getSettingsOnce()
                    val mode = if (settings?.useEdgeFocusBoard == true) {
                        SessionMode.EDGEFOCUS
                    } else {
                        SessionMode.LOCAL
                    }
                    _state.update { it.copy(sessionMode = mode) }
                    appEventLog.log("BLE_CONNECT", "模式: $mode")

                }

                // 运行中连接，启动同步
                // 断开连接时停止同步任务（本地继续倒计时）
                if (!isConnected) {
                    stopBoardSync()
                    stopPreviewLoop()
                }
            }
        }

        scope.launch {
            connectionSupervisor.snapshot
                .map { it.phase to it.connectionEpoch }
                .distinctUntilChanged()
                .collect { (phase, _) ->
                if (phase != HostConnectionPhase.READY) return@collect
                if (_state.value.sessionMode == SessionMode.EDGEFOCUS) {
                    checkBoardSession()
                    if (_state.value.timerState == TimerState.RUNNING ||
                        (_state.value.timerState == TimerState.PRECHECK &&
                            _state.value.precheck?.gimbal?.manualLease != true) ||
                        _state.value.timerState == TimerState.PAUSED
                    ) {
                        startBoardSync()
                    }
                    if (isPreviewReady()) startPreviewLoop()
                }
            }
        }

        bleManager.setPushEventHandler(object : PushEventHandler {
            override fun onPushState(state: FocusState) {
                if (_state.value.sessionMode != SessionMode.EDGEFOCUS) return

                when (state.state) {
                    TimerState.RUNNING -> {
                        if (_state.value.timerState == TimerState.IDLE ||
                            _state.value.timerState == TimerState.FINISHED) {
                            appEventLog.log("PUSH_STATE", "板端开始专注，拉取完整状态")
                            scope.launch { checkBoardSession() }
                        } else if (_state.value.timerState == TimerState.PAUSED) {
                            appEventLog.log("PUSH_STATE", "板端恢复专注")
                            _state.update { it.copy(timerState = TimerState.RUNNING) }
                            startLocalCountdown()
                            startBoardSync()
                        }
                    }
                    TimerState.PAUSED -> {
                        if (_state.value.timerState == TimerState.RUNNING) {
                            appEventLog.log("PUSH_STATE", "板端暂停专注")
                            applyPausedState()
                        }
                    }
                    else -> Unit
                }
            }
            override fun onPushStateSnapshot(state: BoardSessionState) {
                if (_state.value.sessionMode != SessionMode.EDGEFOCUS) return
                if (precheckExitInFlight.get()) {
                    if (state.effectivePhase() == BoardSessionPhase.IDLE) {
                        completePrecheckExit(precheckExitEpoch.get(), state)
                    }
                    return
                }
                if (state.sessionPhase == BoardSessionPhase.IDLE && isEdgeFocusActiveLocally()) {
                    handleBoardIdleWhileLocalActive("push_state_idle")
                    return
                }
                if (state.sessionPhase == BoardSessionPhase.IDLE) {
                    applyBoardDeviceStatus(state)
                }
                if (state.representsActiveRuntime()) {
                    applyBoardSessionState(state)
                }
            }
            override fun onPushSessionEnd(summary: String) {
                cancelBoardCompletionFallback()
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
                cancelBoardCompletionFallback()
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
                        boardTotalDuration = timerComplete.totalDurationSec.takeIf { value -> value > 0 }
                            ?: (timerComplete.targetDurationMs / 1000).toInt(),
                        boardFocusDuration = timerComplete.focusDurationSec.takeIf { value -> value > 0 }
                            ?: (timerComplete.elapsedMs / 1000).toInt(),
                        distractionCount = timerComplete.distractionCount,
                        reminderCount = timerComplete.reminderCount,
                        phoneDistractionCount = timerComplete.phoneDistractionCount,
                        sessionId = timerComplete.sessionId ?: it.sessionId,
                        boardSessionDistractionCount = timerComplete.distractionCount,
                        boardSessionReminderCount = timerComplete.reminderCount,
                        boardSessionPhoneDistractionCount = timerComplete.phoneDistractionCount
                    )
                }
            }
            override fun onPushAlert(alert: PushAlert) {
                appEventLog.log("PUSH_ALERT", "level=${alert.level}, actions=${alert.phoneActions}")
                _state.update {
                    it.copy(
                        lastAlert = alert,
                        outputCapability = alert.outputCapability ?: it.outputCapability,
                        reminderCount = alert.reminderCount ?: it.reminderCount
                    )
                }
            }
        })
    }

    fun clearAlert() {
        val shouldAck = _state.value.lastAlert != null && bleManager.isConnected()
        _state.update { it.copy(lastAlert = null) }
        if (shouldAck) {
            scope.launch {
                bleManager.reminderAck("last_alert")
            }
        }
    }

    fun applyReliableAlert(alert: PushAlert) {
        _state.update {
            it.copy(
                lastAlert = alert,
                outputCapability = alert.outputCapability ?: it.outputCapability,
                reminderCount = alert.reminderCount ?: it.reminderCount
            )
        }
    }

    fun clearReliableAlert() {
        _state.update { it.copy(lastAlert = null) }
    }

    fun applyReliableSessionEnd(payloadJson: String) {
        cancelBoardCompletionFallback()
        val report = JSONObject(payloadJson).optJSONObject("data") ?: return
        finishFromBoardReport(report)
    }

    // ==================== 会话控制 ====================

    fun updateIdleDuration(minutes: Int) {
        val clamped = minutes.coerceIn(1, 120)
        val durationMs = TimeUtils.minutesToMillis(clamped)
        _state.update { current ->
            if (current.timerState != TimerState.IDLE && current.timerState != TimerState.FINISHED) {
                current
            } else {
                current.copy(
                    targetDurationMinutes = clamped,
                    timeRemaining = durationMs,
                    totalTime = durationMs
                )
            }
        }
    }

    /**
     * 启动 EdgeFocus 模式会话
     * 
     * 原则：EdgeFocus 设备是核心，手机是遥控器
     */
    fun startEdgeFocusSession(
        targetDurationMinutes: Int,
        sessionStartTime: Long = System.currentTimeMillis()
    ) {
        if (!bleManager.isConnected() || !connectionSupervisor.snapshot.value.isReady) {
            Log.w(TAG, "Cannot start EdgeFocus session: device not connected")
            return
        }
        if (!pendingStartInFlight.compareAndSet(false, true)) {
            Log.w(TAG, "Ignoring duplicate EdgeFocus start while board request is pending")
            return
        }

        val epoch = startOperationEpoch.incrementAndGet()
        val operationId = "android-${System.currentTimeMillis()}-$epoch"
        val deadlineElapsedMs = SystemClock.elapsedRealtime() + START_RECONCILE_DEADLINE_MS

        // 先设置基础状态（不启动倒计时）
        val totalTimeMs = TimeUtils.minutesToMillis(targetDurationMinutes)
        _state.update {
            it.copy(
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = true,
                timerState = TimerState.IDLE,
                boardSessionActive = false,
                boardSessionPaused = false,
                calibrationCompleted = false,
                sessionPhase = BoardSessionPhase.IDLE,
                preparationState = BoardPreparationState.INITIALIZING,
                preparationError = null,
                focusState = 0,
                elapsedMs = 0L,
                timeRemaining = totalTimeMs,
                totalTime = totalTimeMs,
                targetDurationMinutes = targetDurationMinutes.coerceIn(1, 120),
                precheck = null,
                precheckGuidance = null,
                cameraPreview = null,
                gimbalStatus = null,
                lastAlert = null,
                boardErrorMessage = null,
                reminderSnapshot = null,
                focusReason = null,
                behaviorContext = null,
                distractionCount = 0,
                reminderCount = 0,
                phoneDistractionCount = 0,
                sessionId = null,
                sessionStartTime = sessionStartTime,
                boardTotalDuration = null,
                boardFocusDuration = null,
                boardSessionDistractionCount = null,
                boardSessionReminderCount = null,
                boardSessionPhoneDistractionCount = null
            )
        }

        // start 只发送一次；超时表示结果未知，随后只核对主机权威状态。
        pendingStartJob = scope.launch {
            try {
                try {
                    appEventLog.log(
                        "SESSION_CONTROL",
                        "start, operation=$operationId, duration=${targetDurationMinutes}min"
                    )
                    val result = bleManager.sessionControl(
                        "start",
                        targetDurationMinutes,
                        operationId
                    )
                    result.onSuccess { data ->
                        val json = String(data, Charsets.UTF_8)
                        val envelopeError = runCatching {
                            BoardJsonParser.parseResponseError(json)
                        }.onFailure { error ->
                            appEventLog.log(
                                "SESSION_START_ACK_ENVELOPE_PARSE",
                                error.message ?: "invalid response envelope"
                            )
                        }.getOrNull()
                        envelopeError?.let { error ->
                            failPendingStart(epoch, error)
                            return@launch
                        }
                        runCatching {
                            BoardJsonParser.parseSessionState(json)
                        }.onSuccess { boardState ->
                            if (StartTransactionPolicy.acceptsDirectAck(operationId, boardState.operationId)) {
                                if (applyPendingStartObservation(
                                        epoch,
                                        operationId,
                                        boardState,
                                        targetDurationMinutes
                                    )
                                ) return@launch
                            }
                        }.onFailure { error ->
                            appEventLog.log("SESSION_START_ACK_PARSE", error.message ?: "invalid response")
                        }
                    }.onFailure { error ->
                        appEventLog.log(
                            "SESSION_START_ACK_UNKNOWN",
                            error.message ?: "BLE response unavailable"
                        )
                    }
                } catch (error: Exception) {
                    appEventLog.log(
                        "SESSION_START_ACK_UNKNOWN",
                        error.message ?: "invalid response"
                    )
                }

                while (startOperationEpoch.get() == epoch &&
                    SystemClock.elapsedRealtime() < deadlineElapsedMs
                ) {
                    if (_state.value.sessionPhase != BoardSessionPhase.IDLE) return@launch
                    if (connectionSupervisor.snapshot.value.isReady) {
                        bleManager.getCurrentState().onSuccess { data ->
                            runCatching {
                                BoardJsonParser.parseSessionState(String(data, Charsets.UTF_8))
                            }.onSuccess { boardState ->
                                if (applyPendingStartObservation(
                                        epoch,
                                        operationId,
                                        boardState,
                                        targetDurationMinutes
                                    )
                                ) return@launch
                            }.onFailure { error ->
                                appEventLog.log(
                                    "SESSION_START_RECONCILE_PARSE",
                                    error.message ?: "invalid state"
                                )
                            }
                        }
                    }
                    delay(START_RECONCILE_INTERVAL_MS)
                }
                failPendingStart(epoch, "EdgeFocus 未在 30 秒内确认预检，请重试")
            } finally {
                if (startOperationEpoch.get() == epoch) {
                    pendingStartJob = null
                    pendingStartInFlight.set(false)
                }
            }
        }

        Log.d(TAG, "EdgeFocus session starting: waiting for board response...")
    }

    private fun applyPendingStartObservation(
        epoch: Long,
        operationId: String,
        boardState: BoardSessionState,
        fallbackDurationMinutes: Int
    ): Boolean {
        if (startOperationEpoch.get() != epoch) return true
        StartTransactionPolicy.failureForCurrentOperation(
            operationId,
            boardState,
            System.currentTimeMillis()
        )?.let { error ->
            failPendingStart(epoch, error)
            return true
        }
        if (!boardState.success) {
            failPendingStart(epoch, boardState.preparationError ?: "EdgeFocus 拒绝启动会话")
            return true
        }
        if (!StartTransactionPolicy.isAuthoritativeActive(boardState)) return false
        appEventLog.log(
            "SESSION_START_CONFIRMED",
            "session=${boardState.sessionId}, phase=${boardState.sessionPhase}, preparation=${boardState.preparationState}"
        )
        applyBoardSessionState(boardState, fallbackDurationMinutes)
        return true
    }

    private fun failPendingStart(epoch: Long, message: String) {
        if (startOperationEpoch.get() != epoch) return
        _state.update {
            it.copy(
                timerState = TimerState.IDLE,
                sessionPhase = BoardSessionPhase.IDLE,
                preparationState = BoardPreparationState.FAILED,
                preparationError = message,
                isSyncingWithBoard = false,
                boardErrorMessage = message
            )
        }
        appEventLog.log("SESSION_START_FAILED", message)
    }

    /**
     * 启动本地模式会话
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

        startLocalCountdown()

        Log.d(TAG, "Local session started: duration=${targetDurationMinutes}min")
    }

    fun pauseSession() {
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "pause")
            scope.launch {
                val result = bleManager.sessionControl("pause")
                result.onSuccess { data ->
                    val success = try {
                        JSONObject(String(data, Charsets.UTF_8))
                            .optJSONObject("data")
                            ?.optBoolean("success", false) == true
                    } catch (e: Exception) {
                        false
                    }
                    if (success) {
                        applyPausedState()
                    } else {
                        Log.e(TAG, "Board rejected session pause")
                    }
                }.onFailure { error ->
                    Log.e(TAG, "Failed to pause session on board: ${error.message}")
                }
            }
        } else {
            applyPausedState()
        }
    }

    fun enterControlPanelMode() {
        isControlPanelActive = true
        if (bleManager.isConnected() && _state.value.sessionMode == SessionMode.EDGEFOCUS) {
            scope.launch { checkBoardSession() }
        }
    }

    fun leaveControlPanelMode() {
        isControlPanelActive = false
        if (_state.value.timerState == TimerState.IDLE ||
            _state.value.timerState == TimerState.FINISHED) {
            scope.launch {
                val settings = settingsDao.getSettingsOnce()
                val mode = if (settings?.useEdgeFocusBoard == true) {
                    SessionMode.EDGEFOCUS
                } else {
                    SessionMode.LOCAL
                }
                _state.update { it.copy(sessionMode = mode) }
            }
        }
    }

    private fun applyPausedState() {
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()
        _state.update { it.copy(timerState = TimerState.PAUSED) }
    }

    fun resumeSession() {
        // EdgeFocus模式下，先通知板端，等响应后再恢复
        if (_state.value.sessionMode == SessionMode.EDGEFOCUS && bleManager.isConnected()) {
            appEventLog.log("SESSION_CONTROL", "resume")
            scope.launch {
                try {
                    val result = bleManager.sessionControl("resume")
                    result.onSuccess { data ->
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
            _state.update { it.copy(timerState = TimerState.RUNNING) }
            startLocalCountdown()
        }
    }

    fun stopSession() {
        val stoppingState = _state.value
        if (stoppingState.sessionMode == SessionMode.EDGEFOCUS &&
            stoppingState.timerState == TimerState.PRECHECK
        ) {
            cancelBoardPrecheck()
            return
        }

        invalidatePendingStart()
        cancelBoardCompletionFallback()
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        if (_state.value.sessionMode == SessionMode.EDGEFOCUS) {
            _state.update { it.copy(isSyncingWithBoard = false) }
            if (bleManager.isConnected()) {
                appEventLog.log("SESSION_CONTROL", "stop")
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
            } else {
                appEventLog.log("SESSION_CONTROL", "stop while disconnected, using local fallback")
                completeSession()
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

    private fun cancelBoardPrecheck() {
        invalidatePendingStart()
        cancelBoardCompletionFallback()
        localCountdownJob?.cancel()
        boardSyncJob?.cancel()

        if (!bleManager.isConnected() || !connectionSupervisor.snapshot.value.isReady) {
            _state.update {
                it.copy(
                    isSyncingWithBoard = false,
                    boardErrorMessage = "主机未连接，预检仍保留在主机；重连后可再次退出"
                )
            }
            return
        }

        if (!precheckExitInFlight.compareAndSet(false, true)) return
        val exitEpoch = precheckExitEpoch.incrementAndGet()
        val sessionId = _state.value.sessionId
        if (sessionId.isNullOrBlank()) {
            precheckExitInFlight.set(false)
            _state.update { it.copy(boardErrorMessage = "缺少 PRECHECK session_id，请先刷新主机状态") }
            startBoardSync()
            return
        }

        _state.update { it.copy(isSyncingWithBoard = true, boardErrorMessage = null) }
        appEventLog.log("SESSION_CONTROL", "cancel precheck")
        scope.launch {
            val request = bleManager.sessionControl(
                action = "stop",
                sessionId = sessionId
            )
            if (request.isSuccess) {
                val state = parseAuthoritativeBoardState(request.getOrThrow()) ?: BoardSessionState()
                completePrecheckExit(exitEpoch, state)
                return@launch
            }
            val idleState = awaitAuthoritativeIdle()
            if (exitEpoch != precheckExitEpoch.get() || !precheckExitInFlight.get()) {
                return@launch
            }
            if (idleState != null) {
                completePrecheckExit(exitEpoch, idleState)
            } else {
                val message = request.exceptionOrNull()?.message
                    ?: "主机尚未确认退出预检，请保持连接后重试"
                appEventLog.log("PRECHECK_CANCEL_PENDING", message)
                precheckExitInFlight.set(false)
                _state.update {
                    it.copy(isSyncingWithBoard = false, boardErrorMessage = message)
                }
                startBoardSync()
            }
        }
    }

    private suspend fun awaitAuthoritativeIdle(): BoardSessionState? {
        return withTimeoutOrNull(10_000L) {
            while (true) {
                val observed = bleManager.getCurrentState().getOrNull()
                    ?.let(::parseAuthoritativeBoardState)
                if (observed?.effectivePhase() == BoardSessionPhase.IDLE) {
                    return@withTimeoutOrNull observed
                }
                delay(250L)
            }
            @Suppress("UNREACHABLE_CODE")
            null
        }
    }

    private fun parseAuthoritativeBoardState(bytes: ByteArray): BoardSessionState? = runCatching {
        val root = JSONObject(String(bytes, Charsets.UTF_8))
        val data = root.optJSONObject("data") ?: return@runCatching null
        val runtimePhase = data.optJSONObject("runtime_snapshot")
            ?.optJSONObject("session")
            ?.optString("phase")
            .orEmpty()
        val legacyPhase = data.optString("session_phase")
        val wirePhase = runtimePhase.ifBlank { legacyPhase }
        if (BoardSessionPhase.fromWireOrNull(wirePhase) == null) return@runCatching null
        BoardJsonParser.parseSessionState(data)
    }.getOrNull()

    private fun completePrecheckExit(exitEpoch: Long, boardState: BoardSessionState) {
        if (exitEpoch != precheckExitEpoch.get()) return
        if (!precheckExitInFlight.compareAndSet(true, false)) return
        precheckExitEpoch.incrementAndGet()
        applyBoardIdleAfterPrecheckExit(boardState)
    }

    private fun applyBoardIdleAfterPrecheckExit(boardState: BoardSessionState) {
        closedPrecheckSessionId = _state.value.sessionId
        val targetMs = _state.value.totalTime.takeIf { it > 0L }
            ?: TimeUtils.minutesToMillis(_state.value.targetDurationMinutes)
        _state.update {
            it.copy(
                timerState = TimerState.IDLE,
                timeRemaining = targetMs,
                totalTime = targetMs,
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = false,
                sessionId = null,
                sessionStartTime = 0L,
                boardSessionActive = false,
                boardSessionPaused = false,
                calibrationCompleted = false,
                sessionPhase = BoardSessionPhase.IDLE,
                preparationState = boardState.preparationState,
                preparationError = boardState.preparationError,
                precheck = null,
                cameraPreview = null,
                precheckGuidance = null,
                boardErrorMessage = null
            )
        }
        stopBoardSync()
    }

    fun skipPrecheck() {
        sendSessionControlAndApply("skip_precheck")
    }

    fun skipCalibration() {
        sendSessionControlAndApply("skip_calibration")
    }

    fun refreshCameraPreview() {
        if (!bleManager.isConnected() || !connectionSupervisor.snapshot.value.isReady) return
        if (_state.value.timerState != TimerState.PRECHECK) return
        val requestSessionId = _state.value.sessionId
        scope.launch {
            bleManager.getCameraPreviewImage().onSuccess { data ->
                runCatching {
                    BoardJsonParser.parseCameraPreview(String(data, Charsets.UTF_8))
                }.onSuccess { preview ->
                    if (isCurrentPrecheckSession(requestSessionId)) {
                        _state.update { it.copy(cameraPreview = preview, boardErrorMessage = null) }
                    }
                }.onFailure { error ->
                    appEventLog.log("PREVIEW_PARSE", error.message ?: "invalid preview")
                }
            }.onFailure { error ->
                if (isCurrentPrecheckSession(requestSessionId)) {
                    _state.update { it.copy(boardErrorMessage = "无法刷新摄像头预览：${error.message ?: "未知错误"}") }
                }
            }
        }
    }

    fun refreshBoardState() {
        scope.launch { checkBoardSession() }
    }

    suspend fun pausePrecheckPreviewRequests() {
        stopPreviewLoop()
        bleManager.cancelPreviewRequests()
    }

    fun applyBoardCommandResponse(
        response: ByteArray,
        expectedSessionId: String
    ): Result<BoardSessionState> = runCatching {
        val boardState = BoardJsonParser.parseSessionState(String(response, Charsets.UTF_8))
        check(boardState.sessionId == expectedSessionId) { "PRECHECK 会话已变化" }
        applyBoardSessionState(boardState)
        boardState
    }

    private fun startPreviewLoop() {
        if (!connectionSupervisor.snapshot.value.isReady || !bleManager.isConnected()) return
        if (previewJob?.isActive == true) return
        previewJob = scope.launch {
            while (isPreviewReady() &&
                connectionSupervisor.snapshot.value.isReady &&
                bleManager.isConnected()
            ) {
                val requestSessionId = _state.value.sessionId
                bleManager.getCameraPreviewImage().onSuccess { data ->
                    runCatching {
                        BoardJsonParser.parseCameraPreview(String(data, Charsets.UTF_8))
                    }.onSuccess { preview ->
                        if (isCurrentPrecheckSession(requestSessionId)) {
                            _state.update { it.copy(cameraPreview = preview, boardErrorMessage = null) }
                        }
                    }.onFailure { error ->
                        appEventLog.log("PREVIEW_PARSE", error.message ?: "invalid preview")
                    }
                }
                delay(PREVIEW_INTERVAL_MS)
            }
        }
    }

    private fun stopPreviewLoop() {
        previewJob?.cancel()
        previewJob = null
        scope.launch { bleManager.cancelPreviewRequests() }
    }

    private fun isCurrentPrecheckSession(requestSessionId: String?): Boolean =
        _state.value.timerState == TimerState.PRECHECK &&
            (requestSessionId == null || _state.value.sessionId == requestSessionId)

    private fun isPreviewReady(): Boolean =
        _state.value.timerState == TimerState.PRECHECK &&
            // PRECHECK preview is a still-image aid, not a video stream. Once a
            // frame is available, keep it stable and leave BLE bandwidth free
            // for latency-sensitive manual gimbal commands. The explicit
            // refresh button remains available whenever the user wants a new
            // frame.
            _state.value.cameraPreview?.available != true

    private fun invalidatePendingStart() {
        startOperationEpoch.incrementAndGet()
        pendingStartJob?.cancel()
        pendingStartJob = null
        pendingStartInFlight.set(false)
    }

    fun refreshGimbalStatus() {
        if (!bleManager.isConnected()) return
        scope.launch {
            bleManager.getGimbalStatus().onSuccess { data ->
                val status = BoardJsonParser.parseGimbalStatus(String(data, Charsets.UTF_8))
                _state.update { it.copy(gimbalStatus = status, boardErrorMessage = null) }
            }.onFailure { error ->
                _state.update { it.copy(boardErrorMessage = "无法读取云台状态：${error.message ?: "未知错误"}") }
            }
        }
    }

    private fun refreshGimbalStatusIfVisible() {
        if (_state.value.precheck?.gimbal == null) return
        refreshGimbalStatus()
    }

    fun moveGimbal(
        action: String,
        xDeg: Number? = null,
        yDeg: Number? = null,
        speed: Int? = null,
        stepDeg: Int? = null
    ) {
        if (!bleManager.isConnected() || _state.value.precheck?.gimbal == null) return
        val safeSpeed = speed?.coerceIn(1, 90)
        val safeStep = stepDeg?.coerceIn(1, 8)
        scope.launch {
            bleManager.gimbalControl(action, xDeg, yDeg, safeSpeed, safeStep).onSuccess {
                refreshGimbalStatus()
            }.onFailure { error ->
                _state.update {
                    it.copy(
                        precheck = it.precheck?.copy(gimbal = null),
                        boardErrorMessage = "云台暂不可用，已保留摄像头预检"
                    )
                }
                Log.e(TAG, "Gimbal command failed: ${error.message}")
            }
        }
    }

    private fun sendSessionControlAndApply(action: String) {
        if (!bleManager.isConnected()) return
        scope.launch {
            bleManager.sessionControl(action).onSuccess { data ->
                val boardState = BoardJsonParser.parseSessionState(String(data, Charsets.UTF_8))
                applyBoardSessionState(boardState)
            }.onFailure { error ->
                _state.update { it.copy(boardErrorMessage = "主机操作失败：${error.message ?: "未知错误"}") }
            }
        }
    }

    fun completeSession() {
        cancelBoardCompletionFallback()
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
     * 用于本地模式结束后将记录同步
     */
    suspend fun syncSessionToBoard(session: LocalSessionUpload) {
        if (!bleManager.isConnected()) return
        bleManager.uploadLocalSession(session)
            .onSuccess {
                Log.d(TAG, "Uploaded local session to board: ${session.sessionId}")
            }
            .onFailure { error ->
                Log.e(TAG, "Failed to upload local session to board: ${error.message}", error)
            }
    }

    private fun isEdgeFocusActiveLocally(): Boolean {
        val current = _state.value
        return current.sessionMode == SessionMode.EDGEFOCUS &&
            (current.timerState == TimerState.RUNNING ||
                current.timerState == TimerState.PAUSED ||
                current.timerState == TimerState.PRECHECK)
    }

    private fun reportDataFromResponse(bytes: ByteArray): JSONObject? {
        val root = JSONObject(String(bytes, Charsets.UTF_8))
        val data = root.optJSONObject("data") ?: root
        return data.takeIf { isValidBoardReport(it) }
    }

    private fun isValidBoardReport(report: JSONObject?): Boolean {
        if (report == null) return false
        return report.has("total_duration") || report.has("focus_duration")
    }

    private fun scheduleBoardCompletionFallback(reason: String) {
        if (_state.value.sessionMode != SessionMode.EDGEFOCUS ||
            _state.value.timerState != TimerState.RUNNING
        ) {
            return
        }
        if (boardCompletionFallbackJob?.isActive == true) return
        appEventLog.log("BOARD_COMPLETION_FALLBACK", "scheduled: $reason")
        boardCompletionFallbackJob = scope.launch {
            delay(BOARD_COMPLETION_GRACE_MS)
            pollBoardCompletionOrFallback(reason)
        }
    }

    private fun cancelBoardCompletionFallback() {
        boardCompletionFallbackJob?.cancel()
        boardCompletionFallbackJob = null
    }

    private suspend fun pollBoardCompletionOrFallback(reason: String) {
        val current = _state.value
        if (current.timerState == TimerState.FINISHED ||
            current.sessionMode != SessionMode.EDGEFOCUS
        ) {
            return
        }
        if (!bleManager.isConnected()) {
            appEventLog.log("BOARD_COMPLETION_FALLBACK", "disconnected: $reason")
            completeSession()
            return
        }

        val report = bleManager.getLastReport().getOrNull()?.let { reportDataFromResponse(it) }
        if (isValidBoardReport(report)) {
            appEventLog.log("BOARD_COMPLETION_FALLBACK", "finished by last_report: $reason")
            finishFromBoardReport(report!!)
            return
        }

        val stateBytes = bleManager.getCurrentState().getOrNull()
        if (stateBytes == null) {
            appEventLog.log("BOARD_COMPLETION_FALLBACK", "state unavailable: $reason")
            completeSession()
            return
        }

        val stateData = JSONObject(String(stateBytes, Charsets.UTF_8)).getJSONObject("data")
        val boardState = BoardJsonParser.parseSessionState(stateData)
        if (boardState.sessionPhase == BoardSessionPhase.IDLE || !boardState.active) {
            appEventLog.log("BOARD_COMPLETION_FALLBACK", "board idle: $reason")
            completeSession()
        } else {
            appEventLog.log("BOARD_COMPLETION_FALLBACK", "board still active: $reason")
            applyBoardSessionState(boardState)
        }
    }

    private fun handleBoardIdleWhileLocalActive(reason: String) {
        if (!isEdgeFocusActiveLocally()) return
        appEventLog.log("BOARD_IDLE", reason)
        if (_state.value.timerState == TimerState.PRECHECK) {
            applyBoardIdleAfterPrecheckExit(BoardSessionState())
            return
        }
        cancelBoardCompletionFallback()
        localCountdownJob?.cancel()
        stopBoardSync()
        _state.update {
            it.copy(
                boardSessionActive = false,
                boardSessionPaused = false,
                isSyncingWithBoard = false
            )
        }
        scope.launch {
            val report = bleManager.getLastReport().getOrNull()?.let { reportDataFromResponse(it) }
            if (isValidBoardReport(report)) {
                finishFromBoardReport(report!!)
            } else {
                completeSession()
            }
        }
    }

    private fun finishFromBoardReport(report: JSONObject) {
        cancelBoardCompletionFallback()
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
                boardSessionPhoneDistractionCount = report.optInt("phone_distraction_count", 0),
                phoneDistractionCount = report.optInt("phone_distraction_count", 0)
            )
        }
    }

    private fun applyBoardDeviceStatus(boardState: BoardSessionState) {
        _state.update {
            it.copy(
                boardBatteryPercent = boardState.batteryPercent,
                boardBatteryCharging = boardState.batteryCharging,
                boardPowerSource = boardState.powerSource,
                boardPowerProfile = boardState.powerProfile,
                outputCapability = boardState.outputCapability ?: it.outputCapability
            )
        }
    }

    private fun applyBoardSessionState(
        boardState: BoardSessionState,
        fallbackDurationMinutes: Int = _state.value.targetDurationMinutes
    ) {
        if (PrecheckObservationPolicy.isClosedPrecheck(closedPrecheckSessionId, boardState)) return
        val before = _state.value
        if (PrecheckConfirmationPolicy.isStalePrecheckRegression(
                currentTimerState = before.timerState,
                currentSessionId = before.sessionId,
                incomingSessionId = boardState.sessionId,
                incomingPhase = boardState.effectivePhase()
            )
        ) {
            appEventLog.log(
                "STALE_PRECHECK_IGNORED",
                "session=${boardState.sessionId}, current=${before.sessionPhase}"
            )
            return
        }
        if (boardState.effectivePhase() == BoardSessionPhase.PRECHECK &&
            boardState.sessionId != closedPrecheckSessionId
        ) {
            closedPrecheckSessionId = null
        }
        val fallbackTotalMs = TimeUtils.minutesToMillis(fallbackDurationMinutes)
        val targetMs = when {
            boardState.hasTargetDurationMs && boardState.targetDurationMs > 0L -> boardState.targetDurationMs
            _state.value.totalTime > 0L -> _state.value.totalTime
            else -> fallbackTotalMs
        }
        val phase = boardState.effectivePhase()
        val incomingRemainingMs = when {
            phase == BoardSessionPhase.PRECHECK && boardState.remainingMs <= 0L -> targetMs
            boardState.hasRemainingMs -> boardState.remainingMs.coerceAtLeast(0L)
            phase == BoardSessionPhase.PRECHECK -> targetMs
            _state.value.timeRemaining > 0L -> _state.value.timeRemaining
            else -> targetMs
        }
        val currentState = _state.value
        val remainingMs = BoardRemainingTimePolicy.select(
            currentSessionId = currentState.sessionId,
            incomingSessionId = boardState.sessionId,
            currentPhase = currentState.sessionPhase,
            incomingPhase = phase,
            currentRemainingMs = currentState.timeRemaining,
            incomingRemainingMs = incomingRemainingMs
        )
        val timerState = when (phase) {
            BoardSessionPhase.PRECHECK -> TimerState.PRECHECK
            BoardSessionPhase.FOCUSING -> TimerState.RUNNING
            BoardSessionPhase.PAUSED -> TimerState.PAUSED
            BoardSessionPhase.IDLE -> TimerState.IDLE
        }

        _state.update {
            it.copy(
                timerState = timerState,
                sessionType = SessionType.WORK,
                timeRemaining = remainingMs,
                totalTime = targetMs,
                targetDurationMinutes = if (targetMs > 0) (targetMs / 60000).toInt().coerceAtLeast(1) else fallbackDurationMinutes,
                sessionMode = SessionMode.EDGEFOCUS,
                isSyncingWithBoard = timerState == TimerState.RUNNING || timerState == TimerState.PRECHECK,
                sessionId = boardState.sessionId ?: it.sessionId,
                boardSessionActive = boardState.active || phase == BoardSessionPhase.FOCUSING || phase == BoardSessionPhase.PRECHECK,
                boardSessionPaused = boardState.paused || phase == BoardSessionPhase.PAUSED,
                calibrationCompleted = boardState.calibrationCompleted,
                distractionCount = boardState.distractionCount,
                reminderCount = boardState.reminderCount,
                phoneDistractionCount = boardState.phoneDistractionCount,
                sessionPhase = phase,
                preparationState = boardState.preparationState,
                preparationError = boardState.preparationError,
                focusState = boardState.focusState,
                phoneSignal = boardState.phoneSignal,
                phoneUsageState = boardState.phoneUsageState,
                phoneDistractionLevel = boardState.phoneDistractionLevel,
                riskScore = boardState.riskScore,
                commandLevel = boardState.commandLevel,
                boardBatteryPercent = boardState.batteryPercent,
                boardBatteryCharging = boardState.batteryCharging,
                boardPowerSource = boardState.powerSource,
                boardPowerProfile = boardState.powerProfile,
                elapsedMs = boardState.elapsedMs.takeIf { elapsed -> elapsed > 0L }
                    ?: (targetMs - remainingMs).coerceAtLeast(0L),
                precheck = boardState.precheck,
                outputCapability = boardState.outputCapability ?: it.outputCapability,
                cameraPreview = boardState.cameraPreview ?: it.cameraPreview,
                reminderSnapshot = boardState.reminder ?: it.reminderSnapshot,
                precheckGuidance = boardState.precheckGuidance ?: it.precheckGuidance,
                focusReason = boardState.focusReason ?: it.focusReason,
                behaviorContext = boardState.behaviorContext ?: it.behaviorContext,
                boardErrorMessage = null
            )
        }

        when (timerState) {
            TimerState.PRECHECK -> {
                localCountdownJob?.cancel()
                if (boardState.precheck?.gimbal?.manualLease == true) {
                    // Once the board grants manual control, the last
                    // authoritative snapshot is sufficient for UI gating.
                    // Continuous 3 KB state responses otherwise occupy the
                    // BLE notification channel and delay every direction tap.
                    stopBoardSync()
                } else {
                    startBoardSync()
                    refreshGimbalStatusIfVisible()
                }
            }
            TimerState.RUNNING -> {
                startBoardSync()
                startLocalCountdown()
            }
            TimerState.PAUSED -> {
                localCountdownJob?.cancel()
                startBoardSync()
            }
            TimerState.IDLE, TimerState.FINISHED -> {
                cancelBoardCompletionFallback()
                localCountdownJob?.cancel()
                stopBoardSync()
            }
        }
    }


    private suspend fun checkBoardSession() {
        if (!bleManager.isConnected() || !connectionSupervisor.snapshot.value.isReady) return
        val observationEpoch = precheckExitEpoch.get()
        try {
            val result = bleManager.getCurrentState()
            result.onSuccess { data ->
                if (PrecheckObservationPolicy.isStale(observationEpoch, precheckExitEpoch.get())) {
                    return@onSuccess
                }
                val json = String(data, Charsets.UTF_8)
                val jsonObj = JSONObject(json)
                val stateData = jsonObj.getJSONObject("data")
                val boardState = BoardJsonParser.parseSessionState(stateData)
                if (precheckExitInFlight.get()) {
                    if (boardState.effectivePhase() == BoardSessionPhase.IDLE) {
                        completePrecheckExit(precheckExitEpoch.get(), boardState)
                    }
                    return@onSuccess
                }
                if (boardState.sessionPhase != BoardSessionPhase.IDLE) {
                    applyBoardSessionState(boardState)
                    return@onSuccess
                }
                if (isEdgeFocusActiveLocally()) {
                    handleBoardIdleWhileLocalActive("poll_idle")
                    return@onSuccess
                }
                applyBoardDeviceStatus(boardState)
                if (boardState.preparationState == BoardPreparationState.FAILED) {
                    _state.update {
                        it.copy(
                            preparationState = BoardPreparationState.FAILED,
                            preparationError = boardState.preparationError,
                            boardErrorMessage = boardState.preparationError ?: "EdgeFocus 预检准备失败"
                        )
                    }
                }

                val isActive = stateData.optBoolean("active", false)
                val focusState = stateData.optInt("focus_state", 0)
                val elapsedMs = stateData.optLong("elapsed_ms", 0)
                val remainingMs = stateData.optLong("remaining_ms", 0)
                val targetDurationMs = stateData.optLong("target_duration_ms", 0)
                val distractionCount = stateData.optInt("distraction_count", 0)
                val reminderCount = stateData.optInt("reminder_count", 0)

                if (isActive && remainingMs > 0) {
                    // 设备有进行中的会话，同步
                    Log.d(TAG, "Board has active session, syncing: remaining=${remainingMs}ms")
                    _state.update {
                        val boardSessionId = stateData.optString("session_id")
                        val syncedSessionId = boardSessionId.takeIf { id -> id.isNotBlank() }
                            ?: it.sessionId
                            ?: "session_${System.currentTimeMillis()}"
                        val effectiveElapsedMs = elapsedMs.takeIf { value -> value > 0L }
                            ?: (targetDurationMs - remainingMs).coerceAtLeast(0L)
                        val syncedSessionStartTime = it.sessionStartTime.takeIf { time -> time > 0L }
                            ?: (System.currentTimeMillis() - effectiveElapsedMs)
                        it.copy(
                            timerState = if (focusState == 6) TimerState.PAUSED else TimerState.RUNNING,
                            sessionType = SessionType.WORK,
                            timeRemaining = remainingMs,
                            totalTime = if (targetDurationMs > 0) targetDurationMs else it.totalTime,
                            targetDurationMinutes = if (targetDurationMs > 0) (targetDurationMs / 60000).toInt() else it.targetDurationMinutes,
                            sessionMode = SessionMode.EDGEFOCUS,
                            isSyncingWithBoard = true,
                            sessionId = syncedSessionId,
                            sessionStartTime = syncedSessionStartTime,
                            boardSessionActive = true,
                            boardSessionPaused = focusState == 6,
                            calibrationCompleted = boardState.calibrationCompleted,
                            sessionPhase = if (focusState == 6) BoardSessionPhase.PAUSED else BoardSessionPhase.FOCUSING,
                            focusState = focusState,
                            phoneSignal = boardState.phoneSignal,
                            phoneUsageState = boardState.phoneUsageState,
                            phoneDistractionLevel = boardState.phoneDistractionLevel,
                            riskScore = boardState.riskScore,
                            commandLevel = boardState.commandLevel,
                            boardBatteryPercent = boardState.batteryPercent,
                            boardBatteryCharging = boardState.batteryCharging,
                            boardPowerSource = boardState.powerSource,
                            boardPowerProfile = boardState.powerProfile,
                            elapsedMs = effectiveElapsedMs,
                            outputCapability = boardState.outputCapability ?: it.outputCapability,
                            cameraPreview = boardState.cameraPreview ?: it.cameraPreview,
                            reminderSnapshot = boardState.reminder ?: it.reminderSnapshot,
                            precheckGuidance = boardState.precheckGuidance ?: it.precheckGuidance,
                            focusReason = boardState.focusReason ?: it.focusReason,
                            behaviorContext = boardState.behaviorContext ?: it.behaviorContext,
                            distractionCount = distractionCount,
                            reminderCount = reminderCount
                        )
                    }
                    if (focusState != 6) {
                        startLocalCountdown()
                        startBoardSync()
                    }
                } else {
                    Log.d(TAG, "Board is idle, waiting for user action")
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Check board session failed: ${e.message}")
        }
    }


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

            if (_state.value.timeRemaining <= 0 && _state.value.timerState == TimerState.RUNNING) {
                if (_state.value.sessionMode == SessionMode.LOCAL || !bleManager.isConnected()) {
                    completeSession()
                } else {
                    appEventLog.log("TIMER_ZERO", "EdgeFocus mode waits briefly for board completion push")
                    _state.update { it.copy(timeRemaining = 0L) }
                    scheduleBoardCompletionFallback("timer_zero")
                }
            }
        }
    }

    // ==================== 板端同步 ====================

    @Synchronized
    private fun startBoardSync() {
        if (!BoardSyncLaunchPolicy.shouldLaunch(
                protocolReady = connectionSupervisor.snapshot.value.isReady,
                currentJobActive = boardSyncJob?.isActive == true
            )
        ) return
        boardSyncJob = scope.launch {
            while ((_state.value.timerState == TimerState.RUNNING ||
                   _state.value.timerState == TimerState.PRECHECK ||
                   _state.value.timerState == TimerState.PAUSED) &&
                   _state.value.sessionMode == SessionMode.EDGEFOCUS &&
                   connectionSupervisor.snapshot.value.isReady &&
                   bleManager.isConnected()) {
                try {
                    val observationEpoch = precheckExitEpoch.get()
                    val result = bleManager.getCurrentState()
                    result.onSuccess { data ->
                        calibrateFromBoard(String(data, Charsets.UTF_8), observationEpoch)
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Board sync failed: ${e.message}")
                }
                delay(BOARD_SYNC_INTERVAL)
            }
        }
    }

    @Synchronized
    private fun stopBoardSync() {
        boardSyncJob?.cancel()
        boardSyncJob = null
        _state.update { it.copy(isSyncingWithBoard = pendingStartInFlight.get()) }
    }

    private fun calibrateFromBoard(jsonString: String, observationEpoch: Long) {
        try {
            if (PrecheckObservationPolicy.isStale(observationEpoch, precheckExitEpoch.get())) return
            val json = JSONObject(jsonString)
            val data = json.getJSONObject("data")
            val boardState = BoardJsonParser.parseSessionState(data)
            if (precheckExitInFlight.get()) {
                if (boardState.effectivePhase() == BoardSessionPhase.IDLE) {
                    completePrecheckExit(precheckExitEpoch.get(), boardState)
                }
                return
            }
            if (boardState.sessionPhase == BoardSessionPhase.PRECHECK ||
                boardState.sessionPhase == BoardSessionPhase.PAUSED ||
                (_state.value.timerState == TimerState.PRECHECK &&
                    boardState.sessionPhase == BoardSessionPhase.FOCUSING)
            ) {
                applyBoardSessionState(boardState)
                return
            }

            val boardRemainingMs = if (boardState.hasRemainingMs) {
                boardState.remainingMs
            } else {
                data.optLong("remaining_ms", 0)
            }
            val boardTargetDurationMs = data.optLong("target_duration_ms", 0)
            val boardDistractionCount = data.optInt(
                "distraction_count",
                _state.value.distractionCount
            )
            val boardReminderCount = data.optInt(
                "reminder_count",
                _state.value.reminderCount
            )

            _state.update {
                it.copy(
                    distractionCount = boardDistractionCount,
                    reminderCount = boardReminderCount,
                    sessionPhase = boardState.sessionPhase.takeIf { phase -> phase != BoardSessionPhase.IDLE }
                        ?: it.sessionPhase,
                    boardSessionActive = boardState.active || it.boardSessionActive,
                    boardSessionPaused = boardState.paused,
                    calibrationCompleted = boardState.calibrationCompleted,
                    focusState = boardState.focusState,
                    phoneSignal = boardState.phoneSignal,
                    phoneUsageState = boardState.phoneUsageState,
                    phoneDistractionLevel = boardState.phoneDistractionLevel,
                    riskScore = boardState.riskScore,
                    commandLevel = boardState.commandLevel,
                    boardBatteryPercent = boardState.batteryPercent,
                    boardBatteryCharging = boardState.batteryCharging,
                    boardPowerSource = boardState.powerSource,
                    boardPowerProfile = boardState.powerProfile,
                    elapsedMs = boardState.elapsedMs.takeIf { elapsed -> elapsed > 0L }
                        ?: (boardTargetDurationMs - boardRemainingMs).coerceAtLeast(0L),
                    outputCapability = boardState.outputCapability ?: it.outputCapability,
                    cameraPreview = boardState.cameraPreview ?: it.cameraPreview,
                    reminderSnapshot = boardState.reminder ?: it.reminderSnapshot,
                    precheckGuidance = boardState.precheckGuidance ?: it.precheckGuidance,
                    focusReason = boardState.focusReason ?: it.focusReason,
                    behaviorContext = boardState.behaviorContext ?: it.behaviorContext
                )
            }

            if (boardTargetDurationMs > 0) {
                if (boardRemainingMs <= 0) {
                    appEventLog.log("BOARD_TIME_ZERO", "Waiting briefly for board report push")
                    _state.update {
                        it.copy(
                            timeRemaining = 0L,
                            totalTime = boardTargetDurationMs
                        )
                    }
                    scheduleBoardCompletionFallback("board_remaining_zero")
                    return
                }

                val currentState = _state.value
                val localRemaining = currentState.timeRemaining
                val acceptedRemainingMs = BoardRemainingTimePolicy.select(
                    currentSessionId = currentState.sessionId,
                    incomingSessionId = boardState.sessionId,
                    currentPhase = currentState.sessionPhase,
                    incomingPhase = boardState.effectivePhase(),
                    currentRemainingMs = localRemaining,
                    incomingRemainingMs = boardRemainingMs
                )
                val diff = kotlin.math.abs(localRemaining - acceptedRemainingMs)

                if (diff > CALIBRATION_THRESHOLD_MS) {
                    Log.d(TAG, "Calibrating: local=${localRemaining}ms, board=${boardRemainingMs}ms, accepted=${acceptedRemainingMs}ms, diff=${diff}ms")
                    _state.update {
                        it.copy(
                            timeRemaining = acceptedRemainingMs,
                            totalTime = boardTargetDurationMs
                        )
                    }
                }

            }
        } catch (e: Exception) {
            Log.e(TAG, "Calibrate failed: ${e.message}")
        }
    }


    fun getCurrentTimerState(): TimerState = _state.value.timerState
    fun getCurrentSessionType(): SessionType = _state.value.sessionType
    fun getRemainingTimeMillis(): Long = _state.value.timeRemaining
    fun getTotalTimeMillis(): Long = _state.value.totalTime
    fun getTargetDurationMinutes(): Int = _state.value.targetDurationMinutes
    fun getCompletedSessions(): Int = _state.value.completedSessions
    fun isBoardConnected(): Boolean = _state.value.isBoardConnected || bleManager.isConnected()
    fun getSessionMode(): SessionMode = _state.value.sessionMode
    fun isSyncingWithBoard(): Boolean = _state.value.isSyncingWithBoard
    fun getPhoneDistractionCount(): Int = _state.value.phoneDistractionCount

    fun getBoardTotalDuration(): Int = _state.value.boardTotalDuration ?: 0
    fun getBoardFocusDuration(): Int = _state.value.boardFocusDuration ?: 0
    fun getBoardSessionDistractionCount(): Int = _state.value.boardSessionDistractionCount ?: 0
    fun getBoardSessionReminderCount(): Int = _state.value.boardSessionReminderCount ?: 0
    fun getBoardSessionPhoneDistractionCount(): Int = _state.value.boardSessionPhoneDistractionCount ?: 0
    fun hasBoardSessionReport(): Boolean = _state.value.boardTotalDuration != null
    fun isActiveEdgeFocusSession(): Boolean =
        _state.value.sessionMode == SessionMode.EDGEFOCUS &&
            _state.value.timerState in setOf(TimerState.PRECHECK, TimerState.RUNNING, TimerState.PAUSED) &&
            isBoardConnected()

    fun incrementPhoneDistraction() {
        _state.update {
            if (TimerPhoneDistractionPolicy.shouldIncrementLocalPhoneDistraction(
                    sessionMode = it.sessionMode,
                    timerState = it.timerState
                )
            ) {
                it.copy(phoneDistractionCount = it.phoneDistractionCount + 1)
            } else {
                it
            }
        }
    }
}
