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
import com.edgefocus.app.domain.model.DeviceStatus
import com.edgefocus.app.domain.model.FocusState
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.CopyOnWriteArrayList
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
        private const val REQUESTED_MTU = 517
        private val DEVICE_NAME_FILTERS = listOf("EdgeFocus", "ESP32")

        val SERVICE_UUID: UUID = UUID.fromString("0000ff00-0000-1000-8000-00805f9b34fb")
        val CHAR_WRITE_UUID: UUID = UUID.fromString("0000ff01-0000-1000-8000-00805f9b34fb")
        val CHAR_NOTIFY_UUID: UUID = UUID.fromString("0000ff02-0000-1000-8000-00805f9b34fb")
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
    private val commandMutex = Mutex()
    private var pendingWriteCallback: ((Boolean) -> Unit)? = null
    private val nextRequestId = AtomicInteger(1)
    private val pendingResponseCallbacks = mutableMapOf<Int, (Result<ByteArray>) -> Unit>()

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus: StateFlow<DeviceStatus?> = _deviceStatus.asStateFlow()

    private val pushEventHandlers = CopyOnWriteArrayList<PushEventHandler>()
    private var bleLogListener: ((String, String) -> Unit)? = null

    data class DiscoveredDevice(val name: String, val address: String)
    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    private var targetMacAddress: String = ""
    @Volatile
    private var simulatedConnected = false
    @Volatile
    private var simulateDeviceResponse = false
    @Volatile
    private var simulateAutoCountdown = false
    @Volatile
    private var simulateBoardInSession = false
    private var appEventLog: AppEventLog? = null

    private var simStartTime = 0L
    private var simDurationMs = 25 * 60 * 1000L
    @Volatile
    private var simSessionActive = false

    init {
        val prefs = context.getSharedPreferences("test_prefs", 0)
        simulatedConnected = prefs.getBoolean("simulate_ble", false)
        simulateDeviceResponse = prefs.getBoolean("simulate_device_response", false)
        simulateAutoCountdown = prefs.getBoolean("simulate_auto_countdown", false)
        simulateBoardInSession = prefs.getBoolean("simulate_board_in_session", false)
        simSessionActive = simulateBoardInSession
        simDurationMs = prefs.getInt("simulate_duration", 25) * 60 * 1000L
        targetMacAddress = prefs.getString("edge_focus_mac", "") ?: ""
    }

    fun setSimulatedConnected(value: Boolean) {
        simulatedConnected = value
        if (value) {
            _connectionState.value = ConnectionState.CONNECTED
            appEventLog?.log("BLE_CONNECT", "模拟连接已启用")
        } else {
            _connectionState.value = ConnectionState.DISCONNECTED
            appEventLog?.log("BLE_DISCONNECT", "模拟连接已关闭")
        }
    }

    fun setAppEventLog(log: AppEventLog) {
        this.appEventLog = log
    }

    fun setSimulateDeviceResponse(value: Boolean) {
        simulateDeviceResponse = value
    }

    fun setSimulateAutoCountdown(value: Boolean) {
        simulateAutoCountdown = value
    }

    fun setSimulateBoardInSession(value: Boolean) {
        simulateBoardInSession = value
        simSessionActive = value
        if (value) {
            simStartTime = 0L
        }
    }

    fun setSimDuration(minutes: Int) {
        simDurationMs = minutes * 60 * 1000L
    }

    fun setPushEventHandler(handler: PushEventHandler) {
        pushEventHandlers.addIfAbsent(handler)
    }

    fun removePushEventHandler(handler: PushEventHandler) {
        pushEventHandlers.remove(handler)
    }

    fun setBleLogListener(listener: (direction: String, content: String) -> Unit) {
        this.bleLogListener = listener
    }

    fun removeBleLogListener(listener: (direction: String, content: String) -> Unit) {
        if (bleLogListener === listener) {
            bleLogListener = null
        }
    }

    private fun logBle(direction: String, content: String) {
        Log.d(TAG, "BLE $direction: $content")
        bleLogListener?.invoke(direction, content)
    }

    private val scanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            if (_connectionState.value == ConnectionState.CONNECTING ||
                _connectionState.value == ConnectionState.CONNECTED) {
                return
            }

            val deviceName = result.scanRecord?.deviceName ?: result.device.name.orEmpty()
            val deviceAddress = result.device.address

            if (targetMacAddress.isNotEmpty()) {
                if (deviceAddress == targetMacAddress) {
                    Log.d(TAG, "Found target MAC: $deviceAddress")
                    bluetoothAdapter?.bluetoothLeScanner?.stopScan(this)
                    _connectionState.value = ConnectionState.CONNECTING
                    connectToDevice(result.device)
                }
                return
            }

            val hasExpectedName = DEVICE_NAME_FILTERS.any {
                deviceName.contains(it, ignoreCase = true)
            }
            val advertisesEdgeFocusService = result.scanRecord
                ?.serviceUuids
                ?.any { it.uuid == SERVICE_UUID } == true

            // Initial binding requires both the product name and the advertised GATT service.
            if (!hasExpectedName || !advertisesEdgeFocusService) {
                return
            }

            // 添加到发现列表（不自动连接）
            val current = _discoveredDevices.value.toMutableList()
            if (current.none { it.address == deviceAddress }) {
                current.add(DiscoveredDevice(deviceName, deviceAddress))
                _discoveredDevices.value = current
            }
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
                _connectionState.value = ConnectionState.CONNECTING
                if (!gatt.requestMtu(REQUESTED_MTU)) {
                    gatt.discoverServices()
                }
            } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                Log.d(TAG, "Disconnected from GATT server (status: $status)")
                _connectionState.value = ConnectionState.DISCONNECTED
                gatt.close()
                synchronized(writeLock) {
                    bluetoothGatt = null
                    writeCharacteristic = null
                    notifyCharacteristic = null
                    pendingWriteCallback?.invoke(false)
                    pendingWriteCallback = null
                    pendingResponseCallbacks.values.forEach {
                        it(Result.failure(Exception("BLE disconnected")))
                    }
                    pendingResponseCallbacks.clear()
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "MTU changed: mtu=$mtu, status=$status")
            gatt.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                synchronized(writeLock) {
                    writeCharacteristic = service?.getCharacteristic(CHAR_WRITE_UUID)
                    notifyCharacteristic = service?.getCharacteristic(CHAR_NOTIFY_UUID)
                }

                if (writeCharacteristic == null || notifyCharacteristic == null) {
                    Log.e(TAG, "Required BLE characteristics not found")
                    _connectionState.value = ConnectionState.ERROR
                    return
                }

                notifyCharacteristic?.let { char ->
                    gatt.setCharacteristicNotification(char, true)
                    val descriptor = char.getDescriptor(DESCRIPTOR_UUID)
                    if (descriptor == null) {
                        Log.e(TAG, "Notify descriptor not found")
                        _connectionState.value = ConnectionState.ERROR
                        return
                    }
                    descriptor.value = android.bluetooth.BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    if (!gatt.writeDescriptor(descriptor)) {
                        Log.e(TAG, "Failed to write notify descriptor")
                        _connectionState.value = ConnectionState.ERROR
                        return
                    }
                }
                Log.d(TAG, "Services discovered, enabling notifications")
            } else {
                Log.e(TAG, "Service discovery failed: $status")
                _connectionState.value = ConnectionState.ERROR
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: android.bluetooth.BluetoothGattDescriptor,
            status: Int
        ) {
            if (descriptor.uuid == DESCRIPTOR_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Notifications enabled")
                _connectionState.value = ConnectionState.CONNECTED
            } else {
                Log.e(TAG, "Descriptor write failed: $status")
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
            pendingResponseCallbacks.values.forEach {
                it(Result.failure(Exception("BLE disconnected")))
            }
            pendingResponseCallbacks.clear()
        }
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    fun isConnected(): Boolean {
        if (simulatedConnected) return true
        if (_connectionState.value != ConnectionState.CONNECTED) return false
        if (targetMacAddress.isEmpty()) return false
        return bluetoothGatt?.device?.address == targetMacAddress
    }

    fun isSimulating(): Boolean {
        return simulatedConnected
    }

    suspend fun sendData(data: ByteArray): Result<ByteArray> {
        return sendDataAndAwait(data)
    }

    @SuppressLint("MissingPermission")
    override suspend fun getDeviceStatus(): Result<ByteArray> {
        val json = """{"cmd":"get_device_status"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    private suspend fun sendDataAndAwait(data: ByteArray): Result<ByteArray> {
        return commandMutex.withLock {
            sendDataAndAwaitLocked(data)
        }
    }

    private suspend fun sendDataAndAwaitLocked(data: ByteArray): Result<ByteArray> {
        val originalJson = dataToString(data)

        // Simulated mode returns success immediately.
        if (simulatedConnected) {
            logBle("SEND", originalJson)
            return Result.success(data)
        }

        val requestBytes = try {
            val json = org.json.JSONObject(originalJson)
            if (!json.has("id")) {
                json.put("id", nextRequestId.getAndIncrement())
            }
            json.toString().toByteArray(Charsets.UTF_8)
        } catch (e: Exception) {
            logBle("SEND", originalJson)
            return writeDataOnly(data)
        }

        val requestId = org.json.JSONObject(String(requestBytes, Charsets.UTF_8)).optInt("id")
        logBle("SEND", String(requestBytes, Charsets.UTF_8))

        return try {
            val gatt: BluetoothGatt
            val characteristic: BluetoothGattCharacteristic
            synchronized(writeLock) {
                gatt = bluetoothGatt ?: return Result.failure(IllegalStateException("Not connected"))
                characteristic = writeCharacteristic
                    ?: return Result.failure(IllegalStateException("Write characteristic not available"))
            }

            val responseResult = withTimeoutOrNull(WRITE_TIMEOUT_MS) {
                suspendCancellableCoroutine<Result<ByteArray>> { cont ->
                    synchronized(writeLock) {
                        pendingWriteCallback?.invoke(false)
                        pendingWriteCallback = { success ->
                            if (!success) {
                                pendingResponseCallbacks.remove(requestId)
                                if (cont.isActive) {
                                    cont.resume(Result.failure(Exception("BLE write failed")))
                                }
                            }
                        }
                        pendingResponseCallbacks[requestId] = { result ->
                            if (cont.isActive) cont.resume(result)
                        }
                    }
                    cont.invokeOnCancellation {
                        synchronized(writeLock) {
                            pendingWriteCallback = null
                            pendingResponseCallbacks.remove(requestId)
                        }
                    }
                    characteristic.value = requestBytes
                    if (!gatt.writeCharacteristic(characteristic)) {
                        synchronized(writeLock) {
                            pendingWriteCallback = null
                            pendingResponseCallbacks.remove(requestId)
                        }
                        if (cont.isActive) {
                            cont.resume(Result.failure(Exception("BLE write failed")))
                        }
                    }
                }
            }

            responseResult ?: Result.failure(Exception("BLE response timed out"))
        } catch (e: Exception) {
            synchronized(writeLock) {
                pendingResponseCallbacks.remove(requestId)
            }
            Result.failure(e)
        }
    }

    @SuppressLint("MissingPermission")
    private suspend fun writeDataOnly(data: ByteArray): Result<ByteArray> {
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

    fun setTargetMac(mac: String) {
        targetMacAddress = mac
    }

    @SuppressLint("MissingPermission")
    fun connectToDiscoveredDevice(address: String) {
        if (_connectionState.value == ConnectionState.CONNECTING ||
            _connectionState.value == ConnectionState.CONNECTED) {
            return
        }
        val device = bluetoothAdapter?.getRemoteDevice(address) ?: return
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        _connectionState.value = ConnectionState.CONNECTING
        connectToDevice(device)
    }

    fun clearDiscoveredDevices() {
        _discoveredDevices.value = emptyList()
    }

    @SuppressLint("MissingPermission")
    override suspend fun getCurrentState(): Result<ByteArray> {
        if (simulatedConnected && simulateDeviceResponse) {
            logBle("SEND", """{"cmd":"get_current_state"}""")

            val isActive = simSessionActive
            val remaining = if (isActive && simStartTime > 0) {
                val elapsed = System.currentTimeMillis() - simStartTime
                (simDurationMs - elapsed).coerceAtLeast(0)
            } else if (isActive) {
                (simDurationMs * 0.7).toLong()
            } else {
                0L
            }
            val elapsed = if (isActive && simStartTime > 0) {
                (System.currentTimeMillis() - simStartTime).coerceAtMost(simDurationMs)
            } else if (isActive) {
                (simDurationMs * 0.3).toLong()
            } else 0L

            val response = """{"status":"success","data":{"active":${isActive},"focus_state":1,"elapsed_ms":${elapsed},"distraction_count":0,"reminder_count":0,"calibration_completed":true,"remaining_ms":${remaining},"target_duration_ms":${simDurationMs}}}"""
            logBle("RECV", response)
            return Result.success(response.toByteArray())
        }

        val json = """{"cmd":"get_current_state"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getTodaySummary(): Result<ByteArray> {
        val json = """{"cmd":"get_today_summary"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getLastReport(): Result<ByteArray> {
        val json = """{"cmd":"get_last_report"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getSessionHistory(cursor: String?, limit: Int): Result<ByteArray> {
        if (simulatedConnected) {
            return Result.success(
                """{"status":"success","data":{"sessions":[],"has_more":false}}"""
                    .toByteArray(Charsets.UTF_8)
            )
        }
        val root = org.json.JSONObject().apply {
            put("cmd", "get_session_history")
            put("data", org.json.JSONObject().apply {
                cursor?.takeIf { it.isNotBlank() }?.let { put("cursor", it) }
                put("limit", limit.coerceIn(1, 100))
            })
        }
        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getPreferences(): Result<ByteArray> {
        if (simulatedConnected) {
            return Result.success(
                """{"status":"success","data":{"system_mode":0,"away_timeout_min":15,"vibration_strength":0.5,"led_brightness":75,"reminder_sensitivity":0.5}}"""
                    .toByteArray(Charsets.UTF_8)
            )
        }
        val json = """{"cmd":"get_preferences"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun sessionControl(action: String, targetDurationMin: Int?): Result<ByteArray> {
        // 模拟设备回复
        if (simulatedConnected && simulateDeviceResponse) {
            val actionName = when (action) {
                "start" -> "start"
                "pause" -> "pause"
                "resume" -> "resume"
                "stop" -> "end"
                else -> action
            }
            logBle("SEND", """{"cmd":"session_control","data":{"action":"$actionName"}}""")

            if (action == "start") {
                val durationMs = (targetDurationMin ?: 25) * 60 * 1000L
                simDurationMs = durationMs
                simSessionActive = true
                if (simulateAutoCountdown) {
                    simStartTime = System.currentTimeMillis()
                }
                val response = """{"status":"success","data":{"success":true,"remaining_ms":${durationMs},"target_duration_ms":${durationMs}}}"""
                logBle("RECV", response)
                return Result.success(response.toByteArray())
            }

            if (action == "stop") {
                simSessionActive = false
                simStartTime = 0L
            }

            val response = """{"status":"success","data":{"success":true}}"""
            logBle("RECV", response)
            return Result.success(response.toByteArray())
        }

        val actionName = when (action) {
            "start" -> "start"
            "pause" -> "pause"
            "resume" -> "resume"
            "stop" -> "end"
            else -> action
        }
        val data = buildString {
            append("{\"cmd\":\"session_control\",\"data\":{\"action\":\"$actionName\"")
            targetDurationMin?.let { append(",\"target_duration_min\":$it") }
            append("}}")
        }
        return sendDataAndAwait(data.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun updatePreferences(key: String, value: String): Result<ByteArray> {
        val typedValue: Any = when (key) {
            "system_mode", "away_timeout_min", "led_brightness" -> value.toIntOrNull() ?: value
            "vibration_strength", "reminder_sensitivity" -> value.toFloatOrNull() ?: value
            else -> value
        }
        val data = org.json.JSONObject().apply {
            put(key, typedValue)
        }
        val root = org.json.JSONObject().apply {
            put("cmd", "update_preferences")
            put("data", data)
        }
        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun sessionFeedback(sessionId: String, feedback: String): Result<ByteArray> {
        val root = org.json.JSONObject().apply {
            put("cmd", "session_feedback")
            put("data", org.json.JSONObject().apply {
                put("session_id", sessionId)
                put("feedback", feedback)
            })
        }
        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun phoneSync(usage: PhoneUsage): Result<ByteArray> {
        val data = org.json.JSONObject().apply {
            put("is_in_use", usage.isInUse)
            put("app_category", usage.appCategory ?: "other")
            put("distraction_level", usage.distractionLevel)
            put("app_name", usage.appName ?: "")
            usage.durationSec?.let { put("duration_sec", it) }
        }

        val root = org.json.JSONObject().apply {
            put("cmd", "phone_sync")
            put("data", data)
        }

        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getConsoleData(): Result<ByteArray> {
        val json = """{"cmd":"get_console_data"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    private fun handleNotification(data: ByteArray) {
        if (data.isEmpty()) return

        val jsonStr = dataToString(data)
        logBle("RECV", jsonStr)

        try {
            val json = org.json.JSONObject(jsonStr)
            if (json.has("status")) {
                val responseId = json.optInt("id", -1)
                if (responseId >= 0) {
                    val callback = synchronized(writeLock) {
                        pendingWriteCallback = null
                        pendingResponseCallbacks.remove(responseId)
                    }
                    val result = if (json.optString("status") == "error") {
                        Result.failure(Exception(json.optString("error", "Board returned error")))
                    } else {
                        Result.success(data)
                    }
                    callback?.invoke(result)
                } else {
                    Log.w(TAG, "Response without id: $jsonStr")
                }
                return
            }

            val cmd = json.optString("cmd", "")
            val eventData = json.optJSONObject("data")

            when {
                cmd == "push_state" && eventData != null -> {
                    val stateInt = eventData.optInt("focus_state", 0)
                    val state = when (stateInt) {
                        6 -> com.edgefocus.app.domain.model.TimerState.PAUSED
                        in 0..5, 7 -> com.edgefocus.app.domain.model.TimerState.RUNNING
                        else -> com.edgefocus.app.domain.model.TimerState.IDLE
                    }
                    val focusState = FocusState(state = state)
                    pushEventHandlers.forEach { it.onPushState(focusState) }
                }
                cmd == "push_session_end" && eventData != null -> {
                    val summary = eventData.toString()
                    pushEventHandlers.forEach { it.onPushSessionEnd(summary) }
                }
                cmd == "push_timer_complete" && eventData != null -> {
                    val timerComplete = TimerComplete(
                        elapsedMs = eventData.optLong("elapsed_ms", 0),
                        targetDurationMs = eventData.optLong("target_duration_ms", 0),
                        distractionCount = eventData.optInt("distraction_count", 0),
                        reminderCount = eventData.optInt("reminder_count", 0)
                    )
                    pushEventHandlers.forEach { it.onPushTimerComplete(timerComplete) }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse push event: ${e.message}")
        }
    }
}
