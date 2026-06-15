package com.edgefocus.app.presentation.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
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

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("测试工具") },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回")
                }
            }
        )

        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // 模拟蓝牙连接
            item {
                TestCard(title = "蓝牙模拟") {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column {
                            Text(
                                text = "模拟蓝牙已连接",
                                style = MaterialTheme.typography.bodyLarge
                            )
                            Text(
                                text = if (uiState.simulateBleConnected) "开启 - 应用将假定蓝牙已连接" else "关闭 - 使用真实蓝牙状态",
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
            }

            // BLE 状态
            item {
                TestCard(title = "BLE 状态") {
                    Column {
                        StatusRow("连接状态", "见首页显示")
                        StatusRow("模拟模式", if (uiState.simulateBleConnected) "开启" else "关闭")
                    }
                }
            }

            // 测试命令
            item {
                TestCard(title = "测试命令") {
                    Column(
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                onClick = {
                                    viewModel.addBleLog("SEND", """{"cmd":"get_device_status"}""")
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("获取设备状态")
                            }

                            Button(
                                onClick = {
                                    viewModel.addBleLog("SEND", """{"cmd":"get_current_state"}""")
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("获取当前状态")
                            }
                        }

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Button(
                                onClick = {
                                    viewModel.addBleLog("SEND", """{"cmd":"session_control","data":{"action":"start"}}""")
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("开始会话")
                            }

                            Button(
                                onClick = {
                                    viewModel.addBleLog("SEND", """{"cmd":"session_control","data":{"action":"stop"}}""")
                                },
                                modifier = Modifier.weight(1f)
                            ) {
                                Text("停止会话")
                            }
                        }

                        Button(
                            onClick = {
                                viewModel.addBleLog("SEND", """{"cmd":"phone_sync","data":{"is_in_use":true,"app_category":"entertainment","duration_sec":60}}""")
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("上报手机使用")
                        }
                    }
                }
            }

            // 日志区域
            item {
                TestCard(title = "BLE 通信日志") {
                    Column {
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(
                                text = "最近 ${uiState.bleLogs.size} 条记录",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )

                            IconButton(
                                onClick = { viewModel.clearBleLogs() }
                            ) {
                                Icon(
                                    Icons.Default.Delete,
                                    contentDescription = "清除日志",
                                    tint = MaterialTheme.colorScheme.error
                                )
                            }
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        if (uiState.bleLogs.isEmpty()) {
                            Text(
                                text = "暂无日志",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            // 日志列表
            items(uiState.bleLogs.take(20)) { log ->
                LogItem(log = log)
            }
        }
    }
}

@Composable
private fun TestCard(
    title: String,
    content: @Composable () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary
            )

            Spacer(modifier = Modifier.height(12.dp))

            content()
        }
    }
}

@Composable
private fun StatusRow(
    label: String,
    value: String
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium
        )
    }
}

@Composable
private fun LogItem(log: com.edgefocus.app.presentation.viewmodel.BleLog) {
    val timeFormat = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
    val timeStr = timeFormat.format(Date(log.timestamp))

    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (log.direction == "SEND") {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.secondaryContainer
            }
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.Top
        ) {
            // 方向指示
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(
                        if (log.direction == "SEND") {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.secondary
                        }
                    )
            )

            Spacer(modifier = Modifier.width(8.dp))

            Column(modifier = Modifier.weight(1f)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = if (log.direction == "SEND") "发送" else "接收",
                        style = MaterialTheme.typography.labelSmall,
                        color = if (log.direction == "SEND") {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.secondary
                        }
                    )
                    Text(
                        text = timeStr,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }

                Spacer(modifier = Modifier.height(4.dp))

                Text(
                    text = log.content,
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
    }
}
