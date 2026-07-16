package com.edgefocus.app.data.remote

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.util.Log
import com.edgefocus.app.BuildConfig
import com.edgefocus.app.data.ble.BleRequestScheduler
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.ble.HostRequestPriority
import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.domain.model.DeviceStatus
import com.edgefocus.app.domain.model.FocusState
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.CopyOnWriteArrayList
import javax.inject.Inject
import javax.inject.Singleton
import androidx.annotation.RequiresApi
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
        private const val BLUETOOTH_STATUS_SUCCESS = 0

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
    private var pendingConnectionAddress: String? = null
    private var connectionAttemptGeneration = 0L
    @Volatile
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile
    private var notifyCharacteristic: BluetoothGattCharacteristic? = null
    @Volatile
    private var negotiatedMtu: Int = 23

    private val writeLock = Any()
    private val requestScheduler = BleRequestScheduler()
    private var pendingWriteCallback: ((Boolean) -> Unit)? = null
    private val nextRequestId = AtomicInteger(1)
    private val pendingResponseCallbacks = mutableMapOf<Int, (Result<ByteArray>) -> Unit>()
    private val chunkAssembler = BleChunkAssembler()

    private val _connectionState = MutableStateFlow(ConnectionState.DISCONNECTED)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _deviceStatus = MutableStateFlow<DeviceStatus?>(null)
    val deviceStatus: StateFlow<DeviceStatus?> = _deviceStatus.asStateFlow()

    private val pushEventHandlers = CopyOnWriteArrayList<PushEventHandler>()
    private val reliablePushEventHandlers = CopyOnWriteArrayList<ReliablePushEventHandler>()
    private var bleLogListener: ((String, String) -> Unit)? = null

    data class DiscoveredDevice(val name: String, val address: String)
    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    private var targetMacAddress: String = ""
    @Volatile
    private var autoConnectStoredTargetDuringScan = true
    @Volatile
    private var autoReconnectEnabled = true
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
        if (BuildConfig.DEBUG) {
            simulatedConnected = prefs.getBoolean("simulate_ble", false)
            simulateDeviceResponse = prefs.getBoolean("simulate_device_response", false)
            simulateAutoCountdown = prefs.getBoolean("simulate_auto_countdown", false)
            simulateBoardInSession = prefs.getBoolean("simulate_board_in_session", false)
        } else {
            prefs.edit()
                .remove("simulate_ble")
                .remove("simulate_device_response")
                .remove("simulate_auto_countdown")
                .remove("simulate_board_in_session")
                .apply()
            simulatedConnected = false
            simulateDeviceResponse = false
            simulateAutoCountdown = false
            simulateBoardInSession = false
        }
        simSessionActive = simulateBoardInSession
        simDurationMs = if (BuildConfig.DEBUG) prefs.getInt("simulate_duration", 25) * 60 * 1000L else 25 * 60 * 1000L
        targetMacAddress = ""
    }

    fun setSimulatedConnected(value: Boolean) {
        if (!BuildConfig.DEBUG) {
            simulatedConnected = false
            _connectionState.value = ConnectionState.DISCONNECTED
            return
        }
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
        simulateDeviceResponse = BuildConfig.DEBUG && value
    }

    fun setSimulateAutoCountdown(value: Boolean) {
        simulateAutoCountdown = BuildConfig.DEBUG && value
    }

    fun setSimulateBoardInSession(value: Boolean) {
        simulateBoardInSession = BuildConfig.DEBUG && value
        simSessionActive = simulateBoardInSession
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

    fun addReliablePushEventHandler(handler: ReliablePushEventHandler) {
        reliablePushEventHandlers.addIfAbsent(handler)
    }

    fun removeReliablePushEventHandler(handler: ReliablePushEventHandler) {
        reliablePushEventHandlers.remove(handler)
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

            val scanDecision = BleScanFilter.decide(
                deviceName = deviceName,
                deviceAddress = deviceAddress,
                targetMacAddress = targetMacAddress,
                allowAutoConnectTarget = autoConnectStoredTargetDuringScan
            )

            if (scanDecision.shouldAutoConnectTarget) {
                Log.d(TAG, "Found target MAC: $deviceAddress")
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(this)
                val attemptGeneration = synchronized(writeLock) {
                    pendingConnectionAddress = deviceAddress
                    _connectionState.value = ConnectionState.CONNECTING
                    ++connectionAttemptGeneration
                }
                connectToDevice(result.device, attemptGeneration)
                return
            }

            if (!scanDecision.shouldShowInDiscoveredList) {
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
            if (!isCurrentGatt(gatt)) {
                Log.d(TAG, "Ignoring callback from stale GATT device")
                gatt.close()
                return
            }
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
                requestMtuOrDiscoverServices(gatt)
            } else if (newState == BluetoothGatt.STATE_DISCONNECTED) {
                Log.d(TAG, "Disconnected from GATT server (status: $status)")
                val previousState = _connectionState.value
                _connectionState.value = if (BleGattConnectionPolicy.shouldKeepErrorOnDisconnected(
                        previousState = previousState,
                        gattStatusSuccessful = status == BluetoothGatt.GATT_SUCCESS
                    )
                ) {
                    ConnectionState.ERROR
                } else {
                    ConnectionState.DISCONNECTED
                }
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
                    chunkAssembler.clear()
                }
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            if (!isCurrentGatt(gatt)) return
            Log.d(TAG, "MTU changed: mtu=$mtu, status=$status")
            if (status == BLUETOOTH_STATUS_SUCCESS && mtu > 0) {
                negotiatedMtu = mtu
            }
            discoverServices(gatt)
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (!isCurrentGatt(gatt)) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                val service = gatt.getService(SERVICE_UUID)
                synchronized(writeLock) {
                    writeCharacteristic = service?.getCharacteristic(CHAR_WRITE_UUID)
                    notifyCharacteristic = service?.getCharacteristic(CHAR_NOTIFY_UUID)
                }

                if (writeCharacteristic == null || notifyCharacteristic == null) {
                    failGattInitialization(gatt, "Required BLE characteristics not found")
                    return
                }

                notifyCharacteristic?.let { char ->
                    if (!gatt.setCharacteristicNotification(char, true)) {
                        failGattInitialization(gatt, "Failed to enable local characteristic notification")
                        return
                    }
                    val descriptor = char.getDescriptor(DESCRIPTOR_UUID)
                    if (descriptor == null) {
                        failGattInitialization(gatt, "Notify descriptor not found")
                        return
                    }
                    if (!writeNotifyDescriptor(gatt, descriptor)) {
                        failGattInitialization(gatt, "Failed to write notify descriptor")
                        return
                    }
                }
                Log.d(TAG, "Services discovered, enabling notifications")
            } else {
                failGattInitialization(gatt, "Service discovery failed: $status")
            }
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: android.bluetooth.BluetoothGattDescriptor,
            status: Int
        ) {
            if (!isCurrentGatt(gatt)) return
            if (descriptor.uuid == DESCRIPTOR_UUID && status == BluetoothGatt.GATT_SUCCESS) {
                Log.d(TAG, "Notifications enabled")
                _connectionState.value = ConnectionState.CONNECTED
            } else {
                failGattInitialization(gatt, "Descriptor write failed: $status")
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (!isCurrentGatt(gatt)) return
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
            if (!isCurrentGatt(gatt)) return
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
    fun startScan(autoConnectStoredTarget: Boolean = true) {
        if (bluetoothAdapter?.isEnabled != true) {
            Log.e(TAG, "Bluetooth is not enabled")
            _connectionState.value = ConnectionState.ERROR
            return
        }
        val scanner = bluetoothAdapter.bluetoothLeScanner
        if (scanner == null) {
            Log.e(TAG, "Bluetooth LE scanner is not available")
            _connectionState.value = ConnectionState.ERROR
            return
        }
        try {
            autoReconnectEnabled = true
            autoConnectStoredTargetDuringScan = autoConnectStoredTarget
            _connectionState.value = ConnectionState.SCANNING
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()
            scanner.startScan(emptyList<ScanFilter>(), settings, scanCallback)
        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth scan permission denied: ${e.message}", e)
            _connectionState.value = ConnectionState.ERROR
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Bluetooth scan could not be started: ${e.message}", e)
            _connectionState.value = ConnectionState.ERROR
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        try {
            bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth scan stop permission denied: ${e.message}", e)
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Bluetooth scan could not be stopped: ${e.message}", e)
        }
        if (_connectionState.value == ConnectionState.SCANNING) {
            _connectionState.value = ConnectionState.DISCONNECTED
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice, attemptGeneration: Long) {
        val previousGatt = synchronized(writeLock) {
            if (attemptGeneration != connectionAttemptGeneration) return
            val gatt = bluetoothGatt
            bluetoothGatt = null
            writeCharacteristic = null
            notifyCharacteristic = null
            pendingWriteCallback?.invoke(false)
            pendingWriteCallback = null
            pendingResponseCallbacks.values.forEach {
                it(Result.failure(Exception("BLE reconnecting")))
            }
            pendingResponseCallbacks.clear()
            chunkAssembler.clear()
            gatt
        }
        previousGatt?.let { gatt ->
            try {
                gatt.disconnect()
            } catch (e: SecurityException) {
                Log.e(TAG, "Bluetooth disconnect permission denied before reconnect: ${e.message}", e)
            } catch (e: IllegalStateException) {
                Log.e(TAG, "Bluetooth disconnect failed before reconnect: ${e.message}", e)
            }
            gatt.close()
        }

        val gatt = try {
            if (BleGattConnectionPolicy.shouldUseExplicitLeTransport(Build.VERSION.SDK_INT)) {
                device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
            } else {
                device.connectGatt(context, false, gattCallback)
            }
        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth connect permission denied: ${e.message}", e)
            null
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Invalid Bluetooth device for connection: ${e.message}", e)
            null
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Bluetooth connection could not be started: ${e.message}", e)
            null
        }

        if (gatt == null) {
            synchronized(writeLock) {
                if (attemptGeneration != connectionAttemptGeneration) {
                    return
                } else {
                    pendingConnectionAddress = null
                    _connectionState.value = ConnectionState.ERROR
                }
            }
            return
        }

        val accepted = synchronized(writeLock) {
            if (attemptGeneration == connectionAttemptGeneration &&
                _connectionState.value == ConnectionState.CONNECTING
            ) {
                bluetoothGatt = gatt
                pendingConnectionAddress = null
                true
            } else {
                false
            }
        }
        if (!accepted) {
            try {
                gatt.disconnect()
            } catch (_: SecurityException) {
            } catch (_: IllegalStateException) {
            }
            gatt.close()
        }
    }

    private fun isCurrentGatt(gatt: BluetoothGatt): Boolean {
        return synchronized(writeLock) {
            bluetoothGatt == gatt
        }
    }

    @SuppressLint("MissingPermission")
    private fun requestMtuOrDiscoverServices(gatt: BluetoothGatt) {
        try {
            if (!gatt.requestMtu(REQUESTED_MTU)) {
                discoverServices(gatt)
            }
        } catch (e: SecurityException) {
            failGattInitialization(gatt, "Bluetooth permission denied while requesting MTU: ${e.message}")
        } catch (e: IllegalStateException) {
            failGattInitialization(gatt, "Failed to request MTU: ${e.message}")
        }
    }

    @SuppressLint("MissingPermission")
    private fun discoverServices(gatt: BluetoothGatt) {
        try {
            if (!gatt.discoverServices()) {
                failGattInitialization(gatt, "Failed to start BLE service discovery")
            }
        } catch (e: SecurityException) {
            failGattInitialization(gatt, "Bluetooth permission denied while discovering services: ${e.message}")
        } catch (e: IllegalStateException) {
            failGattInitialization(gatt, "Failed to discover BLE services: ${e.message}")
        }
    }

    @SuppressLint("MissingPermission")
    private fun writeNotifyDescriptor(
        gatt: BluetoothGatt,
        descriptor: BluetoothGattDescriptor
    ): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            writeNotifyDescriptorApi33(gatt, descriptor)
        } else {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    @SuppressLint("MissingPermission")
    private fun writeNotifyDescriptorApi33(
        gatt: BluetoothGatt,
        descriptor: BluetoothGattDescriptor
    ): Boolean = gatt.writeDescriptor(
        descriptor,
        BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
    ) == BluetoothStatusCodes.SUCCESS

    @SuppressLint("MissingPermission")
    private fun failGattInitialization(gatt: BluetoothGatt, message: String) {
        Log.e(TAG, message)
        _connectionState.value = ConnectionState.ERROR
        try {
            gatt.disconnect()
        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth disconnect permission denied after failure: ${e.message}", e)
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Bluetooth disconnect failed after initialization error: ${e.message}", e)
        }
        gatt.close()
        synchronized(writeLock) {
            if (bluetoothGatt == gatt) {
                bluetoothGatt = null
                writeCharacteristic = null
                notifyCharacteristic = null
            }
            pendingWriteCallback?.invoke(false)
            pendingWriteCallback = null
            pendingResponseCallbacks.values.forEach {
                it(Result.failure(Exception(message)))
            }
            pendingResponseCallbacks.clear()
            chunkAssembler.clear()
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        autoReconnectEnabled = false
        synchronized(writeLock) {
            ++connectionAttemptGeneration
            pendingConnectionAddress = null
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
            chunkAssembler.clear()
        }
        _connectionState.value = ConnectionState.DISCONNECTED
    }

    fun isConnected(): Boolean {
        if (BuildConfig.DEBUG && simulatedConnected) return true
        if (_connectionState.value != ConnectionState.CONNECTED) return false
        if (targetMacAddress.isEmpty()) return bluetoothGatt != null
        return bluetoothGatt?.device?.address == targetMacAddress
    }

    fun isSimulating(): Boolean {
        return BuildConfig.DEBUG && simulatedConnected
    }

    suspend fun sendData(data: ByteArray): Result<ByteArray> {
        return sendDataAndAwait(data, HostRequestPriority.P3_BACKGROUND)
    }

    @SuppressLint("MissingPermission")
    override suspend fun getDeviceStatus(): Result<ByteArray> {
        val json = """{"cmd":"get_device_status"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8), HostRequestPriority.P2_STATE)
    }

    private suspend fun sendDataAndAwait(
        data: ByteArray,
        priority: HostRequestPriority = HostRequestPriority.P3_BACKGROUND
    ): Result<ByteArray> {
        return requestScheduler.execute(priority) {
            sendDataAndAwaitLocked(data)
        }
    }

    @SuppressLint("MissingPermission")
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
        BleWritePolicy.validateRequestSize(requestBytes.size, negotiatedMtu)?.let { error ->
            logBle("ERROR", error)
            return Result.failure(IllegalArgumentException(error))
        }

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
        BleWritePolicy.validateRequestSize(data.size, negotiatedMtu)?.let { error ->
            logBle("ERROR", error)
            return Result.failure(IllegalArgumentException(error))
        }
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

    fun shouldAutoReconnect(): Boolean = autoReconnectEnabled && targetMacAddress.isNotBlank()

    fun requestReconnect(): Boolean {
        val target = targetMacAddress.takeIf { it.isNotBlank() } ?: return false
        autoReconnectEnabled = true
        connectToDiscoveredDevice(target)
        return true
    }

    @SuppressLint("MissingPermission")
    fun cancelStaleConnectionAttempt(): Boolean {
        if (_connectionState.value != ConnectionState.CONNECTING) return false
        val (staleGatt, timeoutGeneration) = synchronized(writeLock) {
            val generation = ++connectionAttemptGeneration
            val gatt = bluetoothGatt
            bluetoothGatt = null
            pendingConnectionAddress = null
            writeCharacteristic = null
            notifyCharacteristic = null
            gatt to generation
        }
        try {
            staleGatt?.disconnect()
        } catch (e: SecurityException) {
            Log.w(TAG, "Bluetooth permission denied while timing out connection: ${e.message}")
        } catch (e: IllegalStateException) {
            Log.w(TAG, "Stale Bluetooth connection could not be disconnected: ${e.message}")
        }
        staleGatt?.close()
        return synchronized(writeLock) {
            if (connectionAttemptGeneration == timeoutGeneration &&
                _connectionState.value == ConnectionState.CONNECTING
            ) {
                _connectionState.value = ConnectionState.ERROR
                true
            } else {
                false
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun connectToDiscoveredDevice(address: String) {
        val adapter = bluetoothAdapter
        if (adapter?.isEnabled != true) {
            Log.e(TAG, "Bluetooth is not enabled")
            _connectionState.value = ConnectionState.ERROR
            return
        }
        targetMacAddress = address
        autoReconnectEnabled = true
        val device = try {
            adapter.getRemoteDevice(address)
        } catch (e: IllegalArgumentException) {
            Log.e(TAG, "Invalid Bluetooth address: $address", e)
            _connectionState.value = ConnectionState.ERROR
            return
        }
        var attemptGeneration = 0L
        val shouldSkip = synchronized(writeLock) {
            val currentAddress = bluetoothGatt?.device?.address ?: pendingConnectionAddress
            if (BleGattConnectionPolicy.shouldSkipConnectionRequest(
                    currentState = _connectionState.value,
                    currentGattAddress = currentAddress,
                    requestedAddress = address
                )
            ) {
                true
            } else {
                pendingConnectionAddress = address
                _connectionState.value = ConnectionState.CONNECTING
                attemptGeneration = ++connectionAttemptGeneration
                false
            }
        }
        if (shouldSkip) return
        try {
            adapter.bluetoothLeScanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            Log.e(TAG, "Bluetooth scan stop permission denied before connect: ${e.message}", e)
        } catch (e: IllegalStateException) {
            Log.e(TAG, "Bluetooth scan could not be stopped before connect: ${e.message}", e)
        }
        connectToDevice(device, attemptGeneration)
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
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8), HostRequestPriority.P2_STATE)
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
                """{"status":"success","data":{"system_mode":0,"notification_mode":"normal","power_profile":"go","away_timeout_min":15,"default_focus_duration_min":25,"sound_volume":0.7,"normal_channels":["screen","band","sound"],"silent_channels":["screen","band"],"screen_output_route":"board","sound_output_route":"board","gimbal_enabled":true}}"""
                    .toByteArray(Charsets.UTF_8)
            )
        }
        val json = """{"cmd":"get_preferences"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getCameraPreviewImage(): Result<ByteArray> {
        val json = BoardJsonParser.buildCommand("get_camera_preview_image").toString()
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8), HostRequestPriority.P4_PREVIEW)
    }

    @SuppressLint("MissingPermission")
    override suspend fun getGimbalStatus(): Result<ByteArray> {
        val json = BoardJsonParser.buildCommand("get_gimbal_status").toString()
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
        val data = BoardJsonParser.buildSessionControl(actionName, targetDurationMin).toString()
        return sendDataAndAwait(data.toByteArray(Charsets.UTF_8), HostRequestPriority.P0_CRITICAL)
    }

    @SuppressLint("MissingPermission")
    override suspend fun updatePreferences(key: String, value: String): Result<ByteArray> {
        val typedValue: Any = when (key) {
            "system_mode", "away_timeout_min", "default_focus_duration_min" ->
                value.toIntOrNull() ?: value
            "sound_volume" ->
                value.toFloatOrNull() ?: value
            "gimbal_enabled" -> value.toBooleanStrictOrNull() ?: value
            else -> value
        }
        return updatePreferences(mapOf(key to typedValue))
    }

    @SuppressLint("MissingPermission")
    override suspend fun updatePreferences(preferences: Map<String, Any?>): Result<ByteArray> {
        val root = BoardJsonParser.buildUpdatePreferences(preferences)
        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun gimbalControl(
        action: String,
        xDeg: Number?,
        yDeg: Number?,
        speed: Int?,
        stepDeg: Int?,
        debug: Boolean?
    ): Result<ByteArray> {
        val root = BoardJsonParser.buildGimbalControl(action, xDeg, yDeg, speed, stepDeg, debug)
        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun reminderAck(alertId: String): Result<ByteArray> {
        val json = """{"cmd":"reminder_ack"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
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
        val root = BoardJsonParser.buildPhoneSync(usage)

        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P1_PHONE_HEARTBEAT
        )
    }

    @SuppressLint("MissingPermission")
    override suspend fun uploadLocalSession(session: LocalSessionUpload): Result<ByteArray> {
        val root = BoardJsonParser.buildUploadLocalSession(session)

        return sendDataAndAwait(root.toString().toByteArray(Charsets.UTF_8))
    }

    @SuppressLint("MissingPermission")
    override suspend fun getConsoleData(): Result<ByteArray> {
        val json = """{"cmd":"get_console_data"}"""
        return sendDataAndAwait(json.toByteArray(Charsets.UTF_8))
    }

    override suspend fun clientHello(): Result<ByteArray> {
        val root = HostJsonCodec.clientHello(nextRequestId.getAndIncrement())
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P2_STATE
        )
    }

    override suspend fun eventAck(
        eventId: String,
        sessionId: String,
        stage: String
    ): Result<ByteArray> {
        val ackStage = EventAckStage.entries.firstOrNull { it.wireValue == stage.lowercase() }
            ?: return Result.failure(IllegalArgumentException("Unknown ACK stage: $stage"))
        val root = HostJsonCodec.eventAck(
            requestId = nextRequestId.getAndIncrement(),
            eventId = eventId,
            sessionId = sessionId,
            stage = ackStage
        )
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P0_CRITICAL
        )
    }

    override suspend fun gimbalPrecheckCommand(
        command: String,
        sessionId: String,
        sequence: Long,
        direction: String?
    ): Result<ByteArray> {
        val root = HostJsonCodec.gimbalCommand(
            requestId = nextRequestId.getAndIncrement(),
            command = command,
            sessionId = sessionId,
            sequence = sequence,
            direction = direction
        )
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P0_CRITICAL
        )
    }

    override suspend fun getBandStatus(): Result<ByteArray> {
        val root = HostJsonCodec.getCurrentState(nextRequestId.getAndIncrement())
        root.put("cmd", "get_band_status")
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P2_STATE
        )
    }

    override suspend fun bandControl(action: String): Result<ByteArray> {
        if (action != "scan_connect" && action != "reconnect") {
            return Result.failure(IllegalArgumentException("Unknown band action: $action"))
        }
        val root = org.json.JSONObject()
            .put("cmd", "band_control")
            .put("id", nextRequestId.getAndIncrement())
            .put("data", org.json.JSONObject().put("action", action))
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P2_STATE
        )
    }

    override suspend fun setPowerProfile(profile: String): Result<ByteArray> {
        val root = HostJsonCodec.setPowerProfile(nextRequestId.getAndIncrement(), profile)
        return sendDataAndAwait(
            root.toString().toByteArray(Charsets.UTF_8),
            HostRequestPriority.P2_STATE
        )
    }

    override suspend fun cancelPreviewRequests() {
        requestScheduler.cancelQueued(HostRequestPriority.P4_PREVIEW, "PRECHECK preview stopped")
    }

    private fun handleNotification(data: ByteArray) {
        if (data.isEmpty()) return

        val jsonStr = dataToString(data)
        logBle("RECV", jsonStr)

        try {
            val json = org.json.JSONObject(jsonStr)
            val assembled = chunkAssembler.accept(json)
            if (assembled != null) {
                logBle("RECV_ASSEMBLED", assembled.payload)
                handleNotification(assembled.payload.toByteArray(Charsets.UTF_8))
                return
            }
            if (json.optString("status") == "chunk" || json.optString("cmd") == "push_chunk") {
                return
            }
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
            val reliableEvent = HostJsonCodec.parseReliableEvent(jsonStr)
            if (reliableEvent != null) {
                reliablePushEventHandlers.forEach { it.onReliableEvent(reliableEvent) }
                return
            }

            when {
                cmd == "push_state" && eventData != null -> {
                    val snapshot = BoardJsonParser.parseSessionState(eventData)
                    pushEventHandlers.forEach { it.onPushStateSnapshot(snapshot) }
                    if (!eventData.has("session_phase")) {
                        val stateInt = eventData.optInt("focus_state", 0)
                        val state = when (stateInt) {
                            6 -> com.edgefocus.app.domain.model.TimerState.PAUSED
                            7 -> com.edgefocus.app.domain.model.TimerState.PRECHECK
                            in 0..5, 8, 9 -> com.edgefocus.app.domain.model.TimerState.RUNNING
                            else -> com.edgefocus.app.domain.model.TimerState.IDLE
                        }
                        val focusState = FocusState(state = state)
                        pushEventHandlers.forEach { it.onPushState(focusState) }
                    }
                }
                cmd == "push_session_end" && eventData != null -> {
                    val summary = eventData.toString()
                    pushEventHandlers.forEach { it.onPushSessionEnd(summary) }
                }
                cmd == "push_timer_complete" && eventData != null -> {
                    val timerComplete = BoardJsonParser.parseTimerComplete(eventData)
                    pushEventHandlers.forEach { it.onPushTimerComplete(timerComplete) }
                }
                cmd == "push_alert" && eventData != null -> {
                    val alert = BoardJsonParser.parsePushAlert(eventData)
                    pushEventHandlers.forEach { it.onPushAlert(alert) }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse push event: ${e.message}")
        }
    }
}
