package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import com.edgefocus.app.domain.model.DeviceStatus
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class BleViewModel @Inject constructor(
    private val bleManager: BleManager
) : ViewModel() {

    val connectionState: StateFlow<ConnectionState> = bleManager.connectionState
    val deviceStatus: StateFlow<DeviceStatus?> = bleManager.deviceStatus

    fun startScan() {
        bleManager.startScan()
    }

    fun stopScan() {
        bleManager.stopScan()
    }

    fun disconnect() {
        bleManager.disconnect()
    }

    fun sendSessionCommand(action: String, durationMinutes: Int? = null) {
        if (connectionState.value != ConnectionState.CONNECTED) return
        viewModelScope.launch {
            bleManager.sessionControl(action, durationMinutes)
        }
    }

    fun isConnected(): Boolean {
        return connectionState.value == ConnectionState.CONNECTED
    }
}
