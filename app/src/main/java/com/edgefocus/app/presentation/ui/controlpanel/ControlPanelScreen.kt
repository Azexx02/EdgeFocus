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
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
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
    val pagerState = rememberPagerState(pageCount = { 2 }) // 减少到2页
    val context = LocalContext.current
    val connectionState by bleViewModel.connectionState.collectAsState()
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
            }
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
            }
        }

        // 页面指示器
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp),
            horizontalArrangement = Arrangement.Center
        ) {
            repeat(2) { index ->
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
                .padding(horizontal = 16.dp, vertical = 8.dp),
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
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
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

        Spacer(modifier = Modifier.height(24.dp))

        TimerDial(
            selectedMinutes = uiState.selectedDuration,
            onMinutesChanged = { viewModel.updateSelectedDuration(it) }
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = TimeUtils.formatTime(uiState.timeRemaining),
            style = MaterialTheme.typography.headlineMedium,
            color = MaterialTheme.colorScheme.onSurface
        )

        Spacer(modifier = Modifier.height(32.dp))

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
