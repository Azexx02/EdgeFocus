package com.edgefocus.app.data.session

internal object BoardSyncLaunchPolicy {
    fun shouldLaunch(protocolReady: Boolean, currentJobActive: Boolean): Boolean =
        protocolReady && !currentJobActive
}
