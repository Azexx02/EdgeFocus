package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.ble.model.GimbalCommandAcceptance
import com.edgefocus.app.data.ble.model.HostCommandException
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import com.edgefocus.app.data.remote.effectivePhase
import com.edgefocus.app.data.session.ProtocolCheckpointStore
import com.edgefocus.app.domain.model.TimerState
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

@Singleton
class PrecheckController @Inject constructor(
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor,
    private val checkpointStore: ProtocolCheckpointStore,
    private val timerStateHolder: TimerStateHolder
) {
    private data class BoardObservation(
        val response: ByteArray,
        val state: BoardSessionState
    )

    private val controllerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val confirmationGuard = Any()
    private var activeConfirmation: Deferred<Result<Unit>>? = null
    private val _isConfirming = MutableStateFlow(false)
    val isConfirming: StateFlow<Boolean> = _isConfirming.asStateFlow()

    private val manualInput = ManualGimbalInputCoordinator(
        scope = controllerScope,
        send = { sessionId, direction, fineTune ->
            sendGimbalCommand(
                command = "gimbal_nudge",
                sessionId = sessionId,
                direction = direction.wireValue,
                ownerId = bleManager.gimbalOwnerId,
                fineTune = fineTune
            )
        }
    )

    init {
        controllerScope.launch {
            connectionSupervisor.snapshot
                .map { it.connectionEpoch to it.isReady }
                .distinctUntilChanged()
                .collect { (_, ready) ->
                    if (!ready) {
                        manualInput.clear("主机连接已断开")
                    }
                }
        }
        controllerScope.launch {
            timerStateHolder.state
                .map { it.sessionId to it.timerState }
                .distinctUntilChanged()
                .collect { (_, timerState) ->
                    if (timerState != TimerState.PRECHECK) {
                        manualInput.clear("已退出 PRECHECK")
                    }
                }
        }
    }

    suspend fun nudge(direction: String, fineTune: Boolean): Result<Unit> = runCatching {
        check(!_isConfirming.value) { "正在进入专注会话，请稍候" }
        val current = timerStateHolder.state.value
        val sessionId = currentSessionId()
        check(current.timerState == TimerState.PRECHECK) { "主机已退出 PRECHECK" }
        ensureManualControlsAvailable(sessionId)
        bleManager.cancelPreviewRequests()
        manualInput.enqueue(
            sessionId = sessionId,
            leaseGeneration = null,
            direction = ManualGimbalDirection.fromWire(direction),
            fineTune = fineTune
        ).getOrThrow()
    }

    suspend fun center(): Result<Unit> = withManualControls { sessionId ->
        sendGimbalCommand(
            "gimbal_center",
            sessionId,
            ownerId = bleManager.gimbalOwnerId
        ).map { Unit }
    }

    suspend fun stop(): Result<Unit> = runCatching {
        check(!_isConfirming.value) { "正在进入专注会话，请稍候" }
        val sessionId = currentSessionId()
        val current = timerStateHolder.state.value
        check(current.timerState == TimerState.PRECHECK) { "主机已退出 PRECHECK" }
        manualInput.clear("已请求停止云台")
        bleManager.cancelPreviewRequests()
        bleManager.cancelGimbalControlRequests()
        sendGimbalCommand(
            "gimbal_stop",
            sessionId,
            ownerId = bleManager.gimbalOwnerId
        ).getOrThrow()
    }

    suspend fun confirmPositionAndContinue(): Result<Unit> = confirmationTask().await()

    suspend fun skipAutoAdjust(): Result<Unit> {
        if (_isConfirming.value) {
            return Result.failure(IllegalStateException("正在进入专注会话，请稍候"))
        }
        return runCatching { currentSessionId() }.fold(
            onSuccess = {
                bleManager.cancelPreviewRequests()
                stopAutomaticAdjustment(it)
            },
            onFailure = { Result.failure(it) }
        )
    }

    private fun confirmationTask(): Deferred<Result<Unit>> = synchronized(confirmationGuard) {
        activeConfirmation?.takeUnless { it.isCompleted } ?: run {
            _isConfirming.value = true
            lateinit var created: Deferred<Result<Unit>>
            created = controllerScope.async(start = CoroutineStart.LAZY) {
                runConfirmationTransaction()
            }
            activeConfirmation = created
            created.invokeOnCompletion {
                synchronized(confirmationGuard) {
                    if (activeConfirmation === created) {
                        activeConfirmation = null
                        _isConfirming.value = false
                    }
                }
            }
            created.start()
            created
        }
    }

    private suspend fun runConfirmationTransaction(): Result<Unit> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        checkPreparationReady()
        val sessionId = currentSessionId()
        manualInput.clear("正在确认当前构图")
        bleManager.cancelGimbalControlRequests()
        timerStateHolder.pausePrecheckPreviewRequests()

        val before = readBoardState().getOrThrow()
        check(before.state.sessionId == sessionId) { "PRECHECK 会话已变化" }
        when (PrecheckConfirmationPolicy.afterSessionControl(sessionId, before.state)) {
            SessionReconcileDecision.RUNNING -> {
                applyObservation(before, sessionId)
                return@runCatching
            }
            SessionReconcileDecision.STILL_PRECHECK -> Unit
            SessionReconcileDecision.SESSION_IDLE -> error("主机会话已结束")
            SessionReconcileDecision.SESSION_CHANGED -> error("PRECHECK 会话已变化")
            SessionReconcileDecision.UNKNOWN -> error("无法确认主机预检状态")
        }

        when (PrecheckConfirmationPolicy.gimbalReadiness(before.state.precheck?.gimbal)) {
            PrecheckGimbalReadiness.NO_GIMBAL,
            PrecheckGimbalReadiness.DEGRADED,
            PrecheckGimbalReadiness.POSITION_CONFIRMED -> Unit
            PrecheckGimbalReadiness.MANUAL_READY -> confirmPositionWithReconciliation(sessionId)
            PrecheckGimbalReadiness.BLOCKED_UNREADY ->
                error("云台尚未完成自动调整；请等待完成或点击跳过自动调整")
        }

        continueSessionWithReconciliation(sessionId).getOrThrow()
    }

    private suspend fun confirmPositionWithReconciliation(sessionId: String) {
        val positionResult = sendCommandBytes(
            "precheck_confirm_position",
            sessionId,
            ownerId = bleManager.gimbalOwnerId
        )
        if (positionResult.isSuccess) {
            val acceptance = HostJsonCodec.parseGimbalAcceptance(
                String(positionResult.getOrThrow(), Charsets.UTF_8)
            )
            check(acceptance.originChanged != true) { "主机错误地修改了永久前向原点" }
            return
        }

        val originalError = positionResult.exceptionOrNull()
        val reconciled = readBoardState().getOrElse {
            throw originalError ?: it
        }
        when (PrecheckConfirmationPolicy.afterPositionUncertain(sessionId, reconciled.state)) {
            PositionReconcileDecision.SESSION_RUNNING -> {
                applyObservation(reconciled, sessionId)
                return
            }
            PositionReconcileDecision.POSITION_CONFIRMED -> return
            PositionReconcileDecision.GIMBAL_DEGRADED -> return
            PositionReconcileDecision.RETRY_POSITION ->
                throw originalError ?: IllegalStateException("位置确认未完成，请重试")
            PositionReconcileDecision.SESSION_IDLE -> error("主机会话已结束")
            PositionReconcileDecision.SESSION_CHANGED -> error("PRECHECK 会话已变化")
            PositionReconcileDecision.UNKNOWN ->
                throw originalError ?: IllegalStateException("无法确认云台位置状态")
        }
    }

    private suspend fun continueSessionWithReconciliation(sessionId: String): Result<Unit> = runCatching {
        if (sharedStateIsFormal(sessionId)) return@runCatching

        val request = bleManager.sessionControl(
            action = "skip_precheck",
            sessionId = sessionId
        )
        if (request.isSuccess) {
            val response = request.getOrThrow()
            val acceptance = HostJsonCodec.parseBusinessAcceptance(String(response, Charsets.UTF_8))
            check(acceptance.accepted) { acceptance.error ?: "主机未接受继续专注" }
            val direct = timerStateHolder.applyBoardCommandResponse(response, sessionId)
            if (direct.isSuccess &&
                PrecheckConfirmationPolicy.afterSessionControl(
                    sessionId,
                    direct.getOrThrow()
                ) == SessionReconcileDecision.RUNNING
            ) {
                return@runCatching
            }
        }

        if (sharedStateIsFormal(sessionId)) return@runCatching
        val reconciled = readBoardState().getOrElse { error ->
            if (sharedStateIsFormal(sessionId)) return@runCatching
            throw request.exceptionOrNull() ?: error
        }
        when (PrecheckConfirmationPolicy.afterSessionControl(sessionId, reconciled.state)) {
            SessionReconcileDecision.RUNNING -> applyObservation(reconciled, sessionId)
            SessionReconcileDecision.STILL_PRECHECK -> error("主机仍在预检，请重试")
            SessionReconcileDecision.SESSION_IDLE -> error("主机会话已结束")
            SessionReconcileDecision.SESSION_CHANGED -> error("PRECHECK 会话已变化")
            SessionReconcileDecision.UNKNOWN -> error("无法确认主机会话状态")
        }
    }

    private suspend fun stopAutomaticAdjustment(sessionId: String): Result<Unit> = runCatching {
        sendGimbalCommand(
            "precheck_skip_auto_adjust",
            sessionId,
            ownerId = bleManager.gimbalOwnerId
        ).getOrThrow()
        val deadline = System.nanoTime() + SKIP_SETTLE_TIMEOUT_MS * 1_000_000L
        while (System.nanoTime() < deadline) {
            val observation = readBoardState().getOrThrow()
            check(observation.state.sessionId == sessionId) { "PRECHECK 会话已变化" }
            check(observation.state.effectivePhase() == BoardSessionPhase.PRECHECK) {
                "主机已退出 PRECHECK"
            }
            applyObservation(observation, sessionId)
            if (PrecheckConfirmationPolicy.gimbalReadiness(observation.state.precheck?.gimbal) !=
                PrecheckGimbalReadiness.BLOCKED_UNREADY
            ) {
                return@runCatching
            }
            delay(SKIP_SETTLE_POLL_MS)
        }
        error("主机未及时结束云台自动调整")
    }

    private suspend fun withManualControls(
        command: suspend (String) -> Result<Unit>
    ): Result<Unit> {
        if (_isConfirming.value) {
            return Result.failure(IllegalStateException("正在进入专注会话，请稍候"))
        }
        return runCatching { currentSessionId() }.fold(
            onSuccess = { expectedSessionId ->
                runCatching {
                    val current = timerStateHolder.state.value
                    check(current.timerState == TimerState.PRECHECK &&
                        current.sessionId == expectedSessionId
                    ) { "PRECHECK 会话已变化" }
                    ensureManualControlsAvailable(expectedSessionId)
                    manualInput.clear("已请求云台控制命令")
                    bleManager.cancelPreviewRequests()
                    bleManager.cancelGimbalControlRequests()
                    command(expectedSessionId).getOrThrow()
                }
            },
            onFailure = { Result.failure(it) }
        )
    }

    private fun currentSessionId(): String = timerStateHolder.state.value.sessionId
        ?.takeIf { it.isNotBlank() }
        ?: error("缺少 PRECHECK session_id")

    private suspend fun readBoardState(): Result<BoardObservation> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        val response = bleManager.getCurrentState().getOrThrow()
        BoardObservation(
            response = response,
            state = BoardJsonParser.parseSessionState(String(response, Charsets.UTF_8))
        )
    }

    private suspend fun sendCommand(
        command: String,
        sessionId: String,
        direction: String? = null
    ): Result<Unit> = sendGimbalCommand(command, sessionId, direction).map { Unit }

    private suspend fun sendGimbalCommand(
        command: String,
        sessionId: String,
        direction: String? = null,
        ownerId: String? = null,
        leaseToken: String? = null,
        fineTune: Boolean? = null,
        confirmed: Boolean? = null
    ): Result<GimbalCommandAcceptance> = runCatching {
        val bytes = sendCommandBytes(
            command, sessionId, direction,
            ownerId, leaseToken, fineTune, confirmed
        ).getOrThrow()
        val acceptance = HostJsonCodec.parseGimbalAcceptance(String(bytes, Charsets.UTF_8))
        check(acceptance.accepted) { acceptance.error ?: "$command 被主机拒绝" }
        acceptance
    }.recoverCatching { error ->
        if (error is HostCommandException && error.errorCode == "STALE_SEQUENCE") {
            timerStateHolder.refreshBoardState()
        }
        throw friendlyGimbalError(error)
    }

    private suspend fun sendCommandBytes(
        command: String,
        sessionId: String,
        direction: String? = null,
        ownerId: String? = null,
        leaseToken: String? = null,
        fineTune: Boolean? = null,
        confirmed: Boolean? = null
    ): Result<ByteArray> = runCatching {
        check(connectionSupervisor.snapshot.value.isReady) { "主机尚未就绪" }
        if (command != "precheck_skip_auto_adjust") checkPreparationReady()
        val sequence = checkpointStore.allocateGimbalSequence(sessionId)
        val response = bleManager.gimbalPrecheckCommand(
            command,
            sessionId,
            sequence,
            direction,
            ownerId,
            leaseToken,
            fineTune,
            confirmed
        )
            .getOrThrow()
        val accepted = HostJsonCodec.parseGimbalAcceptance(String(response, Charsets.UTF_8))
        check(accepted.accepted) { accepted.error ?: "$command 被主机拒绝" }
        if (command == "precheck_skip_auto_adjust") {
            timerStateHolder.refreshBoardState()
        }
        response
    }

    private fun ensureManualControlsAvailable(sessionId: String) {
        val current = timerStateHolder.state.value
        check(current.timerState == TimerState.PRECHECK && current.sessionId == sessionId) {
            "PRECHECK 会话已变化"
        }
        val gimbal = current.precheck?.gimbal ?: error("主机未提供云台状态")
        check(!gimbal.state.equals("degraded", ignoreCase = true)) {
            "云台已降级，当前无法手动调整"
        }
        check(gimbal.state.lowercase() !in setOf("searching", "centering", "framing")) {
            "云台正在自动调整；请先点击跳过自动调整"
        }
        check(PrecheckConfirmationPolicy.canUseManualControls(gimbal)) {
            "主机尚未开放云台手动调整"
        }
    }

    private fun friendlyGimbalError(error: Throwable): Throwable {
        val commandError = error as? HostCommandException ?: return error
        val message = when (commandError.errorCode) {
            "MANUAL_LEASE_REQUIRED" -> "主机仍要求旧版控制租约，请确认主机已更新"
            "STALE_SEQUENCE" -> "云台状态已更新，请重试"
            "COMMUNICATION_DEGRADED" -> "云台通信不稳定，请稍后重试"
            "HARDWARE_FAULT" -> "云台发生硬件故障，请检查主机或云台状态"
            "CALIBRATION_REQUIRED" -> "云台需要维护校准"
            "ORIGIN_REQUIRED" -> "云台尚未设置正前方"
            "TARGET_OUT_OF_SAFE_RANGE" -> "已到达云台安全活动范围"
            else -> commandError.message ?: "云台操作失败"
        }
        return HostCommandException(
            message = message,
            errorCode = commandError.errorCode,
            retryable = commandError.retryable,
            responseData = commandError.responseData
        )
    }

    private fun applyObservation(observation: BoardObservation, sessionId: String) {
        timerStateHolder.applyBoardCommandResponse(observation.response, sessionId).getOrThrow()
    }

    private fun sharedStateIsFormal(sessionId: String): Boolean {
        val shared = timerStateHolder.state.value
        return shared.sessionId == sessionId &&
            (shared.timerState == TimerState.RUNNING || shared.timerState == TimerState.PAUSED)
    }

    private fun checkPreparationReady() {
        val preparation = timerStateHolder.state.value.preparationState
        check(preparation == BoardPreparationState.READY || preparation == BoardPreparationState.DEGRADED) {
            timerStateHolder.state.value.preparationError ?: "EdgeFocus 正在准备预检，请稍候"
        }
    }

    private companion object {
        const val SKIP_SETTLE_TIMEOUT_MS = 3_000L
        const val SKIP_SETTLE_POLL_MS = 150L
    }
}
