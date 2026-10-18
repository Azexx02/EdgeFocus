package com.edgefocus.app

import android.app.Application
import com.edgefocus.app.data.ble.BleAutoConnectCoordinator
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.domain.alert.AlertDeliveryCoordinator
import com.edgefocus.app.domain.session.AppSessionCoordinator
import dagger.hilt.android.HiltAndroidApp
import javax.inject.Inject

@HiltAndroidApp
class EdgeFocusApp : Application() {

    @Inject
    lateinit var autoConnectCoordinator: BleAutoConnectCoordinator

    @Inject
    lateinit var connectionSupervisor: ConnectionSupervisor

    @Inject
    lateinit var sessionCoordinator: AppSessionCoordinator

    @Inject
    lateinit var alertDeliveryCoordinator: AlertDeliveryCoordinator

    override fun onCreate() {
        super.onCreate()
        connectionSupervisor.start()
        autoConnectCoordinator.start()
        sessionCoordinator.start()
        alertDeliveryCoordinator.start()
    }
}
