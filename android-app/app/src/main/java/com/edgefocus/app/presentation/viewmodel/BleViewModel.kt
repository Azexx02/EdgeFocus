package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.BuildConfig
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BleScanStartPolicy
import com.edgefocus.app.data.remote.BleSimulationPolicy
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.data.remote.PhoneUsage
import com.edgefocus.app.data.remote.PushEventHandler
import com.edgefocus.app.data.remote.TimerComplete
import com.edgefocus.app.data.ble.BleAutoConnectCoordinator
import com.edgefocus.app.domain.model.DeviceStatus
import com.edgefocus.app.domain.model.FocusState
import com.edgefocus.app.domain.repository.ISettingsRepository
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
    private val bleManager: BleManager,
    private val settingsRepository: ISettingsRepository,
    private val autoConnectCoordinator: BleAutoConnectCoordinator
) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "BleViewModel"
    }

    private val prefs = application.getSharedPreferences("test_prefs", 0)

    private val _simulatedConnectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val simulatedConnectionState: StateFlow<ConnectionState> = _simulatedConnectionState.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = bleManager.connectionState
    val deviceStatus: StateFlow<DeviceStatus?> = bleManager.deviceStatus
    val discoveredDevices: StateFlow<List<BleManager.DiscoveredDevice>> = bleManager.discoveredDevices

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    // 推送事件流
    private val _timerCompleteEvent = MutableSharedFlow<TimerComplete>()
    val timerCompleteEvent: SharedFlow<TimerComplete> = _timerCompleteEvent.asSharedFlow()

    private val pushEventHandler = object : PushEventHandler {
        override fun onPushState(state: FocusState) {
            Log.d(TAG, "Push state: $state")
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
    }

    init {
        val simulateBle = BleSimulationPolicy.isSimulationEnabled(
            isDebug = BuildConfig.DEBUG,
            preferenceEnabled = prefs.getBoolean("simulate_ble", false)
        )
        if (!BuildConfig.DEBUG) {
            prefs.edit().remove("simulate_ble").apply()
        }
        // 恢复模拟状态
        if (simulateBle) {
            _simulatedConnectionState.value = ConnectionState.CONNECTED
        }

        // 设置推送事件处理器
        bleManager.setPushEventHandler(pushEventHandler)
    }

    fun isSimulatingBle(): Boolean {
        return BleSimulationPolicy.isSimulationEnabled(
            isDebug = BuildConfig.DEBUG,
            preferenceEnabled = prefs.getBoolean("simulate_ble", false)
        )
    }

    fun getEffectiveConnectionState(): ConnectionState {
        return BleSimulationPolicy.effectiveConnectionState(
            isDebug = BuildConfig.DEBUG,
            preferenceEnabled = prefs.getBoolean("simulate_ble", false),
            simulatedConnected = _simulatedConnectionState.value == ConnectionState.CONNECTED,
            actualState = connectionState.value
        )
    }

    fun startScan() {
        val decision = BleScanStartPolicy.decide(isSimulatingBle())
        if (decision.shouldDisableSimulation) {
            prefs.edit().putBoolean("simulate_ble", false).apply()
            bleManager.setSimulatedConnected(false)
            _simulatedConnectionState.value = ConnectionState.DISCONNECTED
        }
        if (decision.shouldStartRealScan) {
            autoConnectCoordinator.onUserRequestedScan()
            bleManager.startScan(autoConnectStoredTarget = decision.shouldAutoConnectStoredTarget)
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
            autoConnectCoordinator.onUserDisconnected()
            bleManager.disconnect()
        }
    }

    fun connectToDiscoveredDevice(address: String) {
        if (!isSimulatingBle()) {
            autoConnectCoordinator.onUserSelectedDevice(address)
            bleManager.connectToDiscoveredDevice(address)
        }
    }

    fun setTargetMac(mac: String) {
        bleManager.setTargetMac(mac)
    }

    fun clearDiscoveredDevices() {
        bleManager.clearDiscoveredDevices()
    }

    fun saveMacAddress(mac: String) {
        bleManager.setTargetMac(mac)
        viewModelScope.launch {
            val settings = settingsRepository.getSettingsOnce()
                ?: com.edgefocus.app.data.local.entity.Settings()
            settingsRepository.updateSettings(settings.copy(edgeFocusMacAddress = mac))
        }
    }

    suspend fun sendSessionCommand(action: String, targetDurationMin: Int? = null): Boolean {
        if (isSimulatingBle()) {
            // 模拟模式下直接返回成功
            Log.d(TAG, "Simulated BLE command: $action, duration: $targetDurationMin")
            return true
        }

        if (!bleManager.isConnected()) return false
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

        if (!bleManager.isConnected()) return false
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

        if (!bleManager.isConnected()) return null
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
            bleManager.isConnected()
        }
    }

    override fun onCleared() {
        bleManager.removePushEventHandler(pushEventHandler)
        super.onCleared()
    }
}
