package com.edgefocus.app.presentation.shared

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
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
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

enum class GimbalButtonArrangement {
    Horizontal,
    Vertical
}

@Composable
fun GimbalDirectionalStepButtons(
    directionLabel: String,
    enabled: Boolean,
    onNudge: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    arrangement: GimbalButtonArrangement = GimbalButtonArrangement.Horizontal
) {
    val content: @Composable () -> Unit = {
        GimbalNudgeButton(
            label = directionLabel,
            enabled = enabled,
            fineTune = false,
            onNudge = onNudge
        )
        GimbalNudgeButton(
            label = "$directionLabel 微调",
            enabled = enabled,
            fineTune = true,
            onNudge = onNudge
        )
    }
    when (arrangement) {
        GimbalButtonArrangement.Horizontal -> Row(
            modifier = modifier,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            content()
        }
        GimbalButtonArrangement.Vertical -> Column(
            modifier = modifier,
            verticalArrangement = Arrangement.spacedBy(6.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            content()
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
    var pressed by remember { mutableStateOf(false) }
    val visualPressed = enabled && pressed
    val contentColor = if (visualPressed) {
        Color.White
    } else if (enabled) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f)
    }
    val interaction = if (enabled) {
        Modifier.pointerInput(fineTune) {
            detectTapGestures(
                onPress = press@ {
                    pressed = true
                    var repeated = false
                    try {
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
                    } finally {
                        delay(PRESS_FEEDBACK_HOLD_MS)
                        pressed = false
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
        color = if (visualPressed) {
            MaterialTheme.colorScheme.primary
        } else {
            Color.Transparent
        },
        contentColor = contentColor,
        border = BorderStroke(
            1.dp,
            if (visualPressed) {
                MaterialTheme.colorScheme.primary
            } else if (enabled) {
                MaterialTheme.colorScheme.outline
            } else {
                MaterialTheme.colorScheme.outline.copy(alpha = 0.38f)
            }
        )
    ) {
        Box(
            modifier = Modifier
                .widthIn(min = 72.dp)
                .heightIn(min = 40.dp)
                .padding(horizontal = 14.dp, vertical = 8.dp),
            contentAlignment = Alignment.Center
        ) {
            Text(label)
        }
    }
}

private const val LONG_PRESS_DELAY_MS = 400L
private const val REPEAT_INTERVAL_MS = 150L
private const val PRESS_FEEDBACK_HOLD_MS = 120L
