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
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.ControlPanelViewModel
import com.edgefocus.app.presentation.viewmodel.NfcStatus
import com.edgefocus.app.util.TimeUtils

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ControlPanelScreen(
    viewModel: ControlPanelViewModel = hiltViewModel()
) {
    val uiState = viewModel.uiState.collectAsState().value
    val pagerState = rememberPagerState(pageCount = { 3 })

    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = { Text("NFC Control Panel") }
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
                2 -> StatsPage(uiState = uiState, onEnableNfcRotation = {
                    viewModel.enableNfcScreenRotation()
                })
            }
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp),
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
                TimerState.IDLE -> "Ready"
                TimerState.RUNNING -> "Focusing"
                TimerState.PAUSED -> "Paused"
                TimerState.FINISHED -> "Complete"
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
                        Text("Start Session")
                    }
                }
                TimerState.RUNNING -> {
                    Button(onClick = { viewModel.pauseSession() }) {
                        Text("Pause")
                    }
                    OutlinedButton(onClick = { viewModel.stopSession() }) {
                        Text("Stop")
                    }
                }
                TimerState.PAUSED -> {
                    Button(onClick = { viewModel.startSession() }) {
                        Text("Resume")
                    }
                    OutlinedButton(onClick = { viewModel.stopSession() }) {
                        Text("Stop")
                    }
                }
                else -> {}
            }
        }
    }
}

@Composable
private fun StatsPage(
    uiState: com.edgefocus.app.presentation.viewmodel.ControlPanelUiState,
    onEnableNfcRotation: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "Connection Status",
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary
        )

        Spacer(modifier = Modifier.height(12.dp))

        Row(
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                modifier = Modifier
                    .size(12.dp)
                    .clip(CircleShape)
                    .background(
                        when (uiState.nfcStatus) {
                            NfcStatus.AVAILABLE -> MaterialTheme.colorScheme.primary
                            NfcStatus.UNAVAILABLE -> MaterialTheme.colorScheme.error
                            NfcStatus.CHECKING -> MaterialTheme.colorScheme.surfaceVariant
                        }
                    )
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
                text = when (uiState.nfcStatus) {
                    NfcStatus.AVAILABLE -> "NFC Connected"
                    NfcStatus.UNAVAILABLE -> "NFC Unavailable"
                    NfcStatus.CHECKING -> "Checking NFC..."
                },
                style = MaterialTheme.typography.bodyLarge
            )
        }

        Spacer(modifier = Modifier.height(24.dp))

        Text(
            text = "Session Statistics",
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.primary
        )

        Spacer(modifier = Modifier.height(12.dp))

        StatRow(label = "Completed Sessions", value = "${uiState.completedSessions}")
        StatRow(label = "Total Focus Time", value = "${uiState.totalFocusMinutes} min")
        StatRow(label = "System Mode", value = uiState.systemMode.name.lowercase().replaceFirstChar { it.uppercase() })

        Spacer(modifier = Modifier.height(24.dp))

        OutlinedButton(onClick = onEnableNfcRotation) {
            Text("Enable NFC Screen Rotation")
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
