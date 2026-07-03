package com.edgefocus.app.presentation.ui.controlpanel

import android.media.AudioManager
import android.media.ToneGenerator
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedIconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.foundation.verticalScroll
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.ui.home.DurationSelectorRing
import com.edgefocus.app.presentation.ui.home.PomodoroTimer
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.ControlPanelViewModel
import com.edgefocus.app.util.TimeUtils

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ControlPanelScreen(
    viewModel: ControlPanelViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val pagerState = rememberPagerState(pageCount = { 3 })
    val context = LocalContext.current
    val effectiveConnectionState = bleViewModel.getEffectiveConnectionState()
    val alert = uiState.lastAlert

    LaunchedEffect(alert) {
        if (alert?.phoneActions?.any { it.equals("SOUND", ignoreCase = true) || it.equals("sound", ignoreCase = true) } == true) {
            val volume = ((alert.soundVolume ?: uiState.soundVolume).coerceIn(0f, 1f) * 100).toInt()
            ToneGenerator(AudioManager.STREAM_NOTIFICATION, volume)
                .startTone(ToneGenerator.TONE_PROP_BEEP, 180)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        // Top bar - compact
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = {
                (context as? android.app.Activity)?.finish()
            }, modifier = Modifier.size(36.dp)) {
                Icon(
                    Icons.Default.ArrowBack,
                    contentDescription = "Back",
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(20.dp)
                )
            }
            Text(
                "EdgeFocus",
                style = MaterialTheme.typography.titleLarge,
                color = MaterialTheme.colorScheme.primary
            )
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Icon(
                    Icons.Default.BluetoothConnected,
                    contentDescription = "Bluetooth",
                    tint = if (effectiveConnectionState == ConnectionState.CONNECTED)
                        MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(18.dp)
                )
                Text(
                    text = when (effectiveConnectionState) {
                        ConnectionState.CONNECTED -> if (bleViewModel.isSimulatingBle()) "模拟" else "已连接"
                        ConnectionState.SCANNING -> "扫描"
                        ConnectionState.CONNECTING -> "连接中"
                        ConnectionState.ERROR -> "错误"
                        ConnectionState.DISCONNECTED -> "离线"
                    },
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }

        HorizontalPager(
            state = pagerState,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
        ) { page ->
            when (page) {
                0 -> TimerPage(viewModel = viewModel, uiState = uiState)
                1 -> StatsPage(uiState = uiState)
                2 -> SettingsPage(uiState = uiState, viewModel = viewModel)
            }
        }

        // Pager indicator - minimal
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(18.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            repeat(3) { index ->
                val color = if (pagerState.currentPage == index)
                    MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.surfaceVariant
                Box(
                    modifier = Modifier
                        .padding(horizontal = 3.dp)
                        .size(5.dp)
                        .background(color, CircleShape)
                )
            }
        }
    }

    if (alert?.phoneActions?.any { it.equals("SCREEN", ignoreCase = true) || it.equals("screen", ignoreCase = true) } == true) {
        Dialog(
            onDismissRequest = viewModel::dismissAlert,
            properties = DialogProperties(usePlatformDefaultWidth = false)
        ) {
            Surface(
                modifier = Modifier.fillMaxSize(),
                color = MaterialTheme.colorScheme.errorContainer
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(32.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center
                ) {
                    Text(
                        "专注提醒",
                        style = MaterialTheme.typography.headlineLarge,
                        color = MaterialTheme.colorScheme.onErrorContainer
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        alert.message.ifBlank { "请回到专注状态" },
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onErrorContainer
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "提醒等级 L${alert.level}",
                        style = MaterialTheme.typography.labelLarge,
                        color = MaterialTheme.colorScheme.onErrorContainer
                    )
                    Spacer(modifier = Modifier.height(32.dp))
                    OutlinedButton(onClick = viewModel::dismissAlert) {
                        Text("我知道了")
                    }
                }
            }
        }
    }
}

// ==================== Page 1: Timer ====================

@Composable
private fun TimerPage(
    viewModel: ControlPanelViewModel,
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState
) {
    val isRunning = uiState.timerState == TimerState.RUNNING
    val isIdle = uiState.timerState == TimerState.IDLE

    if (uiState.timerState == TimerState.PRECHECK) {
        PrecheckPage(viewModel = viewModel, uiState = uiState)
        return
    }

    Row(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 32.dp),
        horizontalArrangement = Arrangement.spacedBy(48.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Left: Timer ring + Remaining label
        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Box(
                contentAlignment = Alignment.Center,
                modifier = Modifier.size(240.dp)
            ) {
                if (isIdle) {
                    DurationSelectorRing(
                        selectedMinutes = uiState.selectedDuration,
                        onDurationChanged = { viewModel.updateSelectedDuration(it) },
                        sizeDp = 240
                    )
                } else {
                    PomodoroTimer(
                        timeMillis = uiState.timeRemaining,
                        totalTimeMillis = uiState.totalTime,
                        sizeDp = 240
                    )
                }
            }
            Spacer(modifier = Modifier.height(2.dp))
            Text(
                text = "Remaining",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                OutlinedIconButton(
                    onClick = { viewModel.stopSession() },
                    modifier = Modifier.size(56.dp),
                    enabled = uiState.timerState == TimerState.RUNNING ||
                        uiState.timerState == TimerState.PAUSED
                ) {
                    Icon(Icons.Default.Stop, contentDescription = "Stop", modifier = Modifier.size(28.dp))
                }

                FilledIconButton(
                    onClick = {
                        when (uiState.timerState) {
                            TimerState.RUNNING -> viewModel.pauseSession()
                            TimerState.PAUSED -> viewModel.resumeSession()
                            else -> viewModel.startSession()
                        }
                    },
                    modifier = Modifier.size(80.dp),
                    enabled = uiState.timerState == TimerState.RUNNING ||
                        uiState.timerState == TimerState.PAUSED ||
                        (uiState.isBoardConnected && !uiState.isSyncingWithBoard),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = MaterialTheme.colorScheme.primary
                    )
                ) {
                    Icon(
                        if (isRunning) Icons.Default.Pause else Icons.Default.PlayArrow,
                        contentDescription = if (isRunning) "Pause" else "Start",
                        tint = Color.White,
                        modifier = Modifier.size(40.dp)
                    )
                }

                OutlinedIconButton(
                    onClick = { viewModel.pauseSession() },
                    modifier = Modifier.size(56.dp),
                    enabled = isRunning
                ) {
                    Icon(Icons.Default.Pause, contentDescription = "Pause", modifier = Modifier.size(28.dp))
                }
            }

            if (uiState.isSyncingWithBoard &&
                (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED)
            ) {
                Text(
                    text = "正在等待 EdgeFocus 确认",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            } else if (!uiState.isBoardConnected &&
                (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED)
            ) {
                Text(
                    text = "请先连接已绑定的 EdgeFocus 设备",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }
    }
}

@Composable
private fun PrecheckPage(
    viewModel: ControlPanelViewModel,
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState
) {
    Row(
        modifier = Modifier
            .fillMaxSize()
            .padding(horizontal = 24.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Card(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
        ) {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text("摄像头预检", style = MaterialTheme.typography.titleMedium)
                Text(
                    "请确认画面、距离和坐姿。预览是拍照式刷新，不是实时视频。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                uiState.precheckGuidance?.let {
                    Text(
                        "主机提示：$it",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
                val preview = uiState.cameraPreview
                if (preview?.available == true) {
                    StatRow("预览尺寸", "${preview.width} x ${preview.height}")
                    StatRow("来源", preview.source ?: "主机摄像头")
                    StatRow("序号", "${preview.sequence}")
                } else {
                    Text(
                        "暂无预览，点击刷新获取最近一帧。",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
                uiState.boardErrorMessage?.let {
                    Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.error)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = viewModel::refreshCameraPreview) {
                        Text("刷新画面")
                    }
                    OutlinedButton(onClick = viewModel::skipPrecheck) {
                        Text("继续")
                    }
                    TextButton(onClick = viewModel::skipCalibration) {
                        Text("跳过")
                    }
                }
            }
        }

        if (uiState.precheck?.gimbal != null) {
            Card(
                modifier = Modifier.weight(1f).fillMaxHeight(),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(16.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterVertically)
                ) {
                    Text("云台微调", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "小步调整镜头方向，默认每次 3 度。",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    val gimbalStatus = uiState.gimbalStatus
                    if (gimbalStatus != null) {
                        StatRow("在线", if (gimbalStatus.online) "是" else "否")
                        StatRow("安全", if (gimbalStatus.safe) "是" else "否")
                        StatRow("归位", "X:${yesNo(gimbalStatus.xhome)} Y:${yesNo(gimbalStatus.yhome)}")
                        StatRow(
                            "角度",
                            "X:${gimbalStatus.angleX?.toInt() ?: "-"} Y:${gimbalStatus.angleY?.toInt() ?: "-"}"
                        )
                        gimbalStatus.lastError?.let { StatRow("错误", it) }
                    }
                    OutlinedButton(onClick = { viewModel.moveGimbal(0, -3) }) { Text("上") }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { viewModel.moveGimbal(-3, 0) }) { Text("左") }
                        OutlinedButton(onClick = viewModel::centerGimbal) { Text("归中") }
                        OutlinedButton(onClick = { viewModel.moveGimbal(3, 0) }) { Text("右") }
                    }
                    OutlinedButton(onClick = { viewModel.moveGimbal(0, 3) }) { Text("下") }
                    TextButton(onClick = viewModel::stopGimbal) { Text("停止") }
                }
            }
        }
    }
}

// ==================== Page 2: Stats ====================

@Composable
private fun StatsPage(
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState
) {
    val lastAlert = uiState.lastAlert

    Row(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Column(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text(
                        "当前状态",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = phaseLabel(uiState.sessionPhase, uiState.timerState),
                        style = MaterialTheme.typography.headlineSmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    StatRow("会话 ID", uiState.sessionId?.takeLast(12) ?: "尚未开始")
                    StatRow("主机 active", yesNo(uiState.boardSessionActive))
                    StatRow("主机 paused", yesNo(uiState.boardSessionPaused))
                    StatRow("预检完成", yesNo(uiState.calibrationCompleted))
                    StatRow("行为状态", focusStateLabel(uiState.focusState))
                    uiState.focusReason?.let { StatRow("状态原因", compactJsonText(it)) }
                }
            }

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("会话统计", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    StatRow("走神次数", "${uiState.distractionCount}")
                    StatRow("提醒次数", "${uiState.reminderCount}")
                    StatRow("手机干扰", "${uiState.phoneDistractionCount}")
                    lastAlert?.phoneDistractionLevel?.let { StatRow("手机干扰等级", "L$it") }
                }
            }

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("主机提醒快照", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    if (uiState.reminderSnapshot == null) {
                        Text(
                            "暂无 reminder 快照",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        Text(
                            compactJsonText(uiState.reminderSnapshot),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }

        Column(
            modifier = Modifier
                .weight(1f)
                .fillMaxHeight()
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("行为上下文", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    uiState.precheckGuidance?.let { StatRow("预检提示", compactJsonText(it)) }
                    uiState.behaviorContext?.let { StatRow("上下文", compactJsonText(it)) }
                    if (uiState.precheckGuidance == null &&
                        uiState.behaviorContext == null
                    ) {
                        Text(
                            "等待主机行为快照",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("最近提醒", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    if (lastAlert == null) {
                        Text(
                            "暂无提醒",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    } else {
                        StatRow("等级", "L${lastAlert.level}")
                        StatRow("消息", lastAlert.message.ifBlank { "主机未提供消息" })
                        StatRow("手机动作", lastAlert.phoneActions.joinToString("、") { channelLabel(it) })
                        lastAlert.riskScore?.let { StatRow("风险分", "%.2f".format(it)) }
                        lastAlert.reason?.let { StatRow("原因", compactJsonText(it)) }
                        lastAlert.focusState?.let { StatRow("提醒状态", focusStateLabel(it)) }
                        lastAlert.soundVolume?.let { StatRow("声音音量", "${(it * 100).toInt()}%") }
                    }
                }
            }
        }
    }
}

// ==================== Page 3: Settings ====================

@Composable
private fun SettingsPage(
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState,
    viewModel: ControlPanelViewModel
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        if (!uiState.isBoardConnected) {
            Text(
                "连接已绑定的 EdgeFocus 后才能调整设备设置",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error
            )
        } else if (!uiState.arePreferencesLoaded) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("尚未读取设备设置", style = MaterialTheme.typography.bodySmall)
                OutlinedButton(onClick = viewModel::refreshBoardPreferences) {
                    Text("重新读取")
                }
            }
        }

        // Row 1: System mode
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // System mode
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("系统模式", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilledIconButton(
                            onClick = { viewModel.setSystemMode(0) },
                            modifier = Modifier.weight(1f),
                            enabled = uiState.isBoardConnected && uiState.arePreferencesLoaded,
                            colors = IconButtonDefaults.filledIconButtonColors(
                                containerColor = if (uiState.systemMode == com.edgefocus.app.presentation.viewmodel.SystemMode.NORMAL)
                                    MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceVariant
                            )
                        ) {
                            Text("正常", style = MaterialTheme.typography.labelMedium,
                                color = if (uiState.systemMode == com.edgefocus.app.presentation.viewmodel.SystemMode.NORMAL)
                                    Color.White else MaterialTheme.colorScheme.onSurface)
                        }
                        FilledIconButton(
                            onClick = { viewModel.setSystemMode(1) },
                            modifier = Modifier.weight(1f),
                            enabled = uiState.isBoardConnected && uiState.arePreferencesLoaded,
                            colors = IconButtonDefaults.filledIconButtonColors(
                                containerColor = if (uiState.systemMode == com.edgefocus.app.presentation.viewmodel.SystemMode.SILENT)
                                    MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceVariant
                            )
                        ) {
                            Text("静音", style = MaterialTheme.typography.labelMedium,
                                color = if (uiState.systemMode == com.edgefocus.app.presentation.viewmodel.SystemMode.SILENT)
                                    Color.White else MaterialTheme.colorScheme.onSurface)
                        }
                    }
                }
            }
        }

        // Row 2: Reminder sensitivity + Away timeout
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Reminder sensitivity
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surface
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("提醒灵敏度", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    HardwareSlider(
                        "灵敏度",
                        uiState.reminderSensitivity,
                        uiState.isBoardConnected && uiState.arePreferencesLoaded,
                        onValueChangeFinished = viewModel::commitReminderSensitivity
                    ) {
                        viewModel.setReminderSensitivity(it, persist = false)
                    }
                }
            }

            // Away timeout
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surface
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("离座超时", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("${uiState.awayTimeout} 分钟", style = MaterialTheme.typography.bodyMedium)
                    Slider(
                        value = uiState.awayTimeout.toFloat(),
                        onValueChange = { viewModel.setAwayTimeout(it.toInt(), persist = false) },
                        onValueChangeFinished = viewModel::commitAwayTimeout,
                        valueRange = 5f..60f,
                        enabled = uiState.isBoardConnected && uiState.arePreferencesLoaded
                    )
                }
            }
        }

        // Row 3: Vibration strength
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("振动强度", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    HardwareSlider(
                        "强度",
                        uiState.vibrationStrength,
                        uiState.isBoardConnected && uiState.arePreferencesLoaded,
                        onValueChangeFinished = viewModel::commitVibrationStrength
                    ) {
                        viewModel.setVibrationStrength(it, persist = false)
                    }
                }
            }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("主机默认专注时长", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("${uiState.defaultFocusDurationMin} 分钟", style = MaterialTheme.typography.bodyMedium)
                    Text(
                        "仅作为主机默认值；本次会话时长由计时页选择。",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Slider(
                        value = uiState.defaultFocusDurationMin.toFloat(),
                        onValueChange = { viewModel.setDefaultFocusDuration(it.toInt(), persist = false) },
                        onValueChangeFinished = viewModel::commitDefaultFocusDuration,
                        valueRange = 5f..120f,
                        enabled = uiState.isBoardConnected && uiState.arePreferencesLoaded
                    )
                }
            }

            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("提示音量", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    HardwareSlider(
                        "音量",
                        uiState.soundVolume,
                        uiState.isBoardConnected && uiState.arePreferencesLoaded,
                        onValueChangeFinished = viewModel::commitSoundVolume
                    ) {
                        viewModel.setSoundVolume(it, persist = false)
                    }
                }
            }
        }

        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text("提醒渠道", style = MaterialTheme.typography.titleMedium)
                Spacer(modifier = Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(24.dp)) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("普通模式", style = MaterialTheme.typography.bodyMedium)
                        ChannelToggle("屏幕", "screen", uiState.normalChannels, uiState.isBoardConnected && uiState.arePreferencesLoaded) {
                            viewModel.setNormalChannel("screen", it)
                        }
                        ChannelToggle("手环", "band", uiState.normalChannels, uiState.isBoardConnected && uiState.arePreferencesLoaded) {
                            viewModel.setNormalChannel("band", it)
                        }
                        ChannelToggle("声音", "sound", uiState.normalChannels, uiState.isBoardConnected && uiState.arePreferencesLoaded) {
                            viewModel.setNormalChannel("sound", it)
                        }
                    }
                    Column(modifier = Modifier.weight(1f)) {
                        Text("静音模式", style = MaterialTheme.typography.bodyMedium)
                        ChannelToggle("屏幕", "screen", uiState.silentChannels, uiState.isBoardConnected && uiState.arePreferencesLoaded) {
                            viewModel.setSilentChannel("screen", it)
                        }
                        ChannelToggle("手环", "band", uiState.silentChannels, uiState.isBoardConnected && uiState.arePreferencesLoaded) {
                            viewModel.setSilentChannel("band", it)
                        }
                        Text(
                            "静音模式不使用声音提醒",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun ChannelToggle(
    label: String,
    channel: String,
    selectedChannels: Set<String>,
    enabled: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, style = MaterialTheme.typography.bodySmall)
        Checkbox(
            checked = selectedChannels.contains(channel),
            onCheckedChange = onCheckedChange,
            enabled = enabled
        )
    }
}

private fun phaseLabel(phase: BoardSessionPhase, timerState: TimerState): String {
    return when (phase) {
        BoardSessionPhase.IDLE -> when (timerState) {
            TimerState.IDLE -> "等待开始"
            TimerState.PRECHECK -> "预检中"
            TimerState.RUNNING -> "专注进行中"
            TimerState.PAUSED -> "已暂停"
            TimerState.FINISHED -> "已完成"
        }
        BoardSessionPhase.PRECHECK -> "预检中"
        BoardSessionPhase.FOCUSING -> "专注进行中"
        BoardSessionPhase.PAUSED -> "已暂停"
    }
}

private fun focusStateLabel(value: Int): String {
    return when (value) {
        0 -> "专注"
        1 -> "视线偏离"
        2 -> "疲劳/姿态异常"
        3 -> "手机使用"
        4 -> "离座"
        5 -> "视觉低置信"
        6 -> "暂停"
        7 -> "预检/校准"
        8 -> "学习类手机使用"
        9 -> "娱乐类手机使用"
        else -> "未知"
    }
}

private fun channelLabel(channel: String): String {
    return when (channel.lowercase()) {
        "screen" -> "屏幕"
        "band" -> "手环"
        "sound" -> "声音"
        "light" -> "灯光"
        else -> channel
    }
}

private fun compactJsonText(value: String): String {
    return value
        .replace("{", "")
        .replace("}", "")
        .replace("\"", "")
        .replace(",", "，")
        .take(80)
}

private fun yesNo(value: Boolean): String = if (value) "是" else "否"

@Composable
private fun HardwareSlider(
    label: String,
    value: Float,
    enabled: Boolean = true,
    onValueChangeFinished: () -> Unit = {},
    onValueChange: (Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, style = MaterialTheme.typography.bodyMedium)
            Text(
                "${(value * 100).toInt()}%",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.primary
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            onValueChangeFinished = onValueChangeFinished,
            valueRange = 0f..1f,
            enabled = enabled
        )
    }
}

@Composable
private fun StatRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(
            value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
