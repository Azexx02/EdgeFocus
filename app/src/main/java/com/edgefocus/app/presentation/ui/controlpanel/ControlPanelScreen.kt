package com.edgefocus.app.presentation.ui.controlpanel

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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
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
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
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
}

// ==================== Page 1: Timer ====================

@Composable
private fun TimerPage(
    viewModel: ControlPanelViewModel,
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState
) {
    val isRunning = uiState.timerState == TimerState.RUNNING
    val isIdle = uiState.timerState == TimerState.IDLE

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

        // Right: 3 buttons horizontal — Stop | Play | Pause
        Row(
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Stop (small)
            OutlinedIconButton(
                onClick = { viewModel.stopSession() },
                modifier = Modifier.size(56.dp),
                enabled = uiState.timerState != TimerState.IDLE
            ) {
                Icon(
                    Icons.Default.Stop,
                    contentDescription = "Stop",
                    modifier = Modifier.size(28.dp)
                )
            }

            // Play (large, primary)
            FilledIconButton(
                onClick = {
                    if (isRunning) viewModel.pauseSession()
                    else viewModel.startSession()
                },
                modifier = Modifier.size(80.dp),
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

            // Pause (small)
            OutlinedIconButton(
                onClick = { viewModel.pauseSession() },
                modifier = Modifier.size(56.dp),
                enabled = isRunning
            ) {
                Icon(
                    Icons.Default.Pause,
                    contentDescription = "Pause",
                    modifier = Modifier.size(28.dp)
                )
            }
        }
    }
}

// ==================== Page 2: Stats ====================

@Composable
private fun StatsPage(
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState
) {
    Row(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        // Left: Session stats cards
        Column(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Current status card
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
                        text = when (uiState.timerState) {
                            TimerState.IDLE -> "等待开始"
                            TimerState.RUNNING -> "专注进行中"
                            TimerState.PAUSED -> "已暂停"
                            TimerState.FINISHED -> "已完成"
                        },
                        style = MaterialTheme.typography.headlineSmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
            }

            // Time stats card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("时间统计", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    StatRow("计划专注", "${uiState.selectedDuration} 分钟")
                    StatRow("剩余时间", TimeUtils.formatTime(uiState.timeRemaining))
                    StatRow("已进行", TimeUtils.formatTime(
                        uiState.selectedDuration * 60 * 1000L - uiState.timeRemaining
                    ))
                }
            }

            // Sessions card
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("会话统计", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    StatRow("已完成会话", "${uiState.completedSessions}")
                }
            }
        }

        // Right: Distraction stats
        Column(
            modifier = Modifier.weight(1f).fillMaxHeight(),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("走神次数", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "${uiState.distractionCount}",
                        style = MaterialTheme.typography.headlineLarge,
                        color = MaterialTheme.colorScheme.error
                    )
                }
            }

            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("手机干扰次数", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "${uiState.phoneDistractionCount}",
                        style = MaterialTheme.typography.headlineLarge,
                        color = MaterialTheme.colorScheme.onSurface
                    )
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
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // Row 1: System mode + Home mode
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

            // Home mode
            Card(
                modifier = Modifier.weight(1f),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("Home 模式", style = MaterialTheme.typography.titleMedium)
                        Text(
                            if (uiState.homeMode) "开启" else "关闭",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Switch(
                        checked = uiState.homeMode,
                        onCheckedChange = { viewModel.setHomeMode(it) },
                        colors = SwitchDefaults.colors(
                            checkedTrackColor = MaterialTheme.colorScheme.primary
                        )
                    )
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
                    HardwareSlider("灵敏度", uiState.reminderSensitivity) {
                        viewModel.setReminderSensitivity(it)
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
                        onValueChange = { viewModel.setAwayTimeout(it.toInt()) },
                        valueRange = 5f..60f
                    )
                }
            }
        }
    }
}

@Composable
private fun HardwareSlider(label: String, value: Float, onValueChange: (Float) -> Unit) {
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
            valueRange = 0f..1f
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
