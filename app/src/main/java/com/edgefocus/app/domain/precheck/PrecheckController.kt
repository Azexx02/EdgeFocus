package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.ble.model.HostSessionPhase
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.effectivePhase
import com.edgefocus.app.data.session.ProtocolCheckpointStore
import com.edgefocus.app.domain.model.TimerState
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeout

@Singleton
class PrecheckController @Inject constructor(
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor,
    private val checkpointStore: ProtocolCheckpointStore,
    private val timerStateHolder: TimerStateHolder
) {
    private val manualTransactions = ManualGimbalTransactionCoordinator(
        observe = ::readManualLease,
        requestSkipAuto = { sessionId ->
            sendCommand("precheck_skip_auto_adjust", sessionId)
        }
    )

    suspend fun nudge(direction: String): Result<Unit> = withManualLease { sessionId ->
        sendCommand("gimbal_nudge", sessionId, direction.uppercase())
    }

    suspend fun center(): Result<Unit> = withManualLease { sessionId ->
        sendCommand("gimbal_center", sessionId)
    }

    suspend fun stop(): Result<Unit> = withManualLease { sessionId ->
        sendCommand("gimbal_stop", sessionId)
    }

    suspend fun realign(): Result<Unit> = withManualLease { sessionId ->
        sendCommand("gimbal_realign", sessionId)
    }

    suspend fun confirmPositionAndContinue(): Result<Unit> {
        val command: suspend (String) -> Result<Unit> = { sessionId ->
            runCatching {
                sendCommand("precheck_confirm_position", sessionId).getOrThrow()
                continueSession(sessionId).getOrThrow()
            }
        }
        return if (timerStateHolder.state.value.precheck?.gimbal != null) {
            withManualLease(command)
        } else {
            manualTransactions.runCommand { continueSession(currentSessionId()) }
        }
    }

    suspend fun skipAutoAdjust(): Result<Unit> = runCatching { currentSessionId() }
        .fold(
            onSuccess = { manualTransactions.takeOver(it) },
            onFailure = { Result.failure(it) }
        )

    private suspend fun withManualLease(
        command: suspend (String) -> Result<Unit>
    ): Result<Unit> = runCatching { currentSessionId() }
        .fold(
            onSuccess = { expectedSessionId ->
                manualTransactions.runCommand {
                    runCatching {
                        // The PRECHECK page is continuously fed by authoritative board
                        // snapshots. Re-reading the full state here delayed a tap by up
                        // to seven seconds behind state/preview BLE chunks. Validate the
                        // already displayed lease and let the critical control command
                        // go next instead.
                        val current = timerStateHolder.state.value
                        check(current.timerState == TimerState.PRECHECK &&
                            current.sessionId == expectedSessionId
                        ) { "PRECHECK 会话已变化" }
                        check(current.precheck?.gimbal?.manualLease == true) {
                            "云台正在自动调整；请等待完成或点击跳过自动调整"
                        }
                        bleManager.cancelPreviewRequests()
                        command(expectedSessionId).getOrThrow()
                    }
                }
            },
            onFailure = { Result.failure(it) }
        )

    private fun currentSessionId(): String = timerStateHolder.state.value.sessionId
        ?.takeIf { it.isNotBlank() }
        ?: error("缺少 PRECHECK session_id")

    private suspend fun readManualLease(): Result<ManualLeaseObservation> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        val response = bleManager.getCurrentState().getOrThrow()
        val state = BoardJsonParser.parseSessionState(String(response, Charsets.UTF_8))
        ManualLeaseObservation(
            sessionId = state.sessionId,
            phase = state.effectivePhase(),
            manualLease = state.precheck?.gimbal?.manualLease == true
        )
    }

    private suspend fun sendCommand(
        command: String,
        sessionId: String,
        direction: String? = null
    ): Result<Unit> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        checkPreparationReady()
        val sequence = checkpointStore.allocateGimbalSequence(sessionId)
        val response = bleManager.gimbalPrecheckCommand(command, sessionId, sequence, direction)
            .getOrThrow()
        val accepted = HostJsonCodec.parseBusinessAcceptance(String(response, Charsets.UTF_8))
        check(accepted.accepted) { accepted.error ?: "$command 被主机拒绝" }
        // Manual motion commands are already verified by the board before it
        // responds. Avoid immediately filling BLE with a large state snapshot;
        // takeover/recovery commands still refresh because they change UI
        // capability and lease state.
        if (command == "precheck_skip_auto_adjust" || command == "gimbal_realign") {
            timerStateHolder.refreshBoardState()
        }
    }

    private suspend fun continueSession(sessionId: String): Result<Unit> = runCatching {
        checkPreparationReady()
        val observation = readManualLease().getOrThrow()
        check(observation.sessionId == sessionId &&
            observation.phase == com.edgefocus.app.data.remote.BoardSessionPhase.PRECHECK
        ) { "PRECHECK 会话已变化" }
        val response = bleManager.sessionControl(
            action = "skip_precheck",
            sessionId = sessionId
        ).getOrThrow()
        val accepted = HostJsonCodec.parseBusinessAcceptance(String(response, Charsets.UTF_8))
        check(accepted.accepted) { accepted.error ?: "主机未接受继续专注" }
        withTimeout(10_000L) {
            while (true) {
                val stateResponse = bleManager.getCurrentState().getOrThrow()
                val state = HostJsonCodec.parseState(String(stateResponse, Charsets.UTF_8))
                if (state.session.phase == HostSessionPhase.RUNNING) break
                delay(250L)
            }
        }
        timerStateHolder.refreshBoardState()
    }

    private fun checkPreparationReady() {
        val preparation = timerStateHolder.state.value.preparationState
        check(preparation == BoardPreparationState.READY || preparation == BoardPreparationState.DEGRADED) {
            timerStateHolder.state.value.preparationError ?: "EdgeFocus 正在准备预检，请稍候"
        }
    }
}
