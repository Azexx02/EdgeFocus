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
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.coroutines.resume

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
        private const val WRITE_TIMEOUT_MS = 5000L
        private val DEVICE_NAME_FILTERS = listOf("EdgeFocus", "ESP32")

        val SERVICE_UUID: UUID = UUID.fromString("0000fff0-0000-1000-8000-00805f9b34fb")
        val CHAR_WRITE_UUID: UUID = UUID.fromString("0000fff1-0000-1000-8000-00805f9b34fb")
        val CHAR_NOTIFY_UUID: UUID = UUID.fromString("0000fff2-0000-1000-8000-00805f9b34fb")
        val DESCRIPTOR_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val bluetoothManager: BluetoothManager? =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val bluetoothAdapter: BluetoothAdapter? = bluetoothManager?.adapter

    @Volatile
    private var bluetoothGatt: BluetoothGatt? = null
    @Volatile
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile
    private var notifyCharacteristic: BluetoothGattCharacteristic? = null

    private val writeLock = Any()
    private var pendingWriteCallback: ((Boolean) -> Unit)? = null

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus: StateFlow<DeviceStatus?> = _deviceStatus.asStateFlow()

    private var pushEventHandler: PushEventHandler? = null
    private var bleLogListener: ((String, String) -> Unit)? = null

    fun setPushEventHandler(handler: PushEventHandler) {
        this.pushEventHandler = handler
    }

    fun setBleLogListener(listener: (direction: String, content: String) -> Unit) {
        this.bleLogListener = listener
    }

    private fun logBle(direction: String, content: String) {
        Log.d(TAG, "BLE $direction: $content")
        bleLogListener?.invoke(direction, content)
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val deviceName = result.device.name ?: ""
            if (DEVICE_NAME_FILTERS.none { deviceName.contains(it, ignoreCase = true) }) {
                Log.d(TAG, "Ignoring device: ${result.device.address} (name: '$deviceName')")
                return
            }
            Log.d(TAG, "Found target device: ${result.device.address} (name: '$deviceName')")
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
            if (newState == BluetoothGatt.STATE_CONNECTED) {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    Log.e(TAG, "Connection failed with GATT status: $status")
                    _connectionState.value = ConnectionState.ERROR
                    gatt.close()
                    synchronized(writeLock) {
                        bluetoothGatt = null
                        writeCharacteristic = null
                        notifyCharacteristic = null
                    }
                    return
                }
                Log.d(TAG, "Connected to GATT server")
                _connectionState.value = ConnectionState.CONNECTED
                gatt.discoverServices()
            } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                Log.d(TAG, "Disconnected from GATT server (status: $status)")
                _connectionState.value = ConnectionState.DISCONNECTED
                synchronized(writeLock) {
                    bluetoothGatt = null
                    writeCharacteristic = null
                    notifyCharacteristic = null
                    pendingWriteCallback?.invoke(false)
                    pendingWriteCallback = null
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                synchronized(writeLock) {
                    writeCharacteristic = service?.getCharacteristic(CHAR_WRITE_UUID)
                    notifyCharacteristic = service?.getCharacteristic(CHAR_NOTIFY_UUID)
                }

                notifyCharacteristic?.let { char ->
                    gatt.setCharacteristicNotification(char, true)
                    val descriptor = char.getDescriptor(DESCRIPTOR_UUID)
                    descriptor?.let {
                        it.value = android.bluetooth.BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                        gatt.writeDescriptor(it)
                    }
                }
                Log.d(TAG, "Services discovered and configured")
            } else {
                Log.e(TAG, "Service discovery failed: $status")
                _connectionState.value = ConnectionState.ERROR
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid == CHAR_NOTIFY_UUID) {
                val data = characteristic.value
                logBle("RECEIVE", dataToString(data))
                handleNotification(data)
            }
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            val success = status == BluetoothGatt.GATT_SUCCESS
            if (!success) {
                Log.e(TAG, "Characteristic write failed: $status")
            }
            synchronized(writeLock) {
                pendingWriteCallback?.invoke(success)
                pendingWriteCallback = null
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
        synchronized(writeLock) {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
            bluetoothGatt = null
            writeCharacteristic = null
            notifyCharacteristic = null
            pendingWriteCallback?.invoke(false)
            pendingWriteCallback = null
        }
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    suspend fun sendData(data: ByteArray): Result<ByteArray> {
        return sendDataAndAwait(data)
    }

    private suspend fun sendDataAndAwait(data: ByteArray): Result<ByteArray> {
        logBle("SEND", dataToString(data))
        return try {
            val gatt: BluetoothGatt
            val characteristic: BluetoothGattCharacteristic
            synchronized(writeLock) {
                gatt = bluetoothGatt ?: return Result.failure(IllegalStateException("Not connected"))
                characteristic = writeCharacteristic
                    ?: return Result.failure(IllegalStateException("Write characteristic not available"))
            }

            val writeResult = withTimeoutOrNull(WRITE_TIMEOUT_MS) {
                suspendCancellableCoroutine<Boolean> { cont ->
                    synchronized(writeLock) {
                        pendingWriteCallback?.invoke(false)
                        pendingWriteCallback = { success ->
                            if (cont.isActive) cont.resume(success)
                        }
                    }
                    cont.invokeOnCancellation {
                        synchronized(writeLock) {
                            pendingWriteCallback = null
                        }
                    }
                    characteristic.value = data
                    if (!gatt.writeCharacteristic(characteristic)) {
                        synchronized(writeLock) {
                            pendingWriteCallback = null
                        }
                        if (cont.isActive) cont.resume(false)
                    }
                }
            }

            when {
                writeResult == null -> Result.failure(Exception("BLE write timed out"))
                !writeResult -> Result.failure(Exception("BLE write failed"))
                else -> Result.success(data)
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    private fun dataToString(data: ByteArray): String {
        return try {
            String(data, Charsets.UTF_8)
        } catch (e: Exception) {
            data.joinToString(" ") { String.format("%02X", it) }
        }
    }

    @SuppressLint("MissingPermission")
    override suspend fun getDeviceStatus(): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x01))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getCurrentState(): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x02))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getTodaySummary(): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x03))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getLastReport(): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x04))
    }

    @SuppressLint("MissingPermission")
    override suspend fun sessionControl(action: String, targetDurationMin: Int?): Result<ByteArray> {
        val actionByte = when (action) {
            "start" -> 0x01.toByte()
            "pause" -> 0x02.toByte()
            "resume" -> 0x03.toByte()
            "stop" -> 0x04.toByte()
            else -> 0x00.toByte()
        }
        val durationBytes = targetDurationMin?.let {
            byteArrayOf((it shr 8).toByte(), it.toByte())
        } ?: byteArrayOf()
        return sendDataAndAwait(byteArrayOf(0x05, actionByte) + durationBytes)
    }

    @SuppressLint("MissingPermission")
    override suspend fun updatePreferences(key: String, value: String): Result<ByteArray> {
        val keyBytes = key.toByteArray()
        val valueBytes = value.toByteArray()
        return sendDataAndAwait(byteArrayOf(0x06, keyBytes.size.toByte()) + keyBytes + valueBytes)
    }

    @SuppressLint("MissingPermission")
    override suspend fun reminderAck(alertId: String): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x07) + alertId.toByteArray())
    }

    @SuppressLint("MissingPermission")
    override suspend fun phoneSync(usage: PhoneUsage): Result<ByteArray> {
        // 构建JSON格式的phone_sync数据
        val json = buildString {
            append("{")
            append("\"cmd\":\"phone_sync\",")
            append("\"data\":{")
            append("\"is_in_use\":${usage.isInUse},")
            usage.appCategory?.let { append("\"app_category\":\"$it\",") }
            append("\"distraction_level\":${usage.distractionLevel},")
            usage.appName?.let { append("\"app_name\":\"$it\",") }
            usage.durationSec?.let { append("\"duration_sec\":$it") }
            // 移除末尾的逗号
            if (endsWith(",")) deleteCharAt(length - 1)
            append("}}")
        }
        return sendDataAndAwait(json.toByteArray())
    }

    @SuppressLint("MissingPermission")
    override suspend fun getConsoleData(): Result<ByteArray> {
        return sendDataAndAwait(byteArrayOf(0x09))
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
            0x04.toByte() -> {
                // push_timer_complete
                val timerComplete = parseTimerComplete(data)
                pushEventHandler?.onPushTimerComplete(timerComplete)
            }
        }
    }

    private fun parseTimerComplete(data: ByteArray): TimerComplete {
        // 解析 timer_complete 数据
        // 格式: [0x04, elapsed_ms(4bytes), target_duration_ms(4bytes), distraction_count(2bytes), reminder_count(2bytes)]
        return if (data.size >= 11) {
            val elapsedMs = ((data[1].toInt() and 0xFF) shl 24) or
                    ((data[2].toInt() and 0xFF) shl 16) or
                    ((data[3].toInt() and 0xFF) shl 8) or
                    (data[4].toInt() and 0xFF)
            val targetDurationMs = ((data[5].toInt() and 0xFF) shl 24) or
                    ((data[6].toInt() and 0xFF) shl 16) or
                    ((data[7].toInt() and 0xFF) shl 8) or
                    (data[8].toInt() and 0xFF)
            val distractionCount = ((data[9].toInt() and 0xFF) shl 8) or
                    (data[10].toInt() and 0xFF)
            val reminderCount = if (data.size >= 13) {
                ((data[11].toInt() and 0xFF) shl 8) or (data[12].toInt() and 0xFF)
            } else 0
            TimerComplete(elapsedMs.toLong(), targetDurationMs.toLong(), distractionCount, reminderCount)
        } else {
            TimerComplete(0, 0, 0, 0)
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
}
