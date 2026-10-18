package com.edgefocus.app.presentation.ui.home

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.atan2

private const val MAX_MINUTES = 120

@Composable
fun DurationSelectorRing(
    selectedMinutes: Int,
    onDurationChanged: (Int) -> Unit,
    sizeDp: Int = 300,
    modifier: Modifier = Modifier
) {
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val fillColor = MaterialTheme.colorScheme.primary
    val isSmall = sizeDp <= 240

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier.size(sizeDp.dp)
    ) {
        Canvas(
            modifier = Modifier
                .size(sizeDp.dp)
                .padding(16.dp)
                .pointerInput(Unit) {
                    detectDragGestures { change, _ ->
                        change.consume()
                        val center = Offset(size.width / 2f, size.height / 2f)
                        val delta = change.position - center
                        val angle = Math.toDegrees(
                            atan2(delta.y.toDouble(), delta.x.toDouble())
                        ).toFloat()
                        val normalizedAngle = (angle + 90f + 360f) % 360f
                        val minutes = ((normalizedAngle / 360f) * MAX_MINUTES)
                            .toInt().coerceIn(1, MAX_MINUTES)
                        onDurationChanged(minutes)
                    }
                }
        ) {
            val strokeWidth = 16.dp.toPx()

            drawArc(
                color = trackColor.copy(alpha = 0.5f),
                startAngle = -90f,
                sweepAngle = 360f,
                useCenter = false,
                style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
            )

            val sweepAngle = (selectedMinutes.toFloat() / MAX_MINUTES) * 360f
            drawArc(
                color = fillColor,
                startAngle = -90f,
                sweepAngle = sweepAngle,
                useCenter = false,
                style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = "${selectedMinutes}:00",
                style = if (isSmall) MaterialTheme.typography.displayLarge.copy(fontSize = 60.sp, fontWeight = FontWeight.Bold)
                        else MaterialTheme.typography.displayLarge,
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = "拖动设置时长",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

private fun mutableFloatOf(value: Float) = mutableFloatStateOf(value)
