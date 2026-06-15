package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.PhoneUsage
import com.edgefocus.app.data.remote.PushEventHandler
import com.edgefocus.app.data.remote.TimerComplete
import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.DeviceStatus
import com.edgefocus.app.domain.model.FocusState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
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

    // 推送事件流
    private val _timerCompleteEvent = MutableSharedFlow<TimerComplete>()
    val timerCompleteEvent: SharedFlow<TimerComplete> = _timerCompleteEvent.asSharedFlow()

    private val _alertEvent = MutableSharedFlow<AlertInfo>()
    val alertEvent: SharedFlow<AlertInfo> = _alertEvent.asSharedFlow()

    init {
        // 恢复模拟状态
        val simulateBle = prefs.getBoolean("simulate_ble", false)
        if (simulateBle) {
            _simulatedConnectionState.value = ConnectionState.CONNECTED
        }

        // 设置推送事件处理器
        bleManager.setPushEventHandler(object : PushEventHandler {
            override fun onPushState(state: FocusState) {
                Log.d(TAG, "Push state: $state")
            }

            override fun onPushAlert(alert: AlertInfo) {
                Log.d(TAG, "Push alert: $alert")
                viewModelScope.launch {
                    _alertEvent.emit(alert)
                }
            }

            override fun onPushSessionEnd(summary: String) {
                Log.d(TAG, "Push session end: $summary")
            }

            override fun onPushTimerComplete(timerComplete: TimerComplete) {
                Log.d(TAG, "Push timer complete: $timerComplete")
                viewModelScope.launch {
                    _timerCompleteEvent.emit(timerComplete)
                }
            }
        })
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

    suspend fun sendSessionCommand(action: String, targetDurationMin: Int? = null): Boolean {
        if (isSimulatingBle()) {
            // 模拟模式下直接返回成功
            Log.d(TAG, "Simulated BLE command: $action, duration: $targetDurationMin")
            return true
        }

        if (connectionState.value != ConnectionState.CONNECTED) return false
        val result = bleManager.sessionControl(action, targetDurationMin)
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

    suspend fun phoneSync(usage: PhoneUsage): Boolean {
        if (isSimulatingBle()) {
            Log.d(TAG, "Simulated phone sync: $usage")
            return true
        }

        if (connectionState.value != ConnectionState.CONNECTED) return false
        val result = bleManager.phoneSync(usage)
        return result.fold(
            onSuccess = {
                _lastError.value = null
                true
            },
            onFailure = { error ->
                Log.e(TAG, "Phone sync failed: ${error.message}", error)
                _lastError.value = error.message
                false
            }
        )
    }

    suspend fun getConsoleData(): String? {
        if (isSimulatingBle()) {
            Log.d(TAG, "Simulated get console data")
            return null // 模拟模式返回null
        }

        if (connectionState.value != ConnectionState.CONNECTED) return null
        val result = bleManager.getConsoleData()
        return result.fold(
            onSuccess = { data ->
                _lastError.value = null
                String(data, Charsets.UTF_8)
            },
            onFailure = { error ->
                Log.e(TAG, "Get console data failed: ${error.message}", error)
                _lastError.value = error.message
                null
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
