package com.edgefocus.app.presentation.ui.settings

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.presentation.viewmodel.DistractionLevel
import com.edgefocus.app.presentation.viewmodel.TestViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DistractionAppsScreen(
    onBack: () -> Unit,
    viewModel: TestViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("干扰应用设置") },
            navigationIcon = {
                IconButton(onClick = onBack) {
                    Icon(Icons.Default.ArrowBack, contentDescription = "返回")
                }
            }
        )

        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp)
        ) {
            // 说明文字
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text(
                        text = "干扰等级说明",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "• 强干扰源：短视频、游戏类（高频提醒）",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Text(
                        text = "• 弱干扰源：社交媒体类（定期提醒）",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Text(
                        text = "• 学习软件：不提醒",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                    Text(
                        text = "• 未选择：退出到后台时统一处理",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onPrimaryContainer
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            // 统计
            val highCount = uiState.distractionLevels.values.count { it == DistractionLevel.HIGH.value }
            val lowCount = uiState.distractionLevels.values.count { it == DistractionLevel.LOW.value }
            val learnCount = uiState.distractionLevels.values.count { it == DistractionLevel.LEARNING.value }

            Text(
                text = "强干扰: $highCount | 弱干扰: $lowCount | 学习: $learnCount",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Spacer(modifier = Modifier.height(16.dp))

            LazyColumn(
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(uiState.installedApps) { app ->
                    AppItem(
                        appName = app.appName,
                        packageName = app.packageName,
                        currentLevel = app.distractionLevel,
                        onLevelChanged = { level ->
                            viewModel.setDistractionLevel(app.packageName, level)
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun AppItem(
    appName: String,
    packageName: String,
    currentLevel: DistractionLevel,
    onLevelChanged: (DistractionLevel) -> Unit
) {
    var showMenu by remember { mutableStateOf(false) }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { showMenu = true },
        colors = CardDefaults.cardColors(
            containerColor = when (currentLevel) {
                DistractionLevel.HIGH -> MaterialTheme.colorScheme.errorContainer
                DistractionLevel.LOW -> MaterialTheme.colorScheme.tertiaryContainer
                DistractionLevel.LEARNING -> MaterialTheme.colorScheme.primaryContainer
                DistractionLevel.BACKGROUND -> MaterialTheme.colorScheme.surfaceVariant
            }
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = appName,
                    style = MaterialTheme.typography.bodyLarge
                )
                Text(
                    text = packageName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            Column(horizontalAlignment = Alignment.End) {
                Text(
                    text = currentLevel.label,
                    style = MaterialTheme.typography.labelMedium,
                    color = when (currentLevel) {
                        DistractionLevel.HIGH -> MaterialTheme.colorScheme.onErrorContainer
                        DistractionLevel.LOW -> MaterialTheme.colorScheme.onTertiaryContainer
                        DistractionLevel.LEARNING -> MaterialTheme.colorScheme.onPrimaryContainer
                        DistractionLevel.BACKGROUND -> MaterialTheme.colorScheme.onSurfaceVariant
                    }
                )
                Text(
                    text = currentLevel.description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            DropdownMenu(
                expanded = showMenu,
                onDismissRequest = { showMenu = false }
            ) {
                DistractionLevel.entries.forEach { level ->
                    DropdownMenuItem(
                        text = {
                            Column {
                                Text(text = level.label)
                                Text(
                                    text = level.description,
                                    style = MaterialTheme.typography.bodySmall
                                )
                            }
                        },
                        onClick = {
                            onLevelChanged(level)
                            showMenu = false
                        }
                    )
                }
            }
        }
    }
}
