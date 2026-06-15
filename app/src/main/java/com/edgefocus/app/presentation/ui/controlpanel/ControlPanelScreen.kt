package com.edgefocus.app.presentation.ui.controlpanel

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.ControlPanelViewModel
import com.edgefocus.app.util.TimeUtils

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ControlPanelScreen(
    viewModel: ControlPanelViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel(),
    onBack: () -> Unit = {}
) {
    val uiState = viewModel.uiState.collectAsState().value
    val pagerState = rememberPagerState(pageCount = { 3 })
    val context = LocalContext.current
    val effectiveConnectionState = bleViewModel.getEffectiveConnectionState()

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("控制面板") },
            navigationIcon = {
                IconButton(onClick = {
                    (context as? android.app.Activity)?.finish()
                }) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回")
                }
            },
            modifier = Modifier.height(48.dp)
        )

        HorizontalPager(
            state = pagerState,
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth()
        ) { page ->
            when (page) {
                0 -> TimerPage(viewModel = viewModel, uiState = uiState)
                1 -> ConfigPage(viewModel = viewModel)
                2 -> SessionStatsPage(uiState = uiState)
            }
        }

        // 页面指示器
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 4.dp),
            horizontalArrangement = Arrangement.Center
        ) {
            repeat(3) { index ->
                Box(
                    modifier = Modifier
                        .padding(horizontal = 4.dp)
                        .size(if (pagerState.currentPage == index) 10.dp else 8.dp)
                        .clip(CircleShape)
                        .background(
                            if (pagerState.currentPage == index)
                                MaterialTheme.colorScheme.primary
                            else
                                MaterialTheme.colorScheme.surfaceVariant
                        )
                )
            }
        }

        // BLE连接状态显示
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(
                        when (effectiveConnectionState) {
                            ConnectionState.CONNECTED -> MaterialTheme.colorScheme.primary
                            ConnectionState.ERROR -> MaterialTheme.colorScheme.error
                            else -> MaterialTheme.colorScheme.surfaceVariant
                        }
                    )
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                text = when (effectiveConnectionState) {
                    ConnectionState.CONNECTED -> if (bleViewModel.isSimulatingBle()) "模拟连接" else "设备已连接"
                    ConnectionState.SCANNING -> "扫描中..."
                    ConnectionState.CONNECTING -> "连接中..."
                    ConnectionState.ERROR -> "连接错误"
                    ConnectionState.DISCONNECTED -> "未连接"
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun TimerPage(
    viewModel: ControlPanelViewModel,
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(horizontal = 8.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceEvenly
    ) {
        Text(
            text = when (uiState.timerState) {
                TimerState.IDLE -> "准备就绪"
                TimerState.RUNNING -> "专注中"
                TimerState.PAUSED -> "已暂停"
                TimerState.FINISHED -> "已完成"
                else -> ""
            },
            style = MaterialTheme.typography.titleLarge,
            color = MaterialTheme.colorScheme.primary
        )

        TimerDial(
            selectedMinutes = uiState.selectedDuration,
            onMinutesChanged = { viewModel.updateSelectedDuration(it) }
        )

        Text(
            text = TimeUtils.formatTime(uiState.timeRemaining),
            style = MaterialTheme.typography.headlineMedium,
            color = MaterialTheme.colorScheme.onSurface
        )

        Row(
            horizontalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            when (uiState.timerState) {
                TimerState.IDLE, TimerState.FINISHED -> {
                    Button(onClick = { viewModel.startSession() }) {
                        Text("开始专注")
                    }
                }
                TimerState.RUNNING -> {
                    Button(onClick = { viewModel.pauseSession() }) {
                        Text("暂停")
                    }
                    OutlinedButton(onClick = { viewModel.stopSession() }) {
                        Text("停止")
                    }
                }
                TimerState.PAUSED -> {
                    Button(onClick = { viewModel.startSession() }) {
                        Text("继续")
                    }
                    OutlinedButton(onClick = { viewModel.stopSession() }) {
                        Text("停止")
                    }
                }
                else -> {}
            }
        }
    }
}

@Composable
private fun SessionStatsPage(
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            text = "本次会话统计",
            style = MaterialTheme.typography.titleLarge,
            color = MaterialTheme.colorScheme.primary
        )

        // 状态卡片
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.primaryContainer
            )
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "当前状态",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
                Spacer(modifier = Modifier.height(8.dp))
                Text(
                    text = when (uiState.timerState) {
                        TimerState.IDLE -> "等待开始"
                        TimerState.RUNNING -> "专注进行中"
                        TimerState.PAUSED -> "已暂停"
                        TimerState.FINISHED -> "已完成"
                        else -> "未知"
                    },
                    style = MaterialTheme.typography.headlineSmall,
                    color = MaterialTheme.colorScheme.onPrimaryContainer
                )
            }
        }

        // 时间统计
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "时间统计",
                    style = MaterialTheme.typography.titleMedium
                )
                Spacer(modifier = Modifier.height(12.dp))

                StatRow(
                    label = "计划专注时长",
                    value = "${uiState.selectedDuration} 分钟"
                )
                StatRow(
                    label = "剩余时间",
                    value = TimeUtils.formatTime(uiState.timeRemaining)
                )
                StatRow(
                    label = "已进行时间",
                    value = TimeUtils.formatTime(uiState.selectedDuration * 60 * 1000L - uiState.timeRemaining)
                )
            }
        }

        // 连接信息
        Card(
            modifier = Modifier.fillMaxWidth(),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceVariant
            )
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "设备信息",
                    style = MaterialTheme.typography.titleMedium
                )
                Spacer(modifier = Modifier.height(12.dp))

                StatRow(
                    label = "系统模式",
                    value = when (uiState.systemMode) {
                        com.edgefocus.app.presentation.viewmodel.SystemMode.NORMAL -> "正常"
                        com.edgefocus.app.presentation.viewmodel.SystemMode.SILENT -> "静音"
                        com.edgefocus.app.presentation.viewmodel.SystemMode.HOME -> "家庭"
                    }
                )
                StatRow(
                    label = "离座超时",
                    value = "${uiState.awayTimeout} 分钟"
                )
            }
        }
    }
}

@Composable
private fun StatRow(
    label: String,
    value: String,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
