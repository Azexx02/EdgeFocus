package com.edgefocus.app.data.session

import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.ReliableEventType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReliableEventPolicyTest {
    @Test
    fun deadlineUsesClampedRemainingTtlOnPhoneClock() {
        assertEquals(1_400L, ReliableEventPolicy.localDeadlineElapsedMs(1_000L, 500L, 400L))
        assertEquals(1_500L, ReliableEventPolicy.localDeadlineElapsedMs(1_000L, 500L, 900L))
        assertEquals(1_000L, ReliableEventPolicy.localDeadlineElapsedMs(1_000L, 500L, -1L))
    }

    @Test
    fun sessionEndNeverSupportsUserAck() {
        assertTrue(ReliableEventPolicy.supportsAckStage(ReliableEventType.SESSION_END, EventAckStage.RECEIVED))
        assertTrue(ReliableEventPolicy.supportsAckStage(ReliableEventType.SESSION_END, EventAckStage.PRESENTED))
        assertFalse(ReliableEventPolicy.supportsAckStage(ReliableEventType.SESSION_END, EventAckStage.USER_ACK))
        assertTrue(ReliableEventPolicy.supportsAckStage(ReliableEventType.ALERT, EventAckStage.USER_ACK))
    }

    @Test
    fun gimbalCommandResultOnlyRequiresReceivedAck() {
        assertTrue(ReliableEventPolicy.supportsAckStage(ReliableEventType.GIMBAL_COMMAND_RESULT, EventAckStage.RECEIVED))
        assertFalse(ReliableEventPolicy.supportsAckStage(ReliableEventType.GIMBAL_COMMAND_RESULT, EventAckStage.PRESENTED))
        assertFalse(ReliableEventPolicy.supportsAckStage(ReliableEventType.GIMBAL_COMMAND_RESULT, EventAckStage.USER_ACK))
    }
}
