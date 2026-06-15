package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.content.pm.PackageManager
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.remote.BleManager
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

enum class DistractionLevel(val value: Int, val label: String, val description: String) {
    LEARNING(0, "学习软件", "不提醒"),
    LOW(1, "弱干扰源", "建议：日常社交媒体"),
    HIGH(2, "强干扰源", "建议：短视频、游戏"),
    BACKGROUND(3, "后台/未分类", "退出到后台时的状态");

    companion object {
        fun fromValue(value: Int): DistractionLevel {
            return entries.find { it.value == value } ?: BACKGROUND
        }
    }
}

data class InstalledApp(
    val packageName: String,
    val appName: String,
    val distractionLevel: DistractionLevel = DistractionLevel.BACKGROUND
)

data class BleLog(
    val timestamp: Long = System.currentTimeMillis(),
    val direction: String = "UNKNOWN",
    val content: String
)

data class TestUiState(
    val simulateBleConnected: Boolean = false,
    val bleLogs: List<BleLog> = emptyList(),
    val installedApps: List<InstalledApp> = emptyList(),
    val distractionLevels: Map<String, Int> = emptyMap() // packageName -> level
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
        val simulateBle = prefs.getBoolean("simulate_ble", false)
        _uiState.value = _uiState.value.copy(simulateBleConnected = simulateBle)

        loadInstalledApps()
        loadDistractionLevels()
        loadBleLogs()

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
        currentLogs.add(0, log)
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

    fun setDistractionLevel(packageName: String, level: DistractionLevel) {
        val current = _uiState.value.distractionLevels.toMutableMap()
        if (level == DistractionLevel.BACKGROUND) {
            current.remove(packageName)
        } else {
            current[packageName] = level.value
        }
        _uiState.value = _uiState.value.copy(distractionLevels = current)
        saveDistractionLevels(current)
        updateAppsDistractionLevels(current)
    }

    fun getDistractionLevel(packageName: String): DistractionLevel {
        val levelValue = _uiState.value.distractionLevels[packageName]
        return if (levelValue != null) {
            DistractionLevel.fromValue(levelValue)
        } else {
            DistractionLevel.BACKGROUND
        }
    }

    fun isSimulatingBle(): Boolean {
        return _uiState.value.simulateBleConnected
    }

    private fun loadInstalledApps() {
        viewModelScope.launch {
            try {
                val pm = getApplication<Application>().packageManager
                val myPackageName = getApplication<Application>().packageName

                // 方法1：通过launcher intent获取
                val intent = android.content.Intent(android.content.Intent.ACTION_MAIN).apply {
                    addCategory(android.content.Intent.CATEGORY_LAUNCHER)
                }
                val resolveInfos = pm.queryIntentActivities(intent, PackageManager.MATCH_ALL)

                val apps = mutableMapOf<String, InstalledApp>()

                resolveInfos.forEach { resolveInfo ->
                    val packageName = resolveInfo.activityInfo.packageName
                    if (packageName != myPackageName && !apps.containsKey(packageName)) {
                        val appName = resolveInfo.loadLabel(pm).toString()
                        apps[packageName] = InstalledApp(
                            packageName = packageName,
                            appName = appName,
                            distractionLevel = DistractionLevel.BACKGROUND
                        )
                    }
                }

                // 方法2：如果launcher intent获取的应用太少，补充通过getInstalledApplications获取
                if (apps.size < 20) {
                    val installedApps = pm.getInstalledApplications(PackageManager.GET_META_DATA)
                    installedApps.forEach { appInfo ->
                        val packageName = appInfo.packageName
                        if (packageName != myPackageName && !apps.containsKey(packageName)) {
                            // 检查是否有启动器activity
                            val launchIntent = pm.getLaunchIntentForPackage(packageName)
                            if (launchIntent != null) {
                                val appName = pm.getApplicationLabel(appInfo).toString()
                                apps[packageName] = InstalledApp(
                                    packageName = packageName,
                                    appName = appName,
                                    distractionLevel = DistractionLevel.BACKGROUND
                                )
                            }
                        }
                    }
                }

                val sortedApps = apps.values.sortedBy { it.appName.lowercase() }
                _uiState.value = _uiState.value.copy(installedApps = sortedApps)
                updateAppsDistractionLevels(_uiState.value.distractionLevels)
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }

    private fun loadDistractionLevels() {
        val saved = prefs.getString("distraction_levels", "") ?: ""
        if (saved.isNotEmpty()) {
            val levels = mutableMapOf<String, Int>()
            saved.split(";").filter { it.isNotEmpty() }.forEach { entry ->
                val parts = entry.split("=")
                if (parts.size == 2 && parts[0].isNotEmpty()) {
                    levels[parts[0]] = parts[1].toIntOrNull() ?: 3
                }
            }

            _uiState.value = _uiState.value.copy(distractionLevels = levels)
            updateAppsDistractionLevels(levels)
        }
    }

    private fun saveDistractionLevels(levels: Map<String, Int>) {
        val serialized = levels.entries.joinToString(";") { "${it.key}=${it.value}" }
        prefs.edit().putString("distraction_levels", serialized).apply()
    }

    private fun updateAppsDistractionLevels(levels: Map<String, Int>) {
        val updatedApps = _uiState.value.installedApps.map { app ->
            val levelValue = levels[app.packageName]
            app.copy(
                distractionLevel = if (levelValue != null) {
                    DistractionLevel.fromValue(levelValue)
                } else {
                    DistractionLevel.BACKGROUND
                }
            )
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
