package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.ConnectionState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

data class InstalledApp(
    val packageName: String,
    val appName: String,
    val isDistraction: Boolean
)

data class BleLog(
    val timestamp: Long = System.currentTimeMillis(),
    val direction: String = "UNKNOWN", // "SEND" or "RECEIVE"
    val content: String
)

data class TestUiState(
    val simulateBleConnected: Boolean = false,
    val bleLogs: List<BleLog> = emptyList(),
    val installedApps: List<InstalledApp> = emptyList(),
    val distractionApps: Set<String> = emptySet()
)

@HiltViewModel
class TestViewModel @Inject constructor(
    application: Application,
    private val bleManager: BleManager
) : AndroidViewModel(application) {

    private val _uiState = MutableStateFlow(TestUiState())
    val uiState: StateFlow<TestUiState> = _uiState.asStateFlow()

    private val prefs = application.getSharedPreferences("test_prefs", 0)

    init {
        // 恢复模拟状态
        val simulateBle = prefs.getBoolean("simulate_ble", false)
        _uiState.value = _uiState.value.copy(simulateBleConnected = simulateBle)

        loadInstalledApps()
        loadDistractionApps()
        loadBleLogs()

        // 连接 BLE 日志监听
        bleManager.setBleLogListener { direction, content ->
            addBleLog(direction, content)
        }
    }

    fun toggleSimulateBle() {
        val newValue = !_uiState.value.simulateBleConnected
        _uiState.value = _uiState.value.copy(simulateBleConnected = newValue)
        prefs.edit().putBoolean("simulate_ble", newValue).apply()
    }

    fun addBleLog(direction: String, content: String) {
        val log = BleLog(direction = direction, content = content)
        val currentLogs = _uiState.value.bleLogs.toMutableList()
        currentLogs.add(0, log) // 添加到开头
        if (currentLogs.size > 100) {
            currentLogs.removeAt(currentLogs.size - 1)
        }
        _uiState.value = _uiState.value.copy(bleLogs = currentLogs)
        saveBleLogs(currentLogs)
    }

    fun clearBleLogs() {
        _uiState.value = _uiState.value.copy(bleLogs = emptyList())
        prefs.edit().remove("ble_logs").apply()
    }

    fun toggleDistractionApp(packageName: String) {
        val current = _uiState.value.distractionApps.toMutableSet()
        if (current.contains(packageName)) {
            current.remove(packageName)
        } else {
            current.add(packageName)
        }
        _uiState.value = _uiState.value.copy(distractionApps = current)
        saveDistractionApps(current)
    }

    fun isSimulatingBle(): Boolean {
        return _uiState.value.simulateBleConnected
    }

    private fun loadInstalledApps() {
        viewModelScope.launch {
            val pm = getApplication<Application>().packageManager
            val apps = pm.getInstalledApplications(PackageManager.GET_META_DATA)
                .filter { app ->
                    // 过滤掉系统应用，只显示用户安装的应用
                    (app.flags and ApplicationInfo.FLAG_SYSTEM) == 0 ||
                    pm.getLaunchIntentForPackage(app.packageName) != null
                }
                .map { app ->
                    InstalledApp(
                        packageName = app.packageName,
                        appName = pm.getApplicationLabel(app).toString(),
                        isDistraction = false
                    )
                }
                .sortedBy { it.appName.lowercase() }

            _uiState.value = _uiState.value.copy(installedApps = apps)
        }
    }

    private fun loadDistractionApps() {
        val saved = prefs.getStringSet("distraction_apps", emptySet()) ?: emptySet()
        _uiState.value = _uiState.value.copy(distractionApps = saved)

        // 更新 installedApps 的 isDistraction 状态
        updateAppsDistractionStatus(saved)
    }

    private fun saveDistractionApps(apps: Set<String>) {
        prefs.edit().putStringSet("distraction_apps", apps).apply()
        updateAppsDistractionStatus(apps)
    }

    private fun updateAppsDistractionStatus(distractionApps: Set<String>) {
        val updatedApps = _uiState.value.installedApps.map { app ->
            app.copy(isDistraction = distractionApps.contains(app.packageName))
        }
        _uiState.value = _uiState.value.copy(installedApps = updatedApps)
    }

    private fun loadBleLogs() {
        val saved = prefs.getString("ble_logs", "") ?: ""
        if (saved.isNotEmpty()) {
            val logs = saved.split("\n").filter { it.isNotEmpty() }.map { line ->
                val parts = line.split("|", limit = 3)
                if (parts.size == 3) {
                    BleLog(
                        timestamp = parts[0].toLongOrNull() ?: 0L,
                        direction = parts[1],
                        content = parts[2]
                    )
                } else {
                    BleLog(content = line)
                }
            }
            _uiState.value = _uiState.value.copy(bleLogs = logs)
        }
    }

    private fun saveBleLogs(logs: List<BleLog>) {
        val serialized = logs.joinToString("\n") { "${it.timestamp}|${it.direction}|${it.content}" }
        prefs.edit().putString("ble_logs", serialized).apply()
    }
}
