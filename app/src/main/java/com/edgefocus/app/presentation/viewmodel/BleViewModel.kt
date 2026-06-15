package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.DeviceStatus
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class BleViewModel @Inject constructor(
    application: Application,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "BleViewModel"
    }

    private val prefs = application.getSharedPreferences("test_prefs", 0)

    private val _simulatedConnectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val simulatedConnectionState: StateFlow<ConnectionState> = _simulatedConnectionState.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = bleManager.connectionState
    val deviceStatus: StateFlow<DeviceStatus?> = bleManager.deviceStatus

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    init {
        // 恢复模拟状态
        val simulateBle = prefs.getBoolean("simulate_ble", false)
        if (simulateBle) {
            _simulatedConnectionState.value = ConnectionState.CONNECTED
        }
    }

    fun isSimulatingBle(): Boolean {
        return prefs.getBoolean("simulate_ble", false)
    }

    fun getEffectiveConnectionState(): ConnectionState {
        return if (isSimulatingBle()) {
            _simulatedConnectionState.value
        } else {
            connectionState.value
        }
    }

    fun startScan() {
        if (!isSimulatingBle()) {
            bleManager.startScan()
        }
    }

    fun stopScan() {
        if (!isSimulatingBle()) {
            bleManager.stopScan()
        }
    }

    fun disconnect() {
        if (isSimulatingBle()) {
            _simulatedConnectionState.value = ConnectionState.DISCONNECTED
        } else {
            bleManager.disconnect()
        }
    }

    suspend fun sendSessionCommand(action: String, durationMinutes: Int? = null): Boolean {
        if (isSimulatingBle()) {
            // 模拟模式下直接返回成功
            Log.d(TAG, "Simulated BLE command: $action")
            return true
        }

        if (connectionState.value != ConnectionState.CONNECTED) return false
        val result = bleManager.sessionControl(action, durationMinutes)
        return result.fold(
            onSuccess = {
                _lastError.value = null
                true
            },
            onFailure = { error ->
                Log.e(TAG, "Session command '$action' failed: ${error.message}", error)
                _lastError.value = error.message
                false
            }
        )
    }

    fun isConnected(): Boolean {
        return if (isSimulatingBle()) {
            true // 模拟模式下始终返回已连接
        } else {
            connectionState.value == ConnectionState.CONNECTED
        }
    }
}
