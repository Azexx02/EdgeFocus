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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.edgefocus.app.util.TimeUtils

@Composable
fun HomeScreen() {
    val focusDurationMillis = TimeUtils.minutesToMillis(25)
    var timeRemaining by remember { mutableLongStateOf(focusDurationMillis) }
    var isRunning by remember { mutableStateOf(false) }

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

        Spacer(modifier = Modifier.height(32.dp))

        PomodoroTimer(
            timeMillis = timeRemaining,
            totalTimeMillis = focusDurationMillis
        )

        Spacer(modifier = Modifier.height(32.dp))

        Button(onClick = { isRunning = !isRunning }) {
            Text(text = if (isRunning) "Pause" else "Start")
        }

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Focus Time: 25 min",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}
