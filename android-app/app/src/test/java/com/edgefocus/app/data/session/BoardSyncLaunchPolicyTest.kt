package com.edgefocus.app.data.session

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BoardSyncLaunchPolicyTest {
    @Test
    fun `launches only when protocol is ready and no sync job is active`() {
        assertTrue(BoardSyncLaunchPolicy.shouldLaunch(protocolReady = true, currentJobActive = false))
        assertFalse(BoardSyncLaunchPolicy.shouldLaunch(protocolReady = false, currentJobActive = false))
        assertFalse(BoardSyncLaunchPolicy.shouldLaunch(protocolReady = true, currentJobActive = true))
    }
}
