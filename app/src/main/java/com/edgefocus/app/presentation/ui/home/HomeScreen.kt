package com.edgefocus.app.presentation.ui.home

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.BluetoothDisabled
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Work
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.SessionType
import com.edgefocus.app.domain.model.TimerState
import com.edgefocus.app.presentation.viewmodel.BleViewModel
import com.edgefocus.app.presentation.viewmodel.HomeViewModel
import com.edgefocus.app.presentation.viewmodel.SettingsViewModel
import com.edgefocus.app.util.TimeUtils

@Composable
fun HomeScreen(
    viewModel: HomeViewModel = hiltViewModel(),
    bleViewModel: BleViewModel = hiltViewModel(),
    settingsViewModel: SettingsViewModel = hiltViewModel(),
    onNavigateToControlPanel: () -> Unit = {}
) {
    val uiState by viewModel.uiState.collectAsState()
    val settings by settingsViewModel.settings.collectAsState()
    val effectiveConnectionState = bleViewModel.getEffectiveConnectionState()

    val useBoard = settings.useEdgeFocusBoard
    val isConnected = bleViewModel.isConnected()

    val canStart = when {
        useBoard && uiState.timerState == TimerState.IDLE -> isConnected
        else -> true
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Top Bar
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Spacer(modifier = Modifier.size(48.dp))
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(
                    text = "EdgeFocus",
                    style = MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.pointerInput(Unit) {
                        detectTapGestures(
                            onLongPress = { onNavigateToControlPanel() }
                        )
                    }
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .background(
                                if (isConnected) MaterialTheme.colorScheme.secondary
                                else MaterialTheme.colorScheme.surfaceVariant,
                                CircleShape
                            )
                    )
                Text(
                    text = when {
                        !isConnected && useBoard -> "Device Disconnected"
                        useBoard -> "EdgeFocus Mode"
                        else -> "Local Mode"
                    },
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            IconButton(onClick = { }) {
                Icon(
                    if (isConnected) Icons.Default.BluetoothConnected
                    else Icons.Default.BluetoothDisabled,
                    contentDescription = "Bluetooth",
                    tint = if (isConnected) MaterialTheme.colorScheme.primary
                    else MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(20.dp)
                )
            }
        }

        Spacer(modifier = Modifier.height(32.dp))

        // Context Tag
        val sessionTypeText = when (uiState.sessionType) {
            SessionType.WORK -> "Work Session"
        }
        Row(
            modifier = Modifier
                .background(MaterialTheme.colorScheme.surfaceVariant, CircleShape)
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(
                Icons.Default.Work,
                contentDescription = null,
                modifier = Modifier.size(18.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = sessionTypeText,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Spacer(modifier = Modifier.height(48.dp))

        // Timer Circle
        Box(
            modifier = Modifier
                .size(300.dp)
                .shadow(
                    20.dp,
                    CircleShape,
                    spotColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                )
                .background(MaterialTheme.colorScheme.surface, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            if (uiState.timerState == TimerState.IDLE) {
                DurationSelectorRing(
                    selectedMinutes = viewModel.getWorkDurationMinutes(),
                    onDurationChanged = { viewModel.setWorkDuration(it) }
                )
            } else {
                PomodoroTimer(
                    timeMillis = uiState.timeRemaining,
                    totalTimeMillis = uiState.totalTime
                )
            }
        }

        Spacer(modifier = Modifier.height(48.dp))

        Text(
            text = "Sessions Completed: ${uiState.completedSessions}",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Controls
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(64.dp),
            contentAlignment = Alignment.Center
        ) {
            if (uiState.timerState == TimerState.IDLE || uiState.timerState == TimerState.FINISHED) {
                TactileButton(
                    onClick = { viewModel.startTimer(useBoard && isConnected) },
                    modifier = Modifier
                        .fillMaxWidth(0.8f)
                        .fillMaxHeight(),
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = Color.White,
                    enabled = canStart
                ) {
                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                        if (useBoard && isConnected) "Start EdgeFocus" else "Start Focus",
                        style = MaterialTheme.typography.headlineMedium
                    )
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(0.9f),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    TactileButton(
                        onClick = {
                            if (uiState.timerState == TimerState.RUNNING) {
                                viewModel.pauseTimer()
                            } else {
                                viewModel.startTimer(useBoard && isConnected)
                            }
                        },
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxHeight(),
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        contentColor = MaterialTheme.colorScheme.onSurface
                    ) {
                        Icon(
                            if (uiState.timerState == TimerState.RUNNING)
                                Icons.Default.Pause
                            else Icons.Default.PlayArrow,
                            contentDescription = null
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(
                            if (uiState.timerState == TimerState.RUNNING)
                                "Pause"
                            else "Resume",
                            style = MaterialTheme.typography.headlineMedium
                        )
                    }
                    TactileButton(
                        onClick = { viewModel.stopTimer() },
                        modifier = Modifier.size(88.dp),
                        containerColor = MaterialTheme.colorScheme.surface,
                        contentColor = MaterialTheme.colorScheme.error
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null, modifier = Modifier.size(40.dp))
                    }
                }
            }
        }

        if (useBoard && !isConnected && uiState.timerState == TimerState.IDLE) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "Connect device to use EdgeFocus mode",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
fun TactileButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color,
    contentColor: Color,
    enabled: Boolean = true,
    content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()

    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessMedium
        ),
        label = "button_scale"
    )

    Surface(
        modifier = modifier
            .scale(scale)
            .shadow(
                if (isPressed) 2.dp else 8.dp,
                CircleShape,
                spotColor = containerColor.copy(alpha = 0.5f)
            )
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                enabled = enabled,
                onClick = onClick
            ),
        shape = CircleShape,
        color = if (enabled) containerColor else containerColor.copy(alpha = 0.5f),
        contentColor = if (enabled) contentColor else contentColor.copy(alpha = 0.5f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 24.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
            content = content
        )
    }
}
