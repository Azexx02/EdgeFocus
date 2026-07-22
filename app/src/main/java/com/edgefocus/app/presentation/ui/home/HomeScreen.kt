package com.edgefocus.app.presentation.ui.home

import androidx.compose.foundation.Image
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Work
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.PowerProfile
import com.edgefocus.app.data.remote.BoardPreparationState
import com.edgefocus.app.data.ble.model.ExternalPowerState
import com.edgefocus.app.presentation.shared.rememberCameraPreviewBitmap
import com.edgefocus.app.presentation.shared.GimbalNudgeButton
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.domain.precheck.PrecheckConfirmationPolicy
import com.edgefocus.app.domain.precheck.PrecheckGimbalReadiness
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.HomeViewModel
import com.edgefocus.app.presentation.viewmodel.SettingsViewModel
import com.edgefocus.app.util.TimeUtils

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel(),
    settingsViewModel: SettingsViewModel = hiltViewModel(),
    onNavigateToControlPanel: () -> Unit = {}
) {
    val uiState by viewModel.uiState.collectAsState()
    val settings by settingsViewModel.settings.collectAsState()
    val connectionState by bleViewModel.connectionState.collectAsState()
    val effectiveConnectionState = if (bleViewModel.isSimulatingBle()) {
        bleViewModel.getEffectiveConnectionState()
    } else {
        connectionState
    }

    val useBoard = settings.useEdgeFocusBoard
    val isConnected = effectiveConnectionState == ConnectionState.CONNECTED &&
        bleViewModel.isConnected()

    val canStart = when {
        useBoard && (uiState.timerState == TimerState.IDLE ||
            uiState.timerState == TimerState.FINISHED) -> isConnected && !uiState.isSyncingWithBoard
        else -> true
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Top Bar
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Spacer(modifier = Modifier.size(48.dp))
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    text = "EdgeFocus",
                    style = MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.pointerInput(Unit) {
                        detectTapGestures(
                            onLongPress = { onNavigateToControlPanel() }
                        )
                    }
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .background(
                                if (isConnected) MaterialTheme.colorScheme.secondary
                                else MaterialTheme.colorScheme.surfaceVariant,
                                CircleShape
                            )
                    )
                Text(
                    text = when {
                        !isConnected && useBoard -> "设备未连接"
                        useBoard -> "EdgeFocus 模式 · ${powerStatusText(uiState.boardPowerProfile, uiState.externalPowerState)}"
                        else -> "本地模式"
                    },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            IconButton(onClick = { }) {
                Icon(
                    if (isConnected) Icons.Default.BluetoothConnected
                    else Icons.Default.BluetoothDisabled,
                    contentDescription = "蓝牙",
                    tint = if (isConnected) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(20.dp)
                )
            }
        }

        if (uiState.timerState == TimerState.PRECHECK) {
            HomePrecheckPage(
                uiState = uiState,
                viewModel = viewModel,
                modifier = Modifier.weight(1f)
            )
        } else {
            Spacer(modifier = Modifier.height(32.dp))

        // Context Tag
        val sessionTypeText = when (uiState.sessionType) {
            SessionType.WORK -> "专注会话"
        }
        Row(
            modifier = Modifier
                .background(MaterialTheme.colorScheme.surfaceVariant, CircleShape)
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                Icons.Default.Work,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = sessionTypeText,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Spacer(modifier = Modifier.height(48.dp))

        // Timer Circle
        Box(
            modifier = Modifier
                .size(300.dp)
                .shadow(
                    20.dp,
                    CircleShape,
                    spotColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                )
                .background(MaterialTheme.colorScheme.surface, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            if (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED) {
                DurationSelectorRing(
                    selectedMinutes = viewModel.getWorkDurationMinutes(),
                    onDurationChanged = { viewModel.setWorkDuration(it) }
                )
            } else {
                PomodoroTimer(
                    timeMillis = uiState.timeRemaining,
                    totalTimeMillis = uiState.totalTime
                )
            }
        }

        Spacer(modifier = Modifier.height(48.dp))

        Text(
            text = "已完成会话：${uiState.completedSessions}",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Controls
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(64.dp),
            contentAlignment = Alignment.Center
        ) {
            if (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED) {
                TactileButton(
                    onClick = { viewModel.startTimer(useBoard && isConnected) },
                    modifier = Modifier
                        .fillMaxWidth(0.8f)
                        .fillMaxHeight(),
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = Color.White,
                    enabled = canStart
                ) {
                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        if (useBoard && isConnected) "开始 EdgeFocus" else "开始专注",
                        style = MaterialTheme.typography.headlineMedium
                    )
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(0.9f),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    TactileButton(
                        onClick = {
                            if (uiState.timerState == TimerState.RUNNING) {
                                viewModel.pauseTimer()
                            } else {
                                viewModel.startTimer(useBoard && isConnected)
                            }
                        },
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        contentColor = MaterialTheme.colorScheme.onSurface
                    ) {
                        Icon(
                            if (uiState.timerState == TimerState.RUNNING)
                                Icons.Default.Pause
                            else Icons.Default.PlayArrow,
                            contentDescription = null
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            if (uiState.timerState == TimerState.RUNNING)
                                "暂停"
                            else "继续",
                            style = MaterialTheme.typography.headlineMedium
                        )
                    }
                    TactileButton(
                        onClick = { viewModel.stopTimer() },
                        modifier = Modifier.size(88.dp),
                        containerColor = MaterialTheme.colorScheme.surface,
                        contentColor = MaterialTheme.colorScheme.error
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null, modifier = Modifier.size(40.dp))
                    }
                }
            }
        }

            if (useBoard && uiState.isSyncingWithBoard &&
                (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED)
            ) {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "正在等待 EdgeFocus 确认会话",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else if (useBoard && !isConnected &&
                (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED)
            ) {
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = "连接设备后可使用 EdgeFocus 模式",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

@Composable
private fun HomePrecheckPage(
    uiState: com.edgefocus.app.presentation.viewmodel.TimerUiState,
    viewModel: HomeViewModel,
    modifier: Modifier = Modifier
) {
    val preparationReady = uiState.preparationState == BoardPreparationState.READY ||
        uiState.preparationState == BoardPreparationState.DEGRADED
    val gimbal = uiState.precheck?.gimbal
    val gimbalReadiness = PrecheckConfirmationPolicy.gimbalReadiness(gimbal)
    val manualControlsReady = preparationReady &&
        !uiState.isPrecheckConfirming &&
        uiState.modernGimbalControlAvailable &&
        PrecheckConfirmationPolicy.canUseManualControls(gimbal)
    val confirmationReady = PrecheckConfirmationPolicy.canConfirm(
        preparationReady = preparationReady,
        isConfirming = uiState.isPrecheckConfirming,
        gimbal = gimbal
    )
    val skipAutoAdjustReady = !uiState.isPrecheckConfirming &&
        PrecheckConfirmationPolicy.canSkipAutoAdjust(gimbal)
    Box(modifier = modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(top = 16.dp, bottom = 132.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
        Text("开始前预检", style = MaterialTheme.typography.headlineMedium)
        Text(
            when (uiState.preparationState) {
                BoardPreparationState.INITIALIZING -> "EdgeFocus 正在准备摄像头与预检功能…"
                BoardPreparationState.FAILED -> uiState.preparationError ?: "EdgeFocus 预检准备失败"
                else -> uiState.precheckGuidance ?: "请确认画面、距离和坐姿，再开始正式专注。"
            },
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                val preview = uiState.cameraPreview
                val bitmap by rememberCameraPreviewBitmap(preview)
                val decodedBitmap = bitmap
                Box(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(140.dp),
                    contentAlignment = Alignment.Center
                ) {
                    if (preview?.available == true && decodedBitmap != null) {
                        Image(
                            bitmap = decodedBitmap.asImageBitmap(),
                            contentDescription = "主机摄像头预览",
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit
                        )
                    } else {
                        Text("暂无预览，点击刷新获取最近一帧。")
                    }
                }
                OutlinedButton(
                    onClick = viewModel::refreshCameraPreview,
                    enabled = preparationReady && !uiState.isPrecheckConfirming
                ) {
                    Text("刷新画面")
                }
            }
        }

        if (gimbal != null) {
            Text("云台微调", style = MaterialTheme.typography.titleMedium)
            Text(
                when (gimbalReadiness) {
                    PrecheckGimbalReadiness.DEGRADED -> "云台已降级，将以固定机位继续。"
                    PrecheckGimbalReadiness.POSITION_CONFIRMED -> "云台位置已确认，可以开始专注。"
                    PrecheckGimbalReadiness.MANUAL_READY -> "自动调整已结束，可以手动微调。"
                    else -> "自动调整中；完成后可微调，也可点击跳过。"
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (!uiState.modernGimbalControlAvailable) {
                Text(
                    "主机需升级后才能使用新版连续云台微调。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
            if (gimbal.hardwareFaultActive || gimbal.state.equals("degraded", ignoreCase = true)) {
                OutlinedButton(
                    onClick = viewModel::recoverGimbal,
                    enabled = preparationReady && uiState.modernGimbalControlAvailable
                ) {
                    Text("恢复云台")
                }
            }
            if (gimbal.currentYawDeg != null || gimbal.currentPitchDeg != null) {
                Text(
                    "当前位置 偏航 ${gimbal.currentYawDeg?.toInt() ?: "-"}° · " +
                        "俯仰 ${gimbal.currentPitchDeg?.toInt() ?: "-"}°",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (gimbal.targetYawDeg != null || gimbal.targetPitchDeg != null) {
                Text(
                    "目标 偏航 ${gimbal.targetYawDeg?.toInt() ?: "-"}° · " +
                        "俯仰 ${gimbal.targetPitchDeg?.toInt() ?: "-"}° · " +
                        if (gimbal.settled == true) "已到位" else "运动中",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            GimbalNudgeButton(
                label = "上",
                enabled = manualControlsReady,
                onNudge = { viewModel.nudgeGimbal("UP", it) }
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GimbalNudgeButton(
                    label = "左",
                    enabled = manualControlsReady,
                    onNudge = { viewModel.nudgeGimbal("LEFT", it) }
                )
                OutlinedButton(onClick = viewModel::centerGimbal, enabled = manualControlsReady) { Text("归中") }
                GimbalNudgeButton(
                    label = "右",
                    enabled = manualControlsReady,
                    onNudge = { viewModel.nudgeGimbal("RIGHT", it) }
                )
            }
            GimbalNudgeButton(
                label = "下",
                enabled = manualControlsReady,
                onNudge = { viewModel.nudgeGimbal("DOWN", it) }
            )
            TextButton(onClick = viewModel::stopGimbal, enabled = manualControlsReady) { Text("停止云台") }
        }

            uiState.boardErrorMessage?.let {
                Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
        }

        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.background)
                .padding(top = 8.dp, bottom = 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(
                    onClick = viewModel::skipPrecheckAutoAdjust,
                    enabled = skipAutoAdjustReady
                ) {
                    Text("跳过自动调整")
                }
                TactileButton(
                    onClick = viewModel::confirmPrecheck,
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = Color.White,
                    enabled = confirmationReady
                ) {
                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(if (uiState.isPrecheckConfirming) "正在进入专注…" else "确认并开始")
                }
            }
            TextButton(onClick = viewModel::stopTimer) { Text("取消本次专注") }
        }
    }
}

private fun powerStatusText(
    profile: PowerProfile,
    externalPowerState: ExternalPowerState
): String {
    val profileText = when (profile) {
        PowerProfile.HOME -> "HOME"
        PowerProfile.GO -> "GO"
    }
    val sourceText = when (externalPowerState) {
        ExternalPowerState.PRESENT -> "外部供电"
        ExternalPowerState.ABSENT -> "移动供电"
        ExternalPowerState.UNKNOWN -> "供电未知"
    }
    return "$profileText · $sourceText"
}

@Composable
fun TactileButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color,
    contentColor: Color,
    enabled: Boolean = true,
    content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "button_scale"
    )

    Surface(
        modifier = modifier
            .scale(scale)
            .shadow(
                if (isPressed) 2.dp else 8.dp,
                CircleShape,
                spotColor = containerColor.copy(alpha = 0.5f)
            )
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = onClick
            ),
        shape = CircleShape,
        color = if (enabled) containerColor else containerColor.copy(alpha = 0.5f),
        contentColor = if (enabled) contentColor else contentColor.copy(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 24.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
            content = content
        )
    }
}
