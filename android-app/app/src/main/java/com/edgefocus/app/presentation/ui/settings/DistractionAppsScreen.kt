package com.edgefocus.app.presentation.ui.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.presentation.viewmodel.DistractionLevel
import com.edgefocus.app.presentation.viewmodel.DistractionAppsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DistractionAppsScreen(
    onBack: () -> Unit,
    viewModel: DistractionAppsViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()

    Column(modifier = Modifier.fillMaxSize()) {
        // Header
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(
                onClick = onBack,
                modifier = Modifier.background(MaterialTheme.colorScheme.surface, CircleShape)
            ) {
                Icon(
                    Icons.Default.ArrowBack,
                    contentDescription = "返回",
                    tint = MaterialTheme.colorScheme.primary
                )
            }
            Text(
                "应用干扰设置",
                style = MaterialTheme.typography.headlineMedium,
                color = MaterialTheme.colorScheme.primary
            )
            Spacer(modifier = Modifier.size(48.dp))
        }

        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Description card
            item {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.primaryContainer,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text(
                            "干扰等级说明",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onPrimaryContainer,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                        Text(
                            "· 强干扰：短视频、游戏（高频提醒）",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Text(
                            "· 弱干扰：社交媒体（周期提醒）",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Text(
                            "· 学习类：不提醒",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Text(
                            "· 未选择：按后台/未分类处理",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }
                }
            }

            // Stats
            item {
                val highCount = uiState.distractionLevels.values.count { it == DistractionLevel.HIGH.value }
                val lowCount = uiState.distractionLevels.values.count { it == DistractionLevel.LOW.value }
                val learnCount = uiState.distractionLevels.values.count { it == DistractionLevel.LEARNING.value }

                Text(
                    "强干扰：$highCount | 弱干扰：$lowCount | 学习类：$learnCount",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
                Text(
                    "已安装应用",
                    style = MaterialTheme.typography.headlineMedium,
                    modifier = Modifier.padding(bottom = 8.dp)
                )
            }

            // App list
            items(uiState.installedApps) { app ->
                AppListItem(
                    appName = app.appName,
                    packageName = app.packageName,
                    currentLevel = app.distractionLevel,
                    onLevelChanged = { level ->
                        viewModel.setDistractionLevel(app.packageName, level)
                    }
                )
            }

            item { Spacer(modifier = Modifier.height(24.dp)) }
        }
    }
}

@Composable
private fun AppListItem(
    appName: String,
    packageName: String,
    currentLevel: DistractionLevel,
    onLevelChanged: (DistractionLevel) -> Unit
) {
    var showMenu by remember { mutableStateOf(false) }

    val bgColor = when (currentLevel) {
        DistractionLevel.HIGH -> MaterialTheme.colorScheme.errorContainer
        DistractionLevel.LOW -> MaterialTheme.colorScheme.surfaceVariant
        DistractionLevel.LEARNING -> MaterialTheme.colorScheme.surfaceVariant
        DistractionLevel.BACKGROUND -> MaterialTheme.colorScheme.surface
    }
    val levelColor = when (currentLevel) {
        DistractionLevel.HIGH -> MaterialTheme.colorScheme.error
        DistractionLevel.LOW -> MaterialTheme.colorScheme.tertiary
        DistractionLevel.LEARNING -> MaterialTheme.colorScheme.secondary
        DistractionLevel.BACKGROUND -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Surface(
        shape = RoundedCornerShape(12.dp),
        color = bgColor,
        modifier = Modifier
            .fillMaxWidth()
            .clickable { showMenu = true }
    ) {
        Row(
            modifier = Modifier
                .padding(16.dp)
                .fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column {
                    Text(appName, style = MaterialTheme.typography.bodyLarge)
                    Text(
                        packageName,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    currentLevel.label,
                    style = MaterialTheme.typography.labelMedium,
                    color = levelColor
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
                                Text(level.label)
                                Text(
                                    level.description,
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
