package com.edgefocus.app.data.ble

import com.edgefocus.app.data.remote.ConnectionState

enum class BleAutoConnectBlockReason {
    BOARD_MODE_DISABLED,
    TARGET_MISSING,
    BLUETOOTH_DISABLED,
    PERMISSION_MISSING,
    USER_DISCONNECTED,
    OPERATION_IN_PROGRESS
}

data class BleAutoConnectInputs(
    val useEdgeFocusBoard: Boolean,
    val targetMac: String,
    val bluetoothEnabled: Boolean,
    val permissionsGranted: Boolean,
    val userDisconnected: Boolean,
    val connectionState: ConnectionState
)

data class BleAutoConnectDecision(
    val allowed: Boolean,
    val targetMac: String? = null,
    val blockedBy: BleAutoConnectBlockReason? = null
)

object BleAutoConnectPolicy {
    fun decide(inputs: BleAutoConnectInputs): BleAutoConnectDecision {
        val target = inputs.targetMac.trim()
        val blockedBy = when {
            !inputs.useEdgeFocusBoard -> BleAutoConnectBlockReason.BOARD_MODE_DISABLED
            target.isBlank() -> BleAutoConnectBlockReason.TARGET_MISSING
            !inputs.bluetoothEnabled -> BleAutoConnectBlockReason.BLUETOOTH_DISABLED
            !inputs.permissionsGranted -> BleAutoConnectBlockReason.PERMISSION_MISSING
            inputs.userDisconnected -> BleAutoConnectBlockReason.USER_DISCONNECTED
            inputs.connectionState in setOf(
                ConnectionState.SCANNING,
                ConnectionState.CONNECTING,
                ConnectionState.CONNECTED
            ) -> BleAutoConnectBlockReason.OPERATION_IN_PROGRESS
            else -> null
        }
        return if (blockedBy == null) {
            BleAutoConnectDecision(allowed = true, targetMac = target)
        } else {
            BleAutoConnectDecision(allowed = false, blockedBy = blockedBy)
        }
    }
}
