package com.edgefocus.app.data.remote

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BleScanFilterTest {

    @Test
    fun listsEdgeFocusByNameWhenAdvertisedServiceIsMissing() {
        val decision = BleScanFilter.decide(
            deviceName = "edgefocus",
            deviceAddress = "AA:BB:CC:DD:EE:01",
            targetMacAddress = ""
        )

        assertFalse(decision.shouldAutoConnectTarget)
        assertTrue(decision.shouldShowInDiscoveredList)
    }

    @Test
    fun listsVisibleEdgeFocusEvenWhenDifferentTargetMacIsStored() {
        val decision = BleScanFilter.decide(
            deviceName = "EdgeFocus",
            deviceAddress = "AA:BB:CC:DD:EE:02",
            targetMacAddress = "11:22:33:44:55:66"
        )

        assertFalse(decision.shouldAutoConnectTarget)
        assertTrue(decision.shouldShowInDiscoveredList)
    }

    @Test
    fun autoConnectsExactStoredTargetMacCaseInsensitively() {
        val decision = BleScanFilter.decide(
            deviceName = "EdgeFocus",
            deviceAddress = "AA:BB:CC:DD:EE:03",
            targetMacAddress = "aa:bb:cc:dd:ee:03"
        )

        assertTrue(decision.shouldAutoConnectTarget)
    }

    @Test
    fun doesNotAutoConnectStoredTargetWhenManualDiscoveryDisablesAutoConnect() {
        val decision = BleScanFilter.decide(
            deviceName = "EdgeFocus",
            deviceAddress = "AA:BB:CC:DD:EE:04",
            targetMacAddress = "AA:BB:CC:DD:EE:04",
            allowAutoConnectTarget = false
        )

        assertFalse(decision.shouldAutoConnectTarget)
        assertTrue(decision.shouldShowInDiscoveredList)
    }

    @Test
    fun doesNotListGenericEsp32WhenNameDoesNotMatchEdgeFocus() {
        val decision = BleScanFilter.decide(
            deviceName = "ESP32",
            deviceAddress = "AA:BB:CC:DD:EE:05",
            targetMacAddress = ""
        )

        assertFalse(decision.shouldAutoConnectTarget)
        assertFalse(decision.shouldShowInDiscoveredList)
    }

    @Test
    fun doesNotListNamelessDeviceWhenOnlyAddressIsAvailable() {
        val decision = BleScanFilter.decide(
            deviceName = "",
            deviceAddress = "AA:BB:CC:DD:EE:06",
            targetMacAddress = ""
        )

        assertFalse(decision.shouldAutoConnectTarget)
        assertFalse(decision.shouldShowInDiscoveredList)
    }
}
