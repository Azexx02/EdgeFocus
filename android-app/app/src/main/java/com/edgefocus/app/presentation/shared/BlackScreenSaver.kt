package com.edgefocus.app.presentation.shared

import android.os.SystemClock
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.zIndex
import kotlinx.coroutines.delay

@Composable
fun BlackScreenSaver(
    enabled: Boolean,
    timeoutSeconds: Int,
    content: @Composable () -> Unit
) {
    var saverVisible by remember { mutableStateOf(false) }
    var lastInteraction by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    val timeoutMs = timeoutSeconds.coerceIn(15, 3_600) * 1_000L

    LaunchedEffect(enabled, timeoutMs, lastInteraction) {
        if (!enabled) {
            saverVisible = false
            return@LaunchedEffect
        }
        delay(timeoutMs)
        saverVisible = true
    }

    Box(
        Modifier
            .fillMaxSize()
            .pointerInput(enabled, saverVisible) {
                awaitPointerEventScope {
                    while (true) {
                        awaitPointerEvent(PointerEventPass.Initial)
                        if (enabled && !saverVisible) {
                            lastInteraction = SystemClock.elapsedRealtime()
                        }
                    }
                }
            }
    ) {
        content()
        if (enabled && saverVisible) {
            Box(
                Modifier
                    .fillMaxSize()
                    .zIndex(100f)
                    .background(Color.Black)
                    .pointerInput(Unit) {
                        detectTapGestures {
                            saverVisible = false
                            lastInteraction = SystemClock.elapsedRealtime()
                        }
                    }
            )
        }
    }
}
