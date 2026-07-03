package com.edgefocus.app.presentation.ui.settings

import com.edgefocus.app.data.remote.ConnectionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class BleConnectStatusMessageTest {

    @Test
    fun showsConnectingMessageAfterConfirmingDevice() {
        assertEquals(
            "正在连接 EdgeFocus...",
            bleConnectProgressMessage(ConnectionState.CONNECTING)
        )
    }

    @Test
    fun showsErrorMessageWhenConnectionFails() {
        assertEquals(
            "连接失败，请重新扫描或确认设备已开启",
            bleConnectErrorMessage(ConnectionState.ERROR)
        )
    }

    @Test
    fun hidesTransientMessagesWhenIdle() {
        assertNull(bleConnectProgressMessage(ConnectionState.DISCONNECTED))
        assertNull(bleConnectErrorMessage(ConnectionState.DISCONNECTED))
    }
}
