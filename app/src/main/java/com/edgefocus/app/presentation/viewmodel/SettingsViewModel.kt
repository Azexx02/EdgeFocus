package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.domain.repository.ISettingsRepository
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SettingsViewModel @Inject constructor(
    private val settingsRepository: ISettingsRepository
) : ViewModel() {

    private val _settings = MutableStateFlow(Settings())
    val settings: StateFlow<Settings> = _settings.asStateFlow()

    init {
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let { _settings.value = it }
            }
        }
    }

    fun updatePomodoroDuration(duration: Int) {
        _settings.value = _settings.value.copy(pomodoroDuration = duration)
        saveSettings()
    }

    fun updateShortBreakDuration(duration: Int) {
        _settings.value = _settings.value.copy(shortBreakDuration = duration)
        saveSettings()
    }

    fun updateLongBreakDuration(duration: Int) {
        _settings.value = _settings.value.copy(longBreakDuration = duration)
        saveSettings()
    }

    fun updateUseEdgeFocusBoard(enabled: Boolean) {
        _settings.value = _settings.value.copy(useEdgeFocusBoard = enabled)
        saveSettings()
    }

    fun updateAutoRotateNFC(enabled: Boolean) {
        _settings.value = _settings.value.copy(autoRotateNFC = enabled)
        saveSettings()
    }

    fun updateNotificationOnExit(enabled: Boolean) {
        _settings.value = _settings.value.copy(notificationOnExit = enabled)
        saveSettings()
    }

    private fun saveSettings() {
        viewModelScope.launch {
            settingsRepository.updateSettings(_settings.value)
        }
    }
}
