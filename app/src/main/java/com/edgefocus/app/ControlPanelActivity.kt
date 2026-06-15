package com.edgefocus.app

import android.content.pm.ActivityInfo
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.edgefocus.app.presentation.ui.controlpanel.ControlPanelScreen
import com.edgefocus.app.presentation.ui.theme.EdgeFocusTheme
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class ControlPanelActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // 强制横屏
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        setContent {
            EdgeFocusTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = MaterialTheme.colorScheme.background
                ) {
                    ControlPanelScreen()
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // 恢复竖屏（在 MainActivity 中会自动处理）
    }
}
