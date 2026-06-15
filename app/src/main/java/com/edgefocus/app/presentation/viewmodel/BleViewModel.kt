package com.edgefocus.app.presentation.viewmodel

import android.util.Log
import androidx.lifecycle.ViewModel
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
    private val bleManager: BleManager
) : ViewModel() {

    companion object {
        private const val TAG = "BleViewModel"
    }

    val connectionState: StateFlow<ConnectionState> = bleManager.connectionState
    val deviceStatus: StateFlow<DeviceStatus?> = bleManager.deviceStatus

    private val _lastError = MutableStateFlow<String?>(null)
    val lastError: StateFlow<String?> = _lastError.asStateFlow()

    fun startScan() {
        bleManager.startScan()
    }

    fun stopScan() {
        bleManager.stopScan()
    }

    fun disconnect() {
        bleManager.disconnect()
    }

    suspend fun sendSessionCommand(action: String, durationMinutes: Int? = null): Boolean {
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
        return connectionState.value == ConnectionState.CONNECTED
    }
}
