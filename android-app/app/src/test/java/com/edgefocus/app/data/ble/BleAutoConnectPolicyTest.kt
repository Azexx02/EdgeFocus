package com.edgefocus.app.data.ble

import com.edgefocus.app.data.remote.ConnectionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BleAutoConnectPolicyTest {
    private fun inputs(
        useBoard: Boolean = true,
        targetMac: String = "11:22:33:44:55:66",
        bluetoothEnabled: Boolean = true,
        permissionsGranted: Boolean = true,
        userDisconnected: Boolean = false,
        state: ConnectionState = ConnectionState.DISCONNECTED
    ) = BleAutoConnectInputs(
        useEdgeFocusBoard = useBoard,
        targetMac = targetMac,
        bluetoothEnabled = bluetoothEnabled,
        permissionsGranted = permissionsGranted,
        userDisconnected = userDisconnected,
        connectionState = state
    )

    @Test
    fun eligibleKnownTargetIsAllowed() {
        val decision = BleAutoConnectPolicy.decide(inputs())

        assertTrue(decision.allowed)
        assertEquals("11:22:33:44:55:66", decision.targetMac)
    }

    @Test
    fun everySafetyGateBlocksAutomaticConnection() {
        assertEquals(BleAutoConnectBlockReason.BOARD_MODE_DISABLED, BleAutoConnectPolicy.decide(inputs(useBoard = false)).blockedBy)
        assertEquals(BleAutoConnectBlockReason.TARGET_MISSING, BleAutoConnectPolicy.decide(inputs(targetMac = "")).blockedBy)
        assertEquals(BleAutoConnectBlockReason.BLUETOOTH_DISABLED, BleAutoConnectPolicy.decide(inputs(bluetoothEnabled = false)).blockedBy)
        assertEquals(BleAutoConnectBlockReason.PERMISSION_MISSING, BleAutoConnectPolicy.decide(inputs(permissionsGranted = false)).blockedBy)
        assertEquals(BleAutoConnectBlockReason.USER_DISCONNECTED, BleAutoConnectPolicy.decide(inputs(userDisconnected = true)).blockedBy)
    }

    @Test
    fun activeBleOperationPreventsDuplicateAttempt() {
        listOf(ConnectionState.SCANNING, ConnectionState.CONNECTING, ConnectionState.CONNECTED).forEach { state ->
            val decision = BleAutoConnectPolicy.decide(inputs(state = state))
            assertFalse(decision.allowed)
            assertEquals(BleAutoConnectBlockReason.OPERATION_IN_PROGRESS, decision.blockedBy)
        }
    }
}
