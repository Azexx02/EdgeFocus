package com.edgefocus.app.presentation.ui.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.presentation.viewmodel.SettingsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(
    viewModel: SettingsViewModel = hiltViewModel()
) {
    val settings by viewModel.settings.collectAsState()

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("设置") }
        )

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp)
        ) {
            Text(
                text = "番茄钟设置",
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary
            )

            Spacer(modifier = Modifier.height(16.dp))

            SettingSlider(
                label = "专注时长",
                value = settings.pomodoroDuration.toFloat(),
                valueRange = 1f..60f,
                onValueChange = { viewModel.updatePomodoroDuration(it.toInt()) },
                valueText = "${settings.pomodoroDuration} 分钟"
            )

            SettingSlider(
                label = "短休息",
                value = settings.shortBreakDuration.toFloat(),
                valueRange = 1f..30f,
                onValueChange = { viewModel.updateShortBreakDuration(it.toInt()) },
                valueText = "${settings.shortBreakDuration} 分钟"
            )

            SettingSlider(
                label = "长休息",
                value = settings.longBreakDuration.toFloat(),
                valueRange = 1f..60f,
                onValueChange = { viewModel.updateLongBreakDuration(it.toInt()) },
                valueText = "${settings.longBreakDuration} 分钟"
            )

            Spacer(modifier = Modifier.height(24.dp))

            Text(
                text = "设备集成",
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary
            )

            Spacer(modifier = Modifier.height(16.dp))

            SettingSwitch(
                label = "使用 EdgeFocus 开发板",
                checked = settings.useEdgeFocusBoard,
                onCheckedChange = { viewModel.updateUseEdgeFocusBoard(it) }
            )

            SettingSwitch(
                label = "NFC 自动旋转",
                checked = settings.autoRotateNFC,
                onCheckedChange = { viewModel.updateAutoRotateNFC(it) }
            )

            SettingSwitch(
                label = "退出时通知",
                checked = settings.notificationOnExit,
                onCheckedChange = { viewModel.updateNotificationOnExit(it) }
            )
        }
    }
}

@Composable
private fun SettingSlider(
    label: String,
    value: Float,
    valueRange: ClosedFloatingPointRange<Float>,
    onValueChange: (Float) -> Unit,
    valueText: String
) {
    Column(modifier = Modifier.padding(vertical = 8.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(text = label, style = MaterialTheme.typography.bodyLarge)
            Text(text = valueText, style = MaterialTheme.typography.bodyMedium)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = valueRange,
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun SettingSwitch(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 12.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyLarge)
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
