package com.edgefocus.app.data.remote

internal object BleGattConnectionPolicy {
    fun shouldUseExplicitLeTransport(sdkInt: Int): Boolean = sdkInt >= 23

    fun shouldUseValueBasedDescriptorWrite(sdkInt: Int): Boolean = sdkInt >= 33

    fun shouldKeepErrorOnDisconnected(
        previousState: ConnectionState,
        gattStatusSuccessful: Boolean
    ): Boolean {
        return previousState == ConnectionState.ERROR ||
            (previousState == ConnectionState.CONNECTING && !gattStatusSuccessful)
    }

    fun shouldSkipConnectionRequest(
        currentState: ConnectionState,
        currentGattAddress: String?,
        requestedAddress: String
    ): Boolean {
        return currentState in setOf(ConnectionState.CONNECTING, ConnectionState.CONNECTED) &&
            currentGattAddress.equals(requestedAddress, ignoreCase = true)
    }
}
