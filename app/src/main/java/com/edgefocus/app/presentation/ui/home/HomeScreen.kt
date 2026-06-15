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
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.HomeViewModel

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()

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
                    TimerState.IDLE, TimerState.PAUSED -> viewModel.startTimer()
                    TimerState.RUNNING -> viewModel.pauseTimer()
                    TimerState.BREAK -> viewModel.pauseTimer()
                    TimerState.FINISHED -> viewModel.stopTimer()
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

        Button(onClick = { viewModel.stopTimer() }) {
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