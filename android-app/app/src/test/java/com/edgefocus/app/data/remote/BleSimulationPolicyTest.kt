package com.edgefocus.app.data.remote

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BleSimulationPolicyTest {

    @Test
    fun releaseBuildIgnoresSimulationPreferences() {
        assertFalse(BleSimulationPolicy.isSimulationEnabled(isDebug = false, preferenceEnabled = true))
        assertEquals(
            ConnectionState.DISCONNECTED,
            BleSimulationPolicy.effectiveConnectionState(
                isDebug = false,
                preferenceEnabled = true,
                simulatedConnected = true,
                actualState = ConnectionState.DISCONNECTED
            )
        )
    }

    @Test
    fun debugBuildCanUseSimulationPreferences() {
        assertTrue(BleSimulationPolicy.isSimulationEnabled(isDebug = true, preferenceEnabled = true))
        assertEquals(
            ConnectionState.CONNECTED,
            BleSimulationPolicy.effectiveConnectionState(
                isDebug = true,
                preferenceEnabled = true,
                simulatedConnected = true,
                actualState = ConnectionState.DISCONNECTED
            )
        )
    }
}
