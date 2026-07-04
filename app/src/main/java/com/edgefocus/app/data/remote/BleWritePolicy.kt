package com.edgefocus.app.data.remote

object BleWritePolicy {
    private const val ATT_HEADER_BYTES = 3
    private const val DEFAULT_MTU = 23

    fun maxWritablePayloadBytes(mtu: Int): Int {
        return (mtu.takeIf { it > ATT_HEADER_BYTES } ?: DEFAULT_MTU) - ATT_HEADER_BYTES
    }

    fun validateRequestSize(byteCount: Int, mtu: Int): String? {
        val maxPayloadBytes = maxWritablePayloadBytes(mtu)
        return if (byteCount > maxPayloadBytes) {
            "BLE request too large: $byteCount bytes exceeds writable payload $maxPayloadBytes bytes"
        } else {
            null
        }
    }
}
