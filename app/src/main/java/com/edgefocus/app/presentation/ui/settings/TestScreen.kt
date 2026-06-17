package com.edgefocus.app.presentation.ui.settings

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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.AppEvent
import com.edgefocus.app.presentation.viewmodel.TestViewModel
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TestScreen(
    onBack: () -> Unit,
    viewModel: TestViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val listState = rememberLazyListState()

    LaunchedEffect(uiState.appLogs.size) {
        if (uiState.appLogs.isNotEmpty()) {
            listState.animateScrollToItem(uiState.appLogs.size - 1)
        }
    }

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("通信日志") },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回")
                }
            },
            actions = {
                IconButton(onClick = { viewModel.clearBleLogs() }) {
                    Icon(
                        Icons.Default.Delete,
                        contentDescription = "清除日志",
                        tint = MaterialTheme.colorScheme.error
                    )
                }
            }
        )

        // BLE 模拟开关
        Card(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp),
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
                    Text(
                        text = "模拟 BLE 已连接",
                        style = MaterialTheme.typography.bodyLarge
                    )
                    Text(
                        text = if (uiState.simulateBleConnected) "开启 - 模拟蓝牙已连接" else "关闭 - 使用真实蓝牙",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = uiState.simulateBleConnected,
                    onCheckedChange = { viewModel.toggleSimulateBle() }
                )
            }
        }

        // 模拟设备回复开关
        if (uiState.simulateBleConnected) {
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.surfaceVariant
                )
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    // 设备回复开关
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text("模拟设备回复", style = MaterialTheme.typography.bodyLarge)
                            Text(
                                if (uiState.simulateDeviceResponse) "开启 - 模拟板端响应信号" else "关闭 - 无设备响应",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = uiState.simulateDeviceResponse,
                            onCheckedChange = { viewModel.toggleSimulateDeviceResponse() }
                        )
                    }

                    if (uiState.simulateDeviceResponse) {
                        Spacer(modifier = Modifier.height(12.dp))

                        // 模拟时长
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("模拟时长", style = MaterialTheme.typography.bodyMedium)
                            Row(
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                IconButton(
                                    onClick = { viewModel.setSimDuration(uiState.simDurationMinutes - 5) },
                                    modifier = Modifier.size(32.dp)
                                ) { Text("-", style = MaterialTheme.typography.titleMedium) }
                                Text("${uiState.simDurationMinutes} min", style = MaterialTheme.typography.bodyMedium)
                                IconButton(
                                    onClick = { viewModel.setSimDuration(uiState.simDurationMinutes + 5) },
                                    modifier = Modifier.size(32.dp)
                                ) { Text("+", style = MaterialTheme.typography.titleMedium) }
                            }
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        // 自动倒计时开关
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("自动倒计时", style = MaterialTheme.typography.bodyMedium)
                                Text(
                                    "模拟板端自行倒计时",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Switch(
                                checked = uiState.simulateAutoCountdown,
                                onCheckedChange = { viewModel.toggleSimulateAutoCountdown() }
                            )
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        // 板端已在会话中
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("板端已在会话中", style = MaterialTheme.typography.bodyMedium)
                                Text(
                                    "模拟连接时自动同步板端时间",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            Switch(
                                checked = uiState.simulateBoardInSession,
                                onCheckedChange = { viewModel.toggleSimulateBoardInSession() }
                            )
                        }
                    }
                }
            }
        }

        if (!uiState.simulateBleConnected) {
            // 未开启模拟时的提示
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(32.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = "开启模拟 BLE 后可查看通信日志",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        } else {
            // 日志计数
            Text(
                text = "共 ${uiState.appLogs.size} 条记录",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp)
            )

            // 应用事件日志
            if (uiState.appLogs.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(32.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        text = "启动专注会话后开始记录",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            } else {
                LazyColumn(
                    state = listState,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 16.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    items(uiState.appLogs) { event ->
                        AppEventItem(event = event)
                    }
                }
            }
        }
    }
}

@Composable
private fun AppEventItem(event: AppEvent) {
    val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
    val timeStr = timeFormat.format(Date(event.timestamp))

    val typeColor = when {
        event.type.startsWith("SESSION") -> MaterialTheme.colorScheme.primary
        event.type.startsWith("PHONE") -> MaterialTheme.colorScheme.secondary
        event.type.startsWith("BLE") -> MaterialTheme.colorScheme.tertiary
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = typeColor.copy(alpha = 0.08f)
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            // 类型标签
            Box(
                modifier = Modifier
                    .background(typeColor.copy(alpha = 0.15f), RoundedCornerShape(6.dp))
                    .padding(horizontal = 8.dp, vertical = 4.dp)
            ) {
                Text(
                    text = event.type,
                    style = MaterialTheme.typography.labelSmall,
                    color = typeColor
                )
            }

            // 内容
            Text(
                text = event.message,
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.weight(1f)
            )

            // 时间
            Text(
                text = timeStr,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun LogItem(log: com.edgefocus.app.presentation.viewmodel.BleLog) {
    val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault())
    val timeStr = timeFormat.format(Date(log.timestamp))

    val isSend = log.direction == "SEND"
    val dotColor = if (isSend) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.secondary
    val bgColor = if (isSend) MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
                  else MaterialTheme.colorScheme.secondaryContainer.copy(alpha = 0.3f)

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = bgColor)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(8.dp),
            verticalAlignment = Alignment.Top,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Box(
                modifier = Modifier
                    .padding(top = 4.dp)
                    .size(6.dp)
                    .clip(CircleShape)
                    .background(dotColor)
            )

            Column(modifier = Modifier.weight(1f)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = if (isSend) "SEND" else "RECV",
                        style = MaterialTheme.typography.labelSmall,
                        color = dotColor
                    )
                    Text(
                        text = timeStr,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = log.content,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 4
                )
            }
        }
    }
}
