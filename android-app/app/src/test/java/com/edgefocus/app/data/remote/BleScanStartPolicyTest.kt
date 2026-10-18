package com.edgefocus.app.data.remote

import org.junit.Assert.assertTrue
import org.junit.Assert.assertFalse
import org.junit.Test

class BleScanStartPolicyTest {

    @Test
    fun userRequestedScanDisablesSimulationBeforeRealScan() {
        val decision = BleScanStartPolicy.decide(isSimulatingBle = true)

        assertTrue(decision.shouldDisableSimulation)
        assertTrue(decision.shouldStartRealScan)
    }

    @Test
    fun userRequestedScanStartsRealScanWhenSimulationIsOff() {
        val decision = BleScanStartPolicy.decide(isSimulatingBle = false)

        assertTrue(decision.shouldStartRealScan)
    }

    @Test
    fun userRequestedScanUsesManualDiscoveryInsteadOfAutoConnect() {
        val decision = BleScanStartPolicy.decide(isSimulatingBle = false)

        assertFalse(decision.shouldAutoConnectStoredTarget)
    }
}
