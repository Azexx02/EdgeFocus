package com.edgefocus.app.data.remote

internal data class BleScanStartDecision(
    val shouldDisableSimulation: Boolean,
    val shouldStartRealScan: Boolean,
    val shouldAutoConnectStoredTarget: Boolean
)

internal object BleScanStartPolicy {
    fun decide(isSimulatingBle: Boolean): BleScanStartDecision {
        return BleScanStartDecision(
            shouldDisableSimulation = isSimulatingBle,
            shouldStartRealScan = true,
            shouldAutoConnectStoredTarget = false
        )
    }
}
