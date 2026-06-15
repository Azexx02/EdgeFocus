package com.edgefocus.app.presentation.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.HomeViewModel
import com.edgefocus.app.presentation.viewmodel.SettingsViewModel
import kotlinx.coroutines.launch

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel(),
    settingsViewModel: SettingsViewModel = hiltViewModel(),
    onNavigateToControlPanel: () -> Unit = {}
) {
    val uiState by viewModel.uiState.collectAsState()
    val connectionState by bleViewModel.connectionState.collectAsState()
    val settings by settingsViewModel.settings.collectAsState()
    val scope = rememberCoroutineScope()

    // 获取有效的连接状态（考虑模拟模式）
    val effectiveConnectionState = bleViewModel.getEffectiveConnectionState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.SpaceBetween
    ) {
        // 顶部区域
        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = "EdgeFocus",
                style = MaterialTheme.typography.headlineLarge,
                color = MaterialTheme.colorScheme.primary
            )

            Spacer(modifier = Modifier.height(8.dp))

            Text(
                text = when (effectiveConnectionState) {
                    ConnectionState.CONNECTED -> if (bleViewModel.isSimulatingBle()) "模拟连接" else "蓝牙已连接"
                    ConnectionState.SCANNING -> "扫描中..."
                    ConnectionState.CONNECTING -> "连接中..."
                    ConnectionState.ERROR -> "蓝牙错误"
                    ConnectionState.DISCONNECTED -> "蓝牙未连接"
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (effectiveConnectionState == ConnectionState.CONNECTED)
                    MaterialTheme.colorScheme.primary
                else
                    MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(8.dp))

            val sessionTypeText = when (uiState.sessionType) {
                SessionType.WORK -> "工作会话"
                SessionType.SHORT_BREAK -> "短休息"
                SessionType.LONG_BREAK -> "长休息"
            }
            Text(
                text = sessionTypeText,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.secondary
            )
        }

        // 中间区域 - 计时器
        Column(
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            PomodoroTimer(
                timeMillis = uiState.timeRemaining,
                totalTimeMillis = uiState.totalTime
            )

            Spacer(modifier = Modifier.height(16.dp))

            Text(
                text = "已完成: ${uiState.completedSessions} 个会话",
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        // 底部区域 - 按钮
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // 主要按钮
            Button(
                onClick = {
                    when (uiState.timerState) {
                        TimerState.IDLE -> {
                            // 检查是否需要 BLE 连接
                            val useBoard = settings?.useEdgeFocusBoard ?: true
                            if (useBoard && !bleViewModel.isConnected()) {
                                // 需要 BLE 但未连接，不启动
                                return@Button
                            }
                            scope.launch {
                                val durationMin = (uiState.totalTime / 60000).toInt()
                                if (!bleViewModel.isConnected() ||
                                    bleViewModel.sendSessionCommand("start", durationMin)
                                ) {
                                    viewModel.startTimer()
                                }
                            }
                        }
                        TimerState.PAUSED -> {
                            scope.launch {
                                if (!bleViewModel.isConnected() ||
                                    bleViewModel.sendSessionCommand("resume")
                                ) {
                                    viewModel.startTimer()
                                }
                            }
                        }
                        TimerState.RUNNING -> {
                            viewModel.pauseTimer()
                            if (bleViewModel.isConnected()) {
                                scope.launch { bleViewModel.sendSessionCommand("pause") }
                            }
                        }
                        TimerState.BREAK -> {
                            viewModel.pauseTimer()
                            if (bleViewModel.isConnected()) {
                                scope.launch { bleViewModel.sendSessionCommand("pause") }
                            }
                        }
                        TimerState.FINISHED -> {
                            viewModel.stopTimer()
                            if (bleViewModel.isConnected()) {
                                scope.launch { bleViewModel.sendSessionCommand("stop") }
                            }
                        }
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = !(settings?.useEdgeFocusBoard == true &&
                        !bleViewModel.isConnected() &&
                        uiState.timerState == TimerState.IDLE)
            ) {
                Text(
                    text = when (uiState.timerState) {
                        TimerState.IDLE -> "开始专注"
                        TimerState.RUNNING -> "暂停"
                        TimerState.PAUSED -> "继续"
                        TimerState.BREAK -> "暂停休息"
                        TimerState.FINISHED -> "重置"
                    }
                )
            }

            // 次要按钮行
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                // 停止按钮
                OutlinedButton(
                    onClick = {
                        viewModel.stopTimer()
                        if (bleViewModel.isConnected()) {
                            scope.launch { bleViewModel.sendSessionCommand("stop") }
                        }
                    },
                    modifier = Modifier.weight(1f),
                    enabled = uiState.timerState != TimerState.IDLE
                ) {
                    Text("停止")
                }

                // 控制面板按钮
                OutlinedButton(
                    onClick = onNavigateToControlPanel,
                    modifier = Modifier.weight(1f)
                ) {
                    Text("控制面板")
                }
            }

            // 如果使用 EdgeFocus Board 但未连接，显示提示
            if (settings?.useEdgeFocusBoard == true && !bleViewModel.isConnected()) {
                Text(
                    text = "请先连接 EdgeFocus 设备",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
        }
    }
}
