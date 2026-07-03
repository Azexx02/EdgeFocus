package com.edgefocus.app.data.remote

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BleGattConnectionPolicyTest {

    @Test
    fun usesExplicitLeTransportOnModernAndroid() {
        assertTrue(BleGattConnectionPolicy.shouldUseExplicitLeTransport(sdkInt = 23))
        assertTrue(BleGattConnectionPolicy.shouldUseExplicitLeTransport(sdkInt = 34))
    }

    @Test
    fun fallsBackToLegacyConnectGattBeforeMarshmallow() {
        assertFalse(BleGattConnectionPolicy.shouldUseExplicitLeTransport(sdkInt = 22))
    }

    @Test
    fun usesValueBasedDescriptorWriteOnAndroid13AndLater() {
        assertTrue(BleGattConnectionPolicy.shouldUseValueBasedDescriptorWrite(sdkInt = 33))
        assertTrue(BleGattConnectionPolicy.shouldUseValueBasedDescriptorWrite(sdkInt = 34))
    }

    @Test
    fun fallsBackToLegacyDescriptorWriteBeforeAndroid13() {
        assertFalse(BleGattConnectionPolicy.shouldUseValueBasedDescriptorWrite(sdkInt = 32))
    }

    @Test
    fun keepsErrorVisibleWhenFailureDisconnectCallbackArrivesAfterInitializationError() {
        assertTrue(
            BleGattConnectionPolicy.shouldKeepErrorOnDisconnected(
                previousState = ConnectionState.ERROR,
                gattStatusSuccessful = true
            )
        )
    }

    @Test
    fun treatsFailedDisconnectDuringConnectAsConnectionError() {
        assertTrue(
            BleGattConnectionPolicy.shouldKeepErrorOnDisconnected(
                previousState = ConnectionState.CONNECTING,
                gattStatusSuccessful = false
            )
        )
    }

    @Test
    fun allowsNormalDisconnectAfterSuccessfulGattDisconnect() {
        assertFalse(
            BleGattConnectionPolicy.shouldKeepErrorOnDisconnected(
                previousState = ConnectionState.CONNECTED,
                gattStatusSuccessful = true
            )
        )
    }

    @Test
    fun skipsOnlyWhenAlreadyConnectedToRequestedAddress() {
        assertTrue(
            BleGattConnectionPolicy.shouldSkipConnectionRequest(
                currentState = ConnectionState.CONNECTED,
                currentGattAddress = "AA:BB:CC:DD:EE:FF",
                requestedAddress = "aa:bb:cc:dd:ee:ff"
            )
        )
    }

    @Test
    fun doesNotSkipWhenConnectedToDifferentAddress() {
        assertFalse(
            BleGattConnectionPolicy.shouldSkipConnectionRequest(
                currentState = ConnectionState.CONNECTED,
                currentGattAddress = "AA:BB:CC:DD:EE:01",
                requestedAddress = "AA:BB:CC:DD:EE:02"
            )
        )
    }

    @Test
    fun doesNotSkipConnectingStateSoStaleAttemptsCanBeReplaced() {
        assertFalse(
            BleGattConnectionPolicy.shouldSkipConnectionRequest(
                currentState = ConnectionState.CONNECTING,
                currentGattAddress = "AA:BB:CC:DD:EE:FF",
                requestedAddress = "AA:BB:CC:DD:EE:FF"
            )
        )
    }
}
