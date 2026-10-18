package com.edgefocus.app.data.remote

import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BleWritePolicyTest {

    @Test
    fun rejectsRequestLargerThanWritablePayload() {
        val error = BleWritePolicy.validateRequestSize(byteCount = 21, mtu = 23)
        assertTrue(error?.contains("BLE request too large") == true)
    }

    @Test
    fun allowsRequestWithinWritablePayload() {
        assertNull(BleWritePolicy.validateRequestSize(byteCount = 20, mtu = 23))
    }
}
