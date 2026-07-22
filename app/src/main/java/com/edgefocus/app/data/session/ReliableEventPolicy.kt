package com.edgefocus.app.data.session

import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.ReliableEventType

object ReliableEventPolicy {
    fun localDeadlineElapsedMs(
        receivedElapsedMs: Long,
        ttlMs: Long,
        remainingTtlMs: Long
    ): Long {
        val safeTtl = ttlMs.coerceAtLeast(0L)
        val safeRemaining = remainingTtlMs.coerceIn(0L, safeTtl)
        return if (Long.MAX_VALUE - receivedElapsedMs < safeRemaining) {
            Long.MAX_VALUE
        } else {
            receivedElapsedMs + safeRemaining
        }
    }

    fun supportsAckStage(type: ReliableEventType, stage: EventAckStage): Boolean = when (type) {
        ReliableEventType.ALERT -> true
        ReliableEventType.SESSION_END -> stage != EventAckStage.USER_ACK
    }
}

object SequencePolicy {
    fun next(localCheckpoint: Long?, runtimeSnapshot: Long?, lastAccepted: Long?): Long {
        val maximum = listOfNotNull(localCheckpoint, runtimeSnapshot, lastAccepted)
            .filter { it >= 0L }
            .maxOrNull() ?: 0L
        return if (maximum == Long.MAX_VALUE) Long.MAX_VALUE else maximum + 1L
    }
}

object GimbalSequencePolicy {
    fun next(localCheckpoint: Long?, hostSequence: Long?, nowMs: Long): Long {
        return SequencePolicy.next(localCheckpoint, hostSequence, nowMs)
    }
}
