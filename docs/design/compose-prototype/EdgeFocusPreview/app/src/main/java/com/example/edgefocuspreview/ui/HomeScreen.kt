package com.example.edgefocuspreview.ui

import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BluetoothConnected
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Work
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin

@Composable
fun HomeScreen() {
    var timerState by remember { mutableStateOf(TimerState.IDLE) }
    var totalTimeSeconds by remember { mutableStateOf(25 * 60) }
    var timeRemainingSeconds by remember { mutableStateOf(25 * 60) }

    // Mock timer ticking
    LaunchedEffect(timerState) {
        if (timerState == TimerState.RUNNING) {
            while (timeRemainingSeconds > 0 && timerState == TimerState.RUNNING) {
                kotlinx.coroutines.delay(1000L)
                timeRemainingSeconds--
            }
            if (timeRemainingSeconds == 0) {
                timerState = TimerState.IDLE
            }
        }
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
                    color = MaterialTheme.colorScheme.primary
                )
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    Box(modifier = Modifier.size(8.dp).background(MaterialTheme.colorScheme.secondary, CircleShape))
                    Text("EdgeFocus Mode", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            IconButton(onClick = { }) {
                Icon(Icons.Default.BluetoothConnected, contentDescription = "Bluetooth", tint = MaterialTheme.colorScheme.primary)
            }
        }

        Spacer(modifier = Modifier.height(32.dp))

        // Context Tag
        Row(
            modifier = Modifier
                .background(MaterialTheme.colorScheme.surfaceVariant, CircleShape)
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Icon(Icons.Default.Work, contentDescription = null, modifier = Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Text("Work Session", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }

        Spacer(modifier = Modifier.height(48.dp))

        // Timer Circle
        Box(
            modifier = Modifier
                .size(300.dp)
                .shadow(20.dp, CircleShape, spotColor = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f))
                .background(MaterialTheme.colorScheme.surface, CircleShape),
            contentAlignment = Alignment.Center
        ) {
            CircularTimer(
                totalTime = totalTimeSeconds,
                timeRemaining = timeRemainingSeconds,
                isIdle = timerState == TimerState.IDLE,
                onTimeChange = { newTime ->
                    totalTimeSeconds = newTime
                    timeRemainingSeconds = newTime
                }
            )

            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                val mins = timeRemainingSeconds / 60
                val secs = timeRemainingSeconds % 60
                Text(
                    text = "${mins.toString().padStart(2, '0')}:${secs.toString().padStart(2, '0')}",
                    style = MaterialTheme.typography.displayLarge,
                    color = MaterialTheme.colorScheme.onSurface
                )
                if (timerState == TimerState.IDLE) {
                    Text(
                        text = "Drag to set time",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(48.dp))

        Text(
            text = "Sessions Completed: 3",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(16.dp))

        // Controls
        Box(modifier = Modifier.fillMaxWidth().height(64.dp), contentAlignment = Alignment.Center) {
            if (timerState == TimerState.IDLE) {
                TactileButton(
                    onClick = { timerState = TimerState.RUNNING },
                    modifier = Modifier.fillMaxWidth(0.8f).fillMaxHeight(),
                    containerColor = MaterialTheme.colorScheme.primary,
                    contentColor = Color.White
                ) {
                    Icon(Icons.Default.PlayArrow, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Start Focus", style = MaterialTheme.typography.headlineMedium)
                }
            } else {
                Row(
                    modifier = Modifier.fillMaxWidth(0.9f),
                    horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    TactileButton(
                        onClick = { 
                            timerState = if (timerState == TimerState.RUNNING) TimerState.PAUSED else TimerState.RUNNING 
                        },
                        modifier = Modifier.weight(1f).fillMaxHeight(),
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        contentColor = MaterialTheme.colorScheme.onSurface
                    ) {
                        Icon(if (timerState == TimerState.RUNNING) Icons.Default.Pause else Icons.Default.PlayArrow, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text(if (timerState == TimerState.RUNNING) "Pause" else "Resume", style = MaterialTheme.typography.headlineMedium)
                    }
                    TactileButton(
                        onClick = {
                            timerState = TimerState.IDLE
                            timeRemainingSeconds = totalTimeSeconds
                        },
                        modifier = Modifier.size(64.dp),
                        containerColor = MaterialTheme.colorScheme.surface,
                        contentColor = MaterialTheme.colorScheme.error
                    ) {
                        Icon(Icons.Default.Stop, contentDescription = null)
                    }
                }
            }
        }
    }
}

enum class TimerState { IDLE, RUNNING, PAUSED }

@Composable
fun CircularTimer(
    totalTime: Int,
    timeRemaining: Int,
    isIdle: Boolean,
    onTimeChange: (Int) -> Unit
) {
    val primaryColor = MaterialTheme.colorScheme.primary
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val strokeWidth = 16.dp
    
    val maxMinutes = 120

    Canvas(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
            .pointerInput(isIdle) {
                if (!isIdle) return@pointerInput
                detectDragGestures { change, _ ->
                    change.consume()
                    val center = Offset(size.width / 2, size.height / 2)
                    val angle = atan2(change.position.y - center.y, change.position.x - center.x)
                    // Map angle back to -90 starting point
                    var degrees = Math.toDegrees(angle.toDouble()) + 90
                    if (degrees < 0) degrees += 360
                    
                    val fraction = degrees / 360f
                    val minutes = (fraction * maxMinutes).toInt().coerceIn(1, maxMinutes)
                    onTimeChange(minutes * 60)
                }
            }
    ) {
        val fraction = timeRemaining.toFloat() / (maxMinutes * 60).toFloat()
        val sweepAngle = fraction * 360f

        // Track
        drawArc(
            color = trackColor.copy(alpha = 0.5f),
            startAngle = 0f,
            sweepAngle = 360f,
            useCenter = false,
            style = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
        )

        // Progress
        drawArc(
            color = primaryColor,
            startAngle = -90f,
            sweepAngle = sweepAngle,
            useCenter = false,
            style = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
        )

        if (isIdle) {
            // Draw thumb
            val radius = size.width / 2
            val angleRad = Math.toRadians((sweepAngle - 90).toDouble())
            val thumbX = center.x + radius * cos(angleRad).toFloat()
            val thumbY = center.y + radius * sin(angleRad).toFloat()

            drawCircle(
                color = Color.White,
                radius = 12.dp.toPx(),
                center = Offset(thumbX, thumbY)
            )
            drawCircle(
                color = primaryColor,
                radius = 12.dp.toPx(),
                center = Offset(thumbX, thumbY),
                style = Stroke(width = 4.dp.toPx())
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
    content: @Composable RowScope.() -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    
    val scale by animateFloatAsState(
        targetValue = if (isPressed) 0.95f else 1f,
        animationSpec = spring(dampingRatio = Spring.DampingRatioMediumBouncy, stiffness = Spring.StiffnessMedium)
    )

    Surface(
        modifier = modifier
            .scale(scale)
            .shadow(if (isPressed) 2.dp else 8.dp, CircleShape, spotColor = containerColor.copy(alpha = 0.5f))
            .clickable(interactionSource = interactionSource, indication = null, onClick = onClick),
        shape = CircleShape,
        color = containerColor,
        contentColor = contentColor
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 24.dp),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically,
            content = content
        )
    }
}
