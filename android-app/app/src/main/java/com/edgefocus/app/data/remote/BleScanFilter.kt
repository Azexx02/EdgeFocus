package com.edgefocus.app.data.remote

internal data class BleScanDecision(
    val shouldAutoConnectTarget: Boolean,
    val shouldShowInDiscoveredList: Boolean
)

internal object BleScanFilter {
    private const val EDGEFOCUS_NAME = "EdgeFocus"

    fun decide(
        deviceName: String,
        deviceAddress: String,
        targetMacAddress: String,
        allowAutoConnectTarget: Boolean = true
    ): BleScanDecision {
        val isTarget = allowAutoConnectTarget &&
            targetMacAddress.isNotBlank() &&
            deviceAddress.equals(targetMacAddress.trim(), ignoreCase = true)
        val hasEdgeFocusName = deviceName.contains(EDGEFOCUS_NAME, ignoreCase = true)

        return BleScanDecision(
            shouldAutoConnectTarget = isTarget,
            shouldShowInDiscoveredList = hasEdgeFocusName
        )
    }
}
