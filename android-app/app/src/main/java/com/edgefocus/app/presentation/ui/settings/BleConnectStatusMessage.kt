package com.edgefocus.app.presentation.ui.settings

import com.edgefocus.app.data.remote.ConnectionState

internal fun bleConnectProgressMessage(connectionState: ConnectionState): String? {
    return when (connectionState) {
        ConnectionState.SCANNING -> "正在扫描 EdgeFocus 设备..."
        ConnectionState.CONNECTING -> "正在连接 EdgeFocus..."
        else -> null
    }
}

internal fun bleConnectErrorMessage(connectionState: ConnectionState): String? {
    return when (connectionState) {
        ConnectionState.ERROR -> "连接失败，请重新扫描或确认设备已开启"
        else -> null
    }
}
