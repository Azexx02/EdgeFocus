package com.edgefocus.app.data.ble

import android.Manifest
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import com.edgefocus.app.data.remote.ConnectionState
import dagger.hilt.android.qualifiers.ApplicationContext
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class BleAutoConnectGate @Inject constructor(
    @ApplicationContext private val context: Context
) {
    private val preferences = context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

    @Volatile
    private var useEdgeFocusBoard: Boolean = false

    @Volatile
    private var targetMac: String = ""

    fun configure(useEdgeFocusBoard: Boolean, targetMac: String) {
        this.useEdgeFocusBoard = useEdgeFocusBoard
        this.targetMac = targetMac.trim()
    }

    fun markUserDisconnected() {
        preferences.edit().putBoolean(KEY_USER_DISCONNECTED, true).apply()
    }

    fun allowUserConnection() {
        preferences.edit().putBoolean(KEY_USER_DISCONNECTED, false).apply()
    }

    fun updateTargetMac(mac: String) {
        targetMac = mac.trim()
    }

    fun evaluate(connectionState: ConnectionState): BleAutoConnectDecision =
        BleAutoConnectPolicy.decide(
            BleAutoConnectInputs(
                useEdgeFocusBoard = useEdgeFocusBoard,
                targetMac = targetMac,
                bluetoothEnabled = bluetoothEnabled(),
                permissionsGranted = permissionsGranted(),
                userDisconnected = preferences.getBoolean(KEY_USER_DISCONNECTED, false),
                connectionState = connectionState
            )
        )

    private fun bluetoothEnabled(): Boolean =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)
            ?.adapter
            ?.isEnabled == true

    private fun permissionsGranted(): Boolean {
        val fineLocationGranted = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
        if (!fineLocationGranted) return false
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S) return true
        return ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED && ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED
    }

    companion object {
        private const val PREFERENCES_NAME = "ble_connection_state"
        private const val KEY_USER_DISCONNECTED = "user_disconnected"
    }
}
