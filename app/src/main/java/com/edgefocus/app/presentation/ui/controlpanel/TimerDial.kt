package com.edgefocus.app.presentation.ui.controlpanel

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
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
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import kotlin.math.atan2

@Composable
fun TimerDial(
    selectedMinutes: Int,
    onMinutesChanged: (Int) -> Unit,
    modifier: Modifier = Modifier,
    minMinutes: Int = 5,
    maxMinutes: Int = 120
) {
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val progressColor = MaterialTheme.colorScheme.primary
    val indicatorColor = MaterialTheme.colorScheme.primary
    val textColor = MaterialTheme.colorScheme.onSurface

    var dragAngle by remember { mutableFloatStateOf(selectedMinutesToAngle(selectedMinutes, minMinutes, maxMinutes)) }

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier.size(260.dp)
    ) {
        Canvas(
            modifier = Modifier
                .size(260.dp)
                .pointerInput(minMinutes, maxMinutes) {
                    detectDragGestures { change, _ ->
                        change.consume()
                        val center = Offset(size.width / 2f, size.height / 2f)
                        val dragOffset = change.position - center
                        var angle = Math.toDegrees(
                            atan2(dragOffset.y.toDouble(), dragOffset.x.toDouble())
                        ).toFloat()
                        angle = (angle + 90f + 360f) % 360f
                        dragAngle = angle
                        val fraction = angle / 360f
                        val minutes = (minMinutes + fraction * (maxMinutes - minMinutes)).toInt()
                            .coerceIn(minMinutes, maxMinutes)
                        onMinutesChanged(minutes)
                    }
                }
        ) {
                    val strokeWidth = 12.dp.toPx()
                    val diameter = size.minDimension - strokeWidth
                    val topLeft = Offset(strokeWidth / 2f, strokeWidth / 2f)
                    val arcSize = Size(diameter, diameter)

                    drawArc(
                        color = trackColor,
                        startAngle = -90f,
                        sweepAngle = 360f,
                        useCenter = false,
                        topLeft = topLeft,
                        size = arcSize,
                        style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
                    )

                    val sweepAngle = selectedMinutesToAngle(selectedMinutes, minMinutes, maxMinutes)
                    drawArc(
                        color = progressColor,
                        startAngle = -90f,
                        sweepAngle = sweepAngle,
                        useCenter = false,
                        topLeft = topLeft,
                        size = arcSize,
                        style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
                    )

                    val indicatorAngleRad = Math.toRadians((-90f + sweepAngle).toDouble())
                    val radius = diameter / 2f
                    val centerX = size.width / 2f
                    val centerY = size.height / 2f
                    val indicatorX = centerX + radius * kotlin.math.cos(indicatorAngleRad).toFloat()
                    val indicatorY = centerY + radius * kotlin.math.sin(indicatorAngleRad).toFloat()

                    drawCircle(
                        color = indicatorColor,
                        radius = strokeWidth,
                        center = Offset(indicatorX, indicatorY)
                    )
        }

        Text(
            text = "$selectedMinutes",
            style = MaterialTheme.typography.displayMedium,
            color = textColor
        )
    }

    Text(
        text = "minutes",
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant
    )
}

private fun selectedMinutesToAngle(minutes: Int, min: Int, max: Int): Float {
    val fraction = (minutes - min).toFloat() / (max - min).toFloat()
    return fraction * 360f
}
