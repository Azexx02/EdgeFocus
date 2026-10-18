package com.edgefocus.app.data

import com.edgefocus.app.data.remote.BoardSessionPhase
import com.edgefocus.app.data.remote.BoardSessionState
import com.edgefocus.app.data.remote.effectivePhase

internal object PrecheckObservationPolicy {
    fun isStale(responseEpoch: Long, currentEpoch: Long): Boolean =
        responseEpoch != currentEpoch

    fun isClosedPrecheck(
        closedSessionId: String?,
        observed: BoardSessionState
    ): Boolean =
        !closedSessionId.isNullOrBlank() &&
            observed.effectivePhase() == BoardSessionPhase.PRECHECK &&
            observed.sessionId == closedSessionId
}
