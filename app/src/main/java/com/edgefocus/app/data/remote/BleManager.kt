package com.edgefocus.app.data.remote

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.util.Log
import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.AlertType
import com.edgefocus.app.domain.model.DeviceStatus
import com.edgefocus.app.domain.model.FocusState
import com.edgefocus.app.domain.model.SessionReport
import com.edgefocus.app.domain.model.TodaySummary
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID
import javax.inject.Inject
import javax.inject.Singleton

enum class ConnectionState {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    CONNECTED,
    ERROR
}

@Singleton
class BleManager @Inject constructor(
    @ApplicationContext private val context: Context
) : BleCommand {

    companion object {
        private const val TAG = "BleManager"

        val SERVICE_UUID: UUID = UUID.fromString("0000fff0-0000-1000-8000-00805f9b34fb")
        val CHAR_WRITE_UUID: UUID = UUID.fromString("0000fff1-0000-1000-8000-00805f9b34fb")
        val CHAR_NOTIFY_UUID: UUID = UUID.fromString("0000fff2-0000-1000-8000-00805f9b34fb")
        val DESCRIPTOR_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val bluetoothManager: BluetoothManager? =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter
    private var bluetoothGatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    private var notifyCharacteristic: BluetoothGattCharacteristic? = null

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus: StateFlow<DeviceStatus?> = _deviceStatus.asStateFlow()

    private var pushEventHandler: PushEventHandler? = null

    fun setPushEventHandler(handler: PushEventHandler) {
        this.pushEventHandler = handler
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            Log.d(TAG, "Found device: ${result.device.address}")
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(this)
            _connectionState.value = ConnectionState.CONNECTING
            connectToDevice(result.device)
        }

        override fun onScanFailed(errorCode: Int) {
            Log.e(TAG, "Scan failed with error: $errorCode")
            _connectionState.value = ConnectionState.ERROR
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothGatt.STATE_CONNECTED -> {
                    Log.d(TAG, "Connected to GATT server")
                    _connectionState.value = ConnectionState.CONNECTED
                    gatt.discoverServices()
                }
                BluetoothGatt.STATE_DISCONNECTED -> {
                    Log.d(TAG, "Disconnected from GATT server")
                    _connectionState.value = ConnectionState.DISCONNECTED
                    bluetoothGatt = null
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                writeCharacteristic = service?.getCharacteristic(CHAR_WRITE_UUID)
                notifyCharacteristic = service?.getCharacteristic(CHAR_NOTIFY_UUID)

                notifyCharacteristic?.let { char ->
                    gatt.setCharacteristicNotification(char, true)
                    val descriptor = char.getDescriptor(DESCRIPTOR_UUID)
                    descriptor?.let {
                        it.value = BluetoothGattCharacteristic.ENABLE_NOTIFICATION_VALUE
                        gatt.writeDescriptor(it)
                    }
                }
                Log.d(TAG, "Services discovered and configured")
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == CHAR_NOTIFY_UUID) {
                handleNotification(characteristic.value)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Characteristic written successfully")
            } else {
                Log.e(TAG, "Characteristic write failed: $status")
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (bluetoothAdapter?.isEnabled != true) {
            Log.e(TAG, "Bluetooth is not enabled")
            _connectionState.value = ConnectionState.ERROR
            return
        }
        _connectionState.value = ConnectionState.SCANNING
        bluetoothAdapter.bluetoothLeScanner?.startScan(scanCallback)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        if (_connectionState.value == ConnectionState.SCANNING) {
            _connectionState.value = ConnectionState.DISCONNECTED
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    @SuppressLint("MissingPermission")
    fun sendData(data: ByteArray) {
        val gatt = bluetoothGatt ?: run {
            Log.e(TAG, "Not connected")
            return
        }
        val characteristic = writeCharacteristic ?: run {
            Log.e(TAG, "Write characteristic not available")
            return
        }
        characteristic.value = data
        gatt.writeCharacteristic(characteristic)
    }

    private fun handleNotification(data: ByteArray) {
        if (data.isEmpty()) return

        when (data[0]) {
            0x01.toByte() -> {
                val state = parseState(data)
                pushEventHandler?.onPushState(state)
            }
            0x02.toByte() -> {
                val alert = parseAlert(data)
                pushEventHandler?.onPushAlert(alert)
            }
            0x03.toByte() -> {
                val summary = String(data, 1, data.size - 1)
                pushEventHandler?.onPushSessionEnd(summary)
            }
        }
    }

    private fun parseState(data: ByteArray): FocusState {
        val stateByte = if (data.size > 1) data[1] else 0
        val state = when (stateByte) {
            0x01.toByte() -> com.edgefocus.app.domain.model.TimerState.RUNNING
            0x02.toByte() -> com.edgefocus.app.domain.model.TimerState.PAUSED
            0x03.toByte() -> com.edgefocus.app.domain.model.TimerState.BREAK
            else -> com.edgefocus.app.domain.model.TimerState.IDLE
        }
        return FocusState(state = state)
    }

    private fun parseAlert(data: ByteArray): AlertInfo {
        val alertId = "alert_${System.currentTimeMillis()}"
        val type = if (data.size > 1 && data[1] == 0x01.toByte()) {
            AlertType.DISTRACTION
        } else {
            AlertType.SESSION_END
        }
        val message = if (data.size > 2) String(data, 2, data.size - 2) else "Alert"
        return AlertInfo(alertId = alertId, type = type, message = message)
    }

    override suspend fun getDeviceStatus(): ByteArray {
        return byteArrayOf(0x01)
    }

    override suspend fun getCurrentState(): ByteArray {
        return byteArrayOf(0x02)
    }

    override suspend fun getTodaySummary(): ByteArray {
        return byteArrayOf(0x03)
    }

    override suspend fun getLastReport(): ByteArray {
        return byteArrayOf(0x04)
    }

    override suspend fun sessionControl(action: String, duration: Int?): ByteArray {
        val actionByte = when (action) {
            "start" -> 0x01.toByte()
            "pause" -> 0x02.toByte()
            "resume" -> 0x03.toByte()
            "stop" -> 0x04.toByte()
            else -> 0x00.toByte()
        }
        val durationBytes = duration?.let {
            byteArrayOf((it shr 8).toByte(), it.toByte())
        } ?: byteArrayOf()
        return byteArrayOf(0x05, actionByte) + durationBytes
    }

    override suspend fun updatePreferences(key: String, value: String): ByteArray {
        val keyBytes = key.toByteArray()
        val valueBytes = value.toByteArray()
        return byteArrayOf(0x06, keyBytes.size.toByte()) + keyBytes + valueBytes
    }

    override suspend fun reminderAck(alertId: String): ByteArray {
        return byteArrayOf(0x07) + alertId.toByteArray()
    }

    override suspend fun phoneSync(usage: PhoneUsage): ByteArray {
        val screenTime = usage.screenTimeMinutes
        val unlocks = usage.unlockCount
        return byteArrayOf(
            0x08,
            (screenTime shr 8).toByte(),
            screenTime.toByte(),
            (unlocks shr 8).toByte(),
            unlocks.toByte()
        )
    }
}
