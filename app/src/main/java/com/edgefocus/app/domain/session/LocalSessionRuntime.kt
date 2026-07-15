package com.edgefocus.app.domain.session

import com.edgefocus.app.data.SessionMode

data class LocalSessionRuntime(
    val sessionId: String,
    val owner: SessionMode,
    val startedAtMs: Long,
    val saved: Boolean = false
)

object SessionOwnershipPolicy {
    fun effectiveOwner(activeOwner: SessionMode?, requestedOwner: SessionMode): SessionMode =
        activeOwner ?: requestedOwner
}
