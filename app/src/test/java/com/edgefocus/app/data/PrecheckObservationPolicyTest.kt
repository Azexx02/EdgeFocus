package com.edgefocus.app.data

import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PrecheckObservationPolicyTest {
    @Test
    fun `response started before exit completion is stale`() {
        assertTrue(PrecheckObservationPolicy.isStale(responseEpoch = 3L, currentEpoch = 4L))
        assertFalse(PrecheckObservationPolicy.isStale(responseEpoch = 4L, currentEpoch = 4L))
    }

    @Test
    fun `closed precheck session cannot resurrect`() {
        val oldPrecheck = BoardSessionState(
            sessionId = "session-old",
            sessionPhase = BoardSessionPhase.PRECHECK
        )
        val newPrecheck = oldPrecheck.copy(sessionId = "session-new")

        assertTrue(PrecheckObservationPolicy.isClosedPrecheck("session-old", oldPrecheck))
        assertFalse(PrecheckObservationPolicy.isClosedPrecheck("session-old", newPrecheck))
    }
}
