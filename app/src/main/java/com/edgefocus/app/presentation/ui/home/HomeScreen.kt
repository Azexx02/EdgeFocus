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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.HomeViewModel
import com.edgefocus.app.presentation.viewmodel.SettingsViewModel

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel(),
    settingsViewModel: SettingsViewModel = hiltViewModel(),
    onNavigateToControlPanel: () -> Unit = {}
) {
    val uiState by viewModel.uiState.collectAsState()
    val settings by settingsViewModel.settings.collectAsState()
    val effectiveConnectionState = bleViewModel.getEffectiveConnectionState()

    val useBoard = settings.useEdgeFocusBoard
    val isConnected = bleViewModel.isConnected()

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

            // 连接状态
            Text(
                text = when (effectiveConnectionState) {
                    ConnectionState.CONNECTED -> "设备已连接"
                    ConnectionState.SCANNING -> "扫描中..."
                    ConnectionState.CONNECTING -> "连接中..."
                    ConnectionState.ERROR -> "连接错误"
                    ConnectionState.DISCONNECTED -> "设备未连接"
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (isConnected) MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(4.dp))

            // 会话模式
            Text(
                text = when {
                    uiState.sessionMode == SessionMode.EDGEFOCUS -> "EdgeFocus 模式"
                    uiState.sessionMode == SessionMode.LOCAL && uiState.timerState != TimerState.IDLE -> "本地模式"
                    useBoard && !isConnected -> "请先连接设备"
                    else -> "本地模式"
                },
                style = MaterialTheme.typography.bodySmall,
                color = when {
                    uiState.sessionMode == SessionMode.EDGEFOCUS -> MaterialTheme.colorScheme.primary
                    useBoard && !isConnected && uiState.timerState == TimerState.IDLE -> MaterialTheme.colorScheme.error
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                }
            )

            Spacer(modifier = Modifier.height(8.dp))

            // 会话类型
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
                            viewModel.startTimer(useBoard)
                        }
                        TimerState.PAUSED -> {
                            viewModel.startTimer(useBoard)
                        }
                        TimerState.RUNNING -> {
                            viewModel.pauseTimer()
                        }
                        TimerState.BREAK -> {
                            viewModel.pauseTimer()
                        }
                        TimerState.FINISHED -> {
                            viewModel.stopTimer()
                        }
                    }
                },
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = when (uiState.timerState) {
                        TimerState.IDLE -> if (useBoard) "开始 EdgeFocus 会话" else "开始专注"
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
                    onClick = { viewModel.stopTimer() },
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

            // 提示信息
            if (useBoard && !isConnected) {
                Text(
                    text = "设备未连接，将尝试自动连接",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}
