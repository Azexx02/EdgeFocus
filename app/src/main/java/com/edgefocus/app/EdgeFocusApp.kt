package com.edgefocus.app

import android.app.Application
import android.util.Log
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.local.entity.Settings
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.domain.session.AppSessionCoordinator
import com.edgefocus.app.domain.alert.AlertDeliveryCoordinator
import dagger.hilt.android.HiltAndroidApp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltAndroidApp
class EdgeFocusApp : Application() {

    @Inject
    lateinit var settingsDao: SettingsDao

    @Inject
    lateinit var connectionSupervisor: ConnectionSupervisor

    @Inject
    lateinit var sessionCoordinator: AppSessionCoordinator

    @Inject
    lateinit var alertDeliveryCoordinator: AlertDeliveryCoordinator

    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onCreate() {
        super.onCreate()
        connectionSupervisor.start()
        sessionCoordinator.start()
        alertDeliveryCoordinator.start()
        initializeDefaultSettings()
    }

    private fun initializeDefaultSettings() {
        applicationScope.launch {
            try {
                val existingSettings = settingsDao.getSettingsOnce()
                if (existingSettings == null) {
                    // 插入默认设置
                    settingsDao.insertSettings(Settings())
                    Log.d("EdgeFocusApp", "Default settings inserted")
                } else {
                    Log.d("EdgeFocusApp", "Settings already exist: useEdgeFocusBoard=${existingSettings.useEdgeFocusBoard}")
                }
            } catch (e: Exception) {
                Log.e("EdgeFocusApp", "Failed to initialize settings: ${e.message}")
            }
        }
    }
}
