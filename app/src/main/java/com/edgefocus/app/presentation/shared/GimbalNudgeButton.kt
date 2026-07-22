package com.edgefocus.app.presentation.shared

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.coroutineScope

@Composable
fun GimbalStepModeSelector(
    fineTune: Boolean,
    enabled: Boolean,
    onModeChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    Row(modifier = modifier, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        if (fineTune) {
            Button(onClick = { onModeChange(true) }, enabled = enabled) { Text("微调") }
        } else {
            OutlinedButton(onClick = { onModeChange(true) }, enabled = enabled) { Text("微调") }
        }
        if (fineTune) {
            OutlinedButton(onClick = { onModeChange(false) }, enabled = enabled) { Text("普通步进") }
        } else {
            Button(onClick = { onModeChange(false) }, enabled = enabled) { Text("普通步进") }
        }
    }
}

@Composable
fun GimbalNudgeButton(
    label: String,
    enabled: Boolean,
    fineTune: Boolean,
    onNudge: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    val latestNudge by rememberUpdatedState(onNudge)
    val contentColor = if (enabled) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
    }
    val interaction = if (enabled) {
            Modifier.pointerInput(fineTune) {
            detectTapGestures(
                onPress = press@ {
                    var repeated = false
                    coroutineScope {
                        val repeatJob = launch {
                            delay(LONG_PRESS_DELAY_MS)
                            repeated = true
                            latestNudge(fineTune)
                            while (true) {
                                delay(REPEAT_INTERVAL_MS)
                                latestNudge(fineTune)
                            }
                        }
                        val released = this@press.tryAwaitRelease()
                        repeatJob.cancel()
                        if (released && !repeated) latestNudge(fineTune)
                    }
                }
            )
        }
    } else {
        Modifier
    }
    val accessibility = Modifier.semantics {
        role = Role.Button
        if (enabled) {
            onClick {
                latestNudge(fineTune)
                true
            }
        } else {
            disabled()
        }
    }

    Surface(
        modifier = modifier.then(accessibility).then(interaction),
        shape = RoundedCornerShape(20.dp),
        color = Color.Transparent,
        contentColor = contentColor,
        border = BorderStroke(
            1.dp,
            if (enabled) MaterialTheme.colorScheme.outline else
                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
        )
    ) {
        Box(
            modifier = Modifier
                .widthIn(min = 64.dp)
                .heightIn(min = 40.dp)
                .padding(horizontal = 18.dp, vertical = 8.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(label)
        }
    }
}

private const val LONG_PRESS_DELAY_MS = 400L
private const val REPEAT_INTERVAL_MS = 150L
