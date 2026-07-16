package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.session.ProtocolCheckpointStore
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeout
import com.edgefocus.app.data.ble.model.HostSessionPhase

@Singleton
class PrecheckController @Inject constructor(
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor,
    private val checkpointStore: ProtocolCheckpointStore,
    private val timerStateHolder: TimerStateHolder
) {
    suspend fun nudge(direction: String): Result<Unit> =
        send("gimbal_nudge", direction.uppercase())

    suspend fun center(): Result<Unit> = send("gimbal_center")

    suspend fun stop(): Result<Unit> = send("gimbal_stop")

    suspend fun realign(): Result<Unit> = send("gimbal_realign")

    suspend fun confirmPositionAndContinue(): Result<Unit> = runCatching {
        if (timerStateHolder.state.value.precheck?.gimbal != null) {
            send("precheck_confirm_position").getOrThrow()
        }
        continueSession().getOrThrow()
    }

    suspend fun skipAutoAdjustAndContinue(): Result<Unit> = runCatching {
        if (timerStateHolder.state.value.precheck?.gimbal != null) {
            send("precheck_skip_auto_adjust").getOrThrow()
        }
        continueSession().getOrThrow()
    }

    private suspend fun send(command: String, direction: String? = null): Result<Unit> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        val sessionId = timerStateHolder.state.value.sessionId
            ?.takeIf { it.isNotBlank() }
            ?: error("缺少 PRECHECK session_id")
        val sequence = checkpointStore.allocateGimbalSequence(sessionId)
        val response = bleManager.gimbalPrecheckCommand(command, sessionId, sequence, direction)
            .getOrThrow()
        val accepted = HostJsonCodec.parseBusinessAcceptance(String(response, Charsets.UTF_8))
        check(accepted.accepted) { accepted.error ?: "$command 被主机拒绝" }
        timerStateHolder.refreshBoardState()
    }

    private suspend fun continueSession(): Result<Unit> = runCatching {
        val response = bleManager.sessionControl("skip_precheck").getOrThrow()
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
}
