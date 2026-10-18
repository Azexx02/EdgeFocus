package com.edgefocus.app.presentation.viewmodel

import android.app.Application
import android.content.pm.PackageManager
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

data class DistractionAppsUiState(
    val installedApps: List<InstalledApp> = emptyList(),
    val distractionLevels: Map<String, Int> = emptyMap()
)

@HiltViewModel
class DistractionAppsViewModel @Inject constructor(
    application: Application
) : AndroidViewModel(application) {

    private val prefs = application.getSharedPreferences(DistractionAppConfig.PREFS_NAME, 0)

    private val _uiState = MutableStateFlow(DistractionAppsUiState())
    val uiState: StateFlow<DistractionAppsUiState> = _uiState.asStateFlow()

    init {
        loadDistractionLevels()
        loadInstalledApps()
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

    private fun loadInstalledApps() {
        viewModelScope.launch {
            val pm = getApplication<Application>().packageManager
            val intent = android.content.Intent(android.content.Intent.ACTION_MAIN).apply {
                addCategory(android.content.Intent.CATEGORY_LAUNCHER)
            }
            val apps = linkedMapOf<String, InstalledApp>()
            pm.queryIntentActivities(intent, PackageManager.MATCH_ALL).forEach { resolveInfo ->
                val packageName = resolveInfo.activityInfo.packageName
                if (!packageName.startsWith("com.edgefocus.app") && !apps.containsKey(packageName)) {
                    apps[packageName] = InstalledApp(
                        packageName = packageName,
                        appName = resolveInfo.loadLabel(pm).toString()
                    )
                }
            }
            if (apps.size < 20) {
                pm.getInstalledApplications(PackageManager.GET_META_DATA).forEach { appInfo ->
                    val packageName = appInfo.packageName
                    if (!packageName.startsWith("com.edgefocus.app") &&
                        !apps.containsKey(packageName) &&
                        pm.getLaunchIntentForPackage(packageName) != null
                    ) {
                        apps[packageName] = InstalledApp(
                            packageName = packageName,
                            appName = pm.getApplicationLabel(appInfo).toString()
                        )
                    }
                }
            }
            _uiState.value = _uiState.value.copy(
                installedApps = apps.values.sortedBy { it.appName.lowercase() }
            )
            updateAppsDistractionLevels(_uiState.value.distractionLevels)
        }
    }

    private fun loadDistractionLevels() {
        val saved = prefs.getString(DistractionAppConfig.LEVELS_KEY, "") ?: ""
        _uiState.value = _uiState.value.copy(
            distractionLevels = DistractionAppConfig.parseLevels(saved)
        )
    }

    private fun saveDistractionLevels(levels: Map<String, Int>) {
        prefs.edit()
            .putString(DistractionAppConfig.LEVELS_KEY, DistractionAppConfig.serializeLevels(levels))
            .apply()
    }

    private fun updateAppsDistractionLevels(levels: Map<String, Int>) {
        _uiState.value = _uiState.value.copy(
            installedApps = _uiState.value.installedApps.map { app ->
                val levelValue = levels[app.packageName]
                app.copy(
                    distractionLevel = if (levelValue != null) {
                        DistractionLevel.fromValue(levelValue)
                    } else {
                        DistractionLevel.BACKGROUND
                    }
                )
            }
        )
    }
}
