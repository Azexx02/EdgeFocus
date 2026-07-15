package com.edgefocus.app

import android.Manifest
import android.app.AppOpsManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.nfc.NdefMessage
import android.nfc.NfcAdapter
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import android.provider.Settings
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
import com.edgefocus.app.presentation.navigation.NavigationGraph
import com.edgefocus.app.presentation.ui.theme.EdgeFocusTheme
import com.edgefocus.app.presentation.shared.AlertOverlay
import com.edgefocus.app.presentation.shared.BlackScreenSaver
import com.edgefocus.app.domain.alert.AlertDeliveryCoordinator
import com.edgefocus.app.data.local.dao.SettingsDao
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import javax.inject.Inject
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class MainActivity : ComponentActivity() {

    @Inject
    lateinit var alertDeliveryCoordinator: AlertDeliveryCoordinator

    @Inject
    lateinit var settingsDao: SettingsDao

    companion object {
        private const val TAG = "MainActivity"
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { permissions ->
        val allGranted = permissions.entries.all { it.value }
        if (!allGranted) {
            Log.w(TAG, "Some permissions were not granted")
        }
        checkUsageStatsPermission()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        requestPermissions()

        // NFC / Deep Link: edgefocus://controlpanel
        handleNfcIntent(intent)
        intent = null

        setContent {
            val activeAlert by alertDeliveryCoordinator.activeAlert.collectAsState()
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
                        Box(Modifier.fillMaxSize()) {
                            NavigationGraph()
                            activeAlert?.let {
                                AlertOverlay(
                                    activeAlert = it,
                                    onAcknowledge = alertDeliveryCoordinator::userAcknowledgeActiveAlert
                                )
                            }
                        }
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        handleNfcIntent(intent)
    }

    private fun handleNfcIntent(intent: Intent?) {
        if (intent == null) return

        val opensControlPanel = intent.data?.let {
            it.scheme == "edgefocus" && it.host == "controlpanel"
        } == true || containsControlPanelNdef(intent)

        if (opensControlPanel) {
            startActivity(Intent(this, ControlPanelActivity::class.java))
            finish()
        }
    }

    @Suppress("DEPRECATION")
    private fun containsControlPanelNdef(intent: Intent): Boolean {
        if (intent.action != NfcAdapter.ACTION_NDEF_DISCOVERED) return false

        return intent.getParcelableArrayExtra(NfcAdapter.EXTRA_NDEF_MESSAGES)
            ?.filterIsInstance<NdefMessage>()
            ?.flatMap { it.records.asIterable() }
            ?.any { record ->
                val uri = record.toUri()
                (uri?.scheme == "edgefocus" && uri.host == "controlpanel") ||
                    String(record.payload, Charsets.UTF_8).contains("edgefocus://controlpanel")
            } == true
    }

    private fun requestPermissions() {
        val permissions = mutableListOf<String>()

        // BLE 权限
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
        }

        // 位置权限（BLE 扫描需要）
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) {
            permissions.add(Manifest.permission.ACCESS_FINE_LOCATION)
        }

        // 通知权限（Android 13+）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }

        if (permissions.isNotEmpty()) {
            permissionLauncher.launch(permissions.toTypedArray())
        } else {
            checkUsageStatsPermission()
        }
    }

    private fun checkUsageStatsPermission() {
        val appOps = getSystemService(Context.APP_OPS_SERVICE) as AppOpsManager
        val mode = appOps.checkOpNoThrow(
            AppOpsManager.OPSTR_GET_USAGE_STATS,
            android.os.Process.myUid(),
            packageName
        )
        if (mode != AppOpsManager.MODE_ALLOWED) {
            val intent = Intent(Settings.ACTION_USAGE_ACCESS_SETTINGS)
            startActivity(intent)
        }
    }
}
