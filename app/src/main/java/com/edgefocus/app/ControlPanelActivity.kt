package com.edgefocus.app

import android.content.pm.ActivityInfo
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import com.edgefocus.app.presentation.ui.controlpanel.ControlPanelScreen
import com.edgefocus.app.presentation.ui.theme.EdgeFocusTheme
import com.edgefocus.app.presentation.shared.BlackScreenSaver
import com.edgefocus.app.data.local.dao.SettingsDao
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import javax.inject.Inject
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class ControlPanelActivity : ComponentActivity() {
    @Inject
    lateinit var settingsDao: SettingsDao

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        // 强制横屏
        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        setContent {
            val appSettings by settingsDao.getSettings().collectAsState(initial = null)
            EdgeFocusTheme {
                BlackScreenSaver(
                    enabled = appSettings?.screenSaverEnabled == true,
                    timeoutSeconds = appSettings?.screenSaverTimeoutSec ?: 120
                ) {
                    Surface(
                        modifier = Modifier.fillMaxSize(),
                        color = MaterialTheme.colorScheme.background
                    ) {
                        ControlPanelScreen()
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        // 恢复竖屏（在 MainActivity 中会自动处理）
    }
}
