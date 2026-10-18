package com.edgefocus.app.presentation.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.domain.repository.ISettingsRepository
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.GimbalStatus
import com.edgefocus.app.data.ble.model.HostSessionPhase
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import javax.inject.Inject

data class BandControlUiState(
    val hostConnected: Boolean = false,
    val braceletConnected: Boolean? = null,
    val available: Boolean = false,
    val inProgress: Boolean = false,
    val message: String? = null
)

data class GimbalMaintenanceUiState(
    val hostConnected: Boolean = false,
    val available: Boolean = false,
    val hostIdle: Boolean = false,
    val inProgress: Boolean = false,
    val status: GimbalStatus? = null,
    val message: String? = null
)

@HiltViewModel
class SettingsViewModel @Inject constructor(
    private val settingsRepository: ISettingsRepository,
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor
) : ViewModel() {

    private val _settings = MutableStateFlow(Settings())
    val settings: StateFlow<Settings> = _settings.asStateFlow()
    private val _bandControl = MutableStateFlow(BandControlUiState())
    val bandControl: StateFlow<BandControlUiState> = _bandControl.asStateFlow()
    private val _gimbalMaintenance = MutableStateFlow(GimbalMaintenanceUiState())
    val gimbalMaintenance: StateFlow<GimbalMaintenanceUiState> =
        _gimbalMaintenance.asStateFlow()

    init {
        viewModelScope.launch {
            settingsRepository.getSettings().collect { settings ->
                settings?.let { _settings.value = it }
            }
        }
        viewModelScope.launch {
            connectionSupervisor.snapshot.collect { snapshot ->
                _bandControl.update {
                    it.copy(
                        hostConnected = snapshot.isReady,
                        braceletConnected = snapshot.hostState?.braceletConnected,
                        available = snapshot.hello?.supports("band_proxy_control_v1") == true
                    )
                }
                _gimbalMaintenance.update {
                    it.copy(
                        hostConnected = snapshot.isReady,
                        available = snapshot.hello?.supports(HostJsonCodec.CAP_GIMBAL_ORIGIN) == true,
                        hostIdle = snapshot.hostState?.session?.phase == HostSessionPhase.IDLE
                    )
                }
            }
        }
    }

    fun refreshHostStatus() {
        if (!connectionSupervisor.snapshot.value.isReady) return
        viewModelScope.launch {
            connectionSupervisor.refreshHostState()
            refreshGimbalMaintenanceStatusInternal()
        }
    }

    fun refreshGimbalMaintenanceStatus() {
        if (!connectionSupervisor.snapshot.value.isReady) return
        viewModelScope.launch { refreshGimbalMaintenanceStatusInternal() }
    }

    fun captureForwardOrigin() {
        val current = _gimbalMaintenance.value
        if (!current.hostConnected || !current.available || !current.hostIdle || current.inProgress) return
        viewModelScope.launch {
            _gimbalMaintenance.update { it.copy(inProgress = true, message = null) }
            bleManager.captureForwardOrigin(System.currentTimeMillis().coerceAtLeast(1L))
                .onSuccess { response ->
                    val acceptance = HostJsonCodec.parseGimbalAcceptance(
                        String(response, Charsets.UTF_8)
                    )
                    _gimbalMaintenance.update {
                        it.copy(
                            message = if (acceptance.accepted) {
                                "正前方已保存（原点版本 ${acceptance.originEpoch ?: "已更新"}）"
                            } else {
                                acceptance.error ?: "主机未接受正前方设置"
                            }
                        )
                    }
                    refreshGimbalMaintenanceStatusInternal()
                }
                .onFailure { error ->
                    _gimbalMaintenance.update {
                        it.copy(message = error.message ?: "设置正前方失败")
                    }
                }
            _gimbalMaintenance.update { it.copy(inProgress = false) }
        }
    }

    private suspend fun refreshGimbalMaintenanceStatusInternal() {
        bleManager.getGimbalStatus()
            .onSuccess { response ->
                _gimbalMaintenance.update {
                    it.copy(
                        status = BoardJsonParser.parseGimbalStatus(
                            String(response, Charsets.UTF_8)
                        )
                    )
                }
            }
            .onFailure { error ->
                _gimbalMaintenance.update {
                    it.copy(message = error.message ?: "无法读取云台维护状态")
                }
            }
    }

    fun updatePomodoroDuration(duration: Int) {
        _settings.value = _settings.value.copy(pomodoroDuration = duration)
        saveSettings()
    }

    fun updateUseEdgeFocusBoard(enabled: Boolean) {
        _settings.value = _settings.value.copy(useEdgeFocusBoard = enabled)
        saveSettings()
    }

    fun updateScreenSaverEnabled(enabled: Boolean) {
        _settings.value = _settings.value.copy(screenSaverEnabled = enabled)
        saveSettings()
    }

    fun updateScreenSaverTimeout(seconds: Int) {
        _settings.value = _settings.value.copy(screenSaverTimeoutSec = seconds.coerceIn(15, 600))
        saveSettings()
    }

    fun updateAppScreenAlerts(enabled: Boolean) {
        _settings.value = _settings.value.copy(appScreenAlertsEnabled = enabled)
        saveSettings()
    }

    fun updateAppSoundAlerts(enabled: Boolean) {
        _settings.value = _settings.value.copy(appSoundAlertsEnabled = enabled)
        saveSettings()
    }

    fun requestBandSearch() = runBandControl("scan_connect")

    fun requestBandReconnect() = runBandControl("reconnect")

    private fun runBandControl(action: String) {
        val current = _bandControl.value
        if (!current.hostConnected || !current.available || current.inProgress) return
        viewModelScope.launch {
            _bandControl.update { it.copy(inProgress = true, message = null) }
            bleManager.bandControl(action)
                .onSuccess { response ->
                    val accepted = HostJsonCodec.parseBusinessAcceptance(String(response, Charsets.UTF_8))
                    _bandControl.update {
                        it.copy(
                            message = if (accepted.accepted) {
                                if (action == "reconnect") "已要求主机立即重连手环" else "已要求主机立即搜索手环"
                            } else {
                                accepted.error ?: "主机未接受手环操作"
                            }
                        )
                    }
                    repeat(6) {
                        delay(1_500L)
                        connectionSupervisor.refreshHostState()
                    }
                }
                .onFailure { error ->
                    _bandControl.update { it.copy(message = error.message ?: "手环操作失败") }
                }
            _bandControl.update { it.copy(inProgress = false) }
        }
    }

    private fun saveSettings() {
        viewModelScope.launch {
            settingsRepository.updateSettings(_settings.value)
        }
    }
}
