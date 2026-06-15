package com.edgefocus.app.presentation.ui.home

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.HomeViewModel

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val connectionState by bleViewModel.connectionState.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "EdgeFocus",
            style = MaterialTheme.typography.headlineLarge,
            color = MaterialTheme.colorScheme.primary
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = when (connectionState) {
                ConnectionState.CONNECTED -> "BLE Connected"
                ConnectionState.SCANNING -> "Scanning..."
                ConnectionState.CONNECTING -> "Connecting..."
                ConnectionState.ERROR -> "BLE Error"
                ConnectionState.DISCONNECTED -> "BLE Disconnected"
            },
            style = MaterialTheme.typography.bodySmall,
            color = if (connectionState == ConnectionState.CONNECTED)
                MaterialTheme.colorScheme.primary
            else
                MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        val sessionTypeText = when (uiState.sessionType) {
            SessionType.WORK -> "Work Session"
            SessionType.SHORT_BREAK -> "Short Break"
            SessionType.LONG_BREAK -> "Long Break"
        }
        Text(
            text = sessionTypeText,
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.secondary
        )

        Spacer(modifier = Modifier.height(32.dp))

        PomodoroTimer(
            timeMillis = uiState.timeRemaining,
            totalTimeMillis = uiState.totalTime
        )

        Spacer(modifier = Modifier.height(32.dp))

        Button(
            onClick = {
                when (uiState.timerState) {
                    TimerState.IDLE, TimerState.PAUSED -> {
                        viewModel.startTimer()
                        if (bleViewModel.isConnected()) {
                            val durationMin = (uiState.totalTime / 60000).toInt()
                            bleViewModel.sendSessionCommand("start", durationMin)
                        }
                    }
                    TimerState.RUNNING -> {
                        viewModel.pauseTimer()
                        if (bleViewModel.isConnected()) {
                            bleViewModel.sendSessionCommand("pause")
                        }
                    }
                    TimerState.BREAK -> {
                        viewModel.pauseTimer()
                        if (bleViewModel.isConnected()) {
                            bleViewModel.sendSessionCommand("pause")
                        }
                    }
                    TimerState.FINISHED -> {
                        viewModel.stopTimer()
                        if (bleViewModel.isConnected()) {
                            bleViewModel.sendSessionCommand("stop")
                        }
                    }
                }
            }
        ) {
            Text(
                text = when (uiState.timerState) {
                    TimerState.IDLE -> "Start"
                    TimerState.RUNNING -> "Pause"
                    TimerState.PAUSED -> "Resume"
                    TimerState.BREAK -> "Pause Break"
                    TimerState.FINISHED -> "Reset"
                }
            )
        }

        Spacer(modifier = Modifier.height(16.dp))

        Button(onClick = {
            viewModel.stopTimer()
            if (bleViewModel.isConnected()) {
                bleViewModel.sendSessionCommand("stop")
            }
        }) {
            Text(text = "Stop")
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Completed: ${uiState.completedSessions} sessions",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}