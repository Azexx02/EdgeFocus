package com.edgefocus.app.data.ble

import android.bluetooth.BluetoothAdapter
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.data.remote.BleManager
import dagger.hilt.android.qualifiers.ApplicationContext
import java.util.concurrent.atomic.AtomicBoolean
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

@Singleton
class BleAutoConnectCoordinator @Inject constructor(
    @ApplicationContext private val context: Context,
    private val settingsDao: SettingsDao,
    private val bleManager: BleManager,
    private val gate: BleAutoConnectGate
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val started = AtomicBoolean(false)
    private val attemptMutex = Mutex()
    private val legacyPreferences = context.getSharedPreferences("test_prefs", Context.MODE_PRIVATE)

    private val bluetoothStateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (intent?.action == BluetoothAdapter.ACTION_STATE_CHANGED &&
                intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR) == BluetoothAdapter.STATE_ON
            ) {
                retryNow()
            }
        }
    }

    fun start() {
        if (!started.compareAndSet(false, true)) return
        ContextCompat.registerReceiver(
            context,
            bluetoothStateReceiver,
            IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED),
            // Bluetooth state broadcasts can originate from the privileged
            // Bluetooth process rather than the system UID.
            ContextCompat.RECEIVER_EXPORTED
        )
        scope.launch {
            ensureSettings()
            settingsDao.getSettings().filterNotNull().collect { settings ->
                applySettings(settings)
                attemptKnownTarget()
            }
        }
    }

    fun retryNow() {
        scope.launch {
            val settings = settingsDao.getSettingsOnce() ?: ensureSettings()
            applySettings(settings)
            attemptKnownTarget()
        }
    }

    fun onUserRequestedScan() {
        gate.allowUserConnection()
    }

    fun onUserSelectedDevice(address: String) {
        gate.allowUserConnection()
        gate.updateTargetMac(address)
        bleManager.setTargetMac(address)
    }

    fun onUserDisconnected() {
        gate.markUserDisconnected()
    }

    private suspend fun ensureSettings(): Settings {
        val existing = settingsDao.getSettingsOnce()
        if (existing != null) return existing
        val defaults = Settings()
        settingsDao.insertSettings(defaults)
        return defaults
    }

    private suspend fun applySettings(settings: Settings) {
        val roomMac = settings.edgeFocusMacAddress.trim()
        val legacyMac = legacyPreferences.getString("edge_focus_mac", "").orEmpty().trim()
        val preferredMac = roomMac.ifBlank { legacyMac }
        val effectiveSettings = if (roomMac.isBlank() && legacyMac.isNotBlank()) {
            settings.copy(edgeFocusMacAddress = legacyMac).also { migrated ->
                settingsDao.updateSettings(migrated)
            }
        } else {
            settings
        }
        gate.configure(effectiveSettings.useEdgeFocusBoard, preferredMac)
        if (preferredMac.isNotBlank()) bleManager.setTargetMac(preferredMac)
    }

    private suspend fun attemptKnownTarget() = attemptMutex.withLock {
        val decision = gate.evaluate(bleManager.connectionState.value)
        if (decision.allowed) {
            bleManager.connectToDiscoveredDevice(decision.targetMac!!)
        }
    }
}
