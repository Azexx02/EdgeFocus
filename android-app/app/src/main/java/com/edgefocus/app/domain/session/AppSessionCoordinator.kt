package com.edgefocus.app.domain.session

import android.content.Context
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.SharedTimerState
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.model.HostConnectionPhase
import com.edgefocus.app.data.local.entity.Session
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.LocalSessionUpload
import com.edgefocus.app.data.session.SessionPersistenceStore
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.service.AppMonitorService
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class AppSessionCoordinator @Inject constructor(
    @ApplicationContext private val context: Context,
    private val timerStateHolder: TimerStateHolder,
    private val connectionSupervisor: ConnectionSupervisor,
    private val persistenceStore: SessionPersistenceStore,
    private val bleManager: BleManager
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val mutex = Mutex()
    private var started = false
    private var runtime: LocalSessionRuntime? = null
    private var monitorRunning = false
    @Volatile
    private var completionInProgress = false
    private var previousTimerState = TimerState.IDLE

    val timerState: StateFlow<SharedTimerState> = timerStateHolder.state
    private val _uiSnapshot = MutableStateFlow(AppUiSnapshot())
    val uiSnapshot: StateFlow<AppUiSnapshot> = _uiSnapshot.asStateFlow()

    fun start() {
        if (started) return
        started = true
        scope.launch {
            timerStateHolder.state.collect { state ->
                observeTimerState(state)
            }
        }
        scope.launch {
            connectionSupervisor.snapshot.collectLatest { connection ->
                _uiSnapshot.update {
                    it.copy(
                        hostConnected = connection.phase != HostConnectionPhase.DISCONNECTED,
                        hostReady = connection.phase == HostConnectionPhase.READY,
                        recovering = connection.phase == HostConnectionPhase.CONNECTING ||
                            connection.phase == HostConnectionPhase.NEGOTIATING
                    )
                }
                if (connection.phase == HostConnectionPhase.READY) flushOutbox()
            }
        }
    }

    fun startLocal(durationMinutes: Int) = startNew(SessionMode.LOCAL, durationMinutes)

    fun startEdgeFocus(durationMinutes: Int) = startNew(SessionMode.EDGEFOCUS, durationMinutes)

    fun pause() = timerStateHolder.pauseSession()
    fun resume() = timerStateHolder.resumeSession()

    fun stop() {
        val state = timerStateHolder.state.value
        if (state.sessionMode == SessionMode.LOCAL && state.timerState in ACTIVE_STATES) {
            completionInProgress = true
            scope.launch {
                try {
                    persistCompletion(state, forceLocalStop = true)
                } finally {
                    completionInProgress = false
                }
            }
        }
        timerStateHolder.stopSession()
    }

    private fun startNew(requestedOwner: SessionMode, durationMinutes: Int) {
        val current = runtime
        val owner = SessionOwnershipPolicy.effectiveOwner(
            activeOwner = current?.takeIf { !it.saved }?.owner,
            requestedOwner = requestedOwner
        )
        if (current != null && !current.saved &&
            (timerStateHolder.state.value.timerState in ACTIVE_STATES ||
                timerStateHolder.state.value.isSyncingWithBoard || completionInProgress)
        ) return
        val now = System.currentTimeMillis()
        val sessionId = "session_$now"
        runtime = LocalSessionRuntime(sessionId, owner, now)
        if (owner == SessionMode.EDGEFOCUS) {
            timerStateHolder.startEdgeFocusSession(durationMinutes, now)
        } else {
            timerStateHolder.startLocalSession(durationMinutes, sessionId, now)
        }
    }

    private suspend fun observeTimerState(state: SharedTimerState) {
        if (runtime == null && state.timerState in ACTIVE_STATES && !state.sessionId.isNullOrBlank()) {
            runtime = LocalSessionRuntime(
                sessionId = state.sessionId,
                owner = state.sessionMode,
                startedAtMs = state.sessionStartTime.takeIf { it > 0L } ?: System.currentTimeMillis()
            )
        }

        if (state.timerState == TimerState.RUNNING && previousTimerState != TimerState.RUNNING) {
            startMonitor(state.sessionId ?: runtime?.sessionId)
        }
        val active = runtime
        val canPersistFinished = active?.saved == false &&
            (active.owner == SessionMode.LOCAL || timerStateHolder.hasBoardSessionReport())
        if (state.timerState == TimerState.FINISHED && canPersistFinished && !completionInProgress) {
            completionInProgress = true
            try {
                persistCompletion(state)
            } finally {
                completionInProgress = false
            }
        }
        if (state.timerState == TimerState.FINISHED && previousTimerState != TimerState.FINISHED) {
            stopMonitor()
        }
        if (state.timerState == TimerState.IDLE && previousTimerState in ACTIVE_STATES) {
            stopMonitor()
        }
        previousTimerState = state.timerState
        _uiSnapshot.value = AppUiSnapshot(
            timerState = state.timerState,
            owner = runtime?.owner ?: state.sessionMode,
            sessionId = runtime?.sessionId ?: state.sessionId,
            startTimeMs = runtime?.startedAtMs ?: state.sessionStartTime,
            timeRemainingMs = state.timeRemaining,
            totalTimeMs = state.totalTime,
            hostConnected = _uiSnapshot.value.hostConnected,
            hostReady = _uiSnapshot.value.hostReady,
            recovering = _uiSnapshot.value.recovering
        )
    }

    private suspend fun persistCompletion(state: SharedTimerState, forceLocalStop: Boolean = false) {
        mutex.withLock {
            val active = runtime ?: return
            if (active.saved) return
            val owner = active.owner
            val elapsedSeconds = if (owner == SessionMode.EDGEFOCUS && timerStateHolder.getBoardTotalDuration() > 0) {
                timerStateHolder.getBoardTotalDuration()
            } else {
                ((state.totalTime - state.timeRemaining) / 1000L).toInt()
            }.coerceAtLeast(0)
            val focusSeconds = if (owner == SessionMode.EDGEFOCUS && timerStateHolder.getBoardFocusDuration() > 0) {
                timerStateHolder.getBoardFocusDuration()
            } else {
                elapsedSeconds
            }.coerceAtLeast(0)
            val end = System.currentTimeMillis()
            val session = Session(
                id = active.sessionId,
                startTime = active.startedAtMs,
                endTime = end,
                duration = elapsedSeconds,
                focusDuration = focusSeconds,
                distractionCount = if (owner == SessionMode.EDGEFOCUS) {
                    timerStateHolder.getBoardSessionDistractionCount()
                } else {
                    maxOf(state.distractionCount, state.phoneDistractionCount)
                },
                reminderCount = if (owner == SessionMode.EDGEFOCUS) {
                    timerStateHolder.getBoardSessionReminderCount()
                } else 0,
                phoneDistractionCount = if (owner == SessionMode.EDGEFOCUS) {
                    timerStateHolder.getBoardSessionPhoneDistractionCount()
                } else state.phoneDistractionCount,
                boardSessionId = if (owner == SessionMode.EDGEFOCUS) state.sessionId ?: active.sessionId else null,
                source = owner.name,
                synced = owner == SessionMode.EDGEFOCUS && timerStateHolder.hasBoardSessionReport()
            )
            if (owner == SessionMode.LOCAL) {
                val upload = session.toUpload()
                persistenceStore.saveLocalCompletion(session, upload.toJson().toString(), end)
            } else if (!forceLocalStop && timerStateHolder.hasBoardSessionReport()) {
                persistenceStore.saveEdgeFocusCompletion(session)
            } else {
                return
            }
            runtime = active.copy(saved = true)
        }
        flushOutbox()
    }

    private suspend fun flushOutbox() {
        if (!connectionSupervisor.snapshot.value.isReady) return
        persistenceStore.dueUploads().forEach { item ->
            val upload = runCatching { localUploadFromJson(item.payloadJson) }.getOrNull() ?: run {
                persistenceStore.recordUploadFailure(
                    item.sessionId,
                    Long.MAX_VALUE,
                    "invalid_outbox_payload"
                )
                return@forEach
            }
            bleManager.uploadLocalSession(upload).onSuccess { response ->
                val acceptance = runCatching {
                    com.edgefocus.app.data.ble.HostJsonCodec.parseBusinessAcceptance(
                        String(response, Charsets.UTF_8)
                    )
                }.getOrNull()
                if (acceptance?.accepted != true) {
                    persistenceStore.recordUploadFailure(
                        item.sessionId,
                        System.currentTimeMillis() + retryDelayMs(item.attemptCount),
                        acceptance?.error ?: "upload_rejected"
                    )
                    return@onSuccess
                }
                val boardId = runCatching {
                    JSONObject(String(response, Charsets.UTF_8)).optJSONObject("data")
                        ?.optString("session_id")
                }.getOrNull().takeUnless { it.isNullOrBlank() } ?: item.sessionId
                persistenceStore.markUploaded(item.sessionId, boardId)
            }.onFailure { error ->
                val retryAt = System.currentTimeMillis() + retryDelayMs(item.attemptCount)
                persistenceStore.recordUploadFailure(
                    item.sessionId,
                    retryAt,
                    error.message ?: "upload_failed"
                )
            }
        }
    }

    private fun startMonitor(sessionId: String?) {
        if (monitorRunning) return
        AppMonitorService.startService(context, sessionId)
        monitorRunning = true
    }

    private fun stopMonitor() {
        if (!monitorRunning) return
        AppMonitorService.stopService(context)
        monitorRunning = false
    }

    private fun Session.toUpload() = LocalSessionUpload(
        sessionId = id,
        startTimeMs = startTime,
        endTimeMs = endTime ?: System.currentTimeMillis(),
        totalDurationSec = duration,
        focusDurationSec = focusDuration,
        distractionCount = distractionCount,
        reminderCount = reminderCount,
        phoneDistractionCount = phoneDistractionCount
    )

    private fun LocalSessionUpload.toJson() = JSONObject()
        .put("session_id", sessionId)
        .put("start_time_ms", startTimeMs)
        .put("end_time_ms", endTimeMs)
        .put("total_duration", totalDurationSec)
        .put("focus_duration", focusDurationSec)
        .put("distraction_count", distractionCount)
        .put("reminder_count", reminderCount)
        .put("phone_distraction_count", phoneDistractionCount)

    private fun localUploadFromJson(raw: String): LocalSessionUpload {
        val json = JSONObject(raw)
        return LocalSessionUpload(
            sessionId = json.getString("session_id"),
            startTimeMs = json.getLong("start_time_ms"),
            endTimeMs = json.getLong("end_time_ms"),
            totalDurationSec = json.getInt("total_duration"),
            focusDurationSec = json.getInt("focus_duration"),
            distractionCount = json.getInt("distraction_count"),
            reminderCount = json.getInt("reminder_count"),
            phoneDistractionCount = json.getInt("phone_distraction_count")
        )
    }

    private fun retryDelayMs(attemptCount: Int): Long =
        (1_000L shl attemptCount.coerceIn(0, 5)).coerceAtMost(30_000L)

    companion object {
        private val ACTIVE_STATES = setOf(TimerState.PRECHECK, TimerState.RUNNING, TimerState.PAUSED)
    }
}
