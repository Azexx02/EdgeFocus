package com.edgefocus.app.data.remote

object BleSimulationPolicy {
    fun isSimulationEnabled(isDebug: Boolean, preferenceEnabled: Boolean): Boolean {
        return isDebug && preferenceEnabled
    }

    fun effectiveConnectionState(
        isDebug: Boolean,
        preferenceEnabled: Boolean,
        simulatedConnected: Boolean,
        actualState: ConnectionState
    ): ConnectionState {
        return if (isSimulationEnabled(isDebug, preferenceEnabled) && simulatedConnected) {
            ConnectionState.CONNECTED
        } else {
            actualState
        }
    }
}
