package com.edgefocus.app.presentation.ui.home

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.edgefocus.app.util.TimeUtils

@Composable
fun PomodoroTimer(
    timeMillis: Long,
    totalTimeMillis: Long,
    sizeDp: Int = 300,
    modifier: Modifier = Modifier
) {
    val progress = if (totalTimeMillis > 0) {
        timeMillis.toFloat() / totalTimeMillis.toFloat()
    } else {
        0f
    }

    val animatedProgress by animateFloatAsState(
        targetValue = progress,
        animationSpec = tween(durationMillis = 300),
        label = "timer_progress"
    )

    val primaryColor = MaterialTheme.colorScheme.primary
    val trackColor = MaterialTheme.colorScheme.surfaceVariant
    val fontSize = if (sizeDp <= 240) MaterialTheme.typography.displayLarge.copy(fontSize = 60.sp)
                   else MaterialTheme.typography.displayLarge

    Box(
        contentAlignment = Alignment.Center,
        modifier = modifier.size(sizeDp.dp)
    ) {
        Canvas(
            modifier = Modifier
                .size(sizeDp.dp)
                .padding(16.dp)
        ) {
            val strokeWidth = 16.dp.toPx()

            drawArc(
                color = trackColor.copy(alpha = 0.5f),
                startAngle = -90f,
                sweepAngle = 360f,
                useCenter = false,
                style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
            )

            drawArc(
                color = primaryColor,
                startAngle = -90f,
                sweepAngle = animatedProgress * 360f,
                useCenter = false,
                style = Stroke(width = strokeWidth, cap = StrokeCap.Round)
            )
        }
        Text(
            text = TimeUtils.formatTime(timeMillis),
            style = fontSize,
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}
