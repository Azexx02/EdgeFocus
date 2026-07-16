package com.edgefocus.app.data.ble

import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.HostSessionPhase
import com.edgefocus.app.data.ble.model.PhoneUsageState
import com.edgefocus.app.data.ble.model.ReliableEventType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HostJsonCodecTest {
    @Test
    fun `start always uses live camera`() {
        val request = HostJsonCodec.sessionControl(7, "start", 25)

        assertEquals("live_camera", request.getJSONObject("data").getString("source_mode"))
    }

    @Test
    fun `phone sync contains raw fact and no distraction level`() {
        val request = HostJsonCodec.phoneSync(
            requestId = 9,
            sessionId = "session_1",
            sequence = 42,
            timestampMs = 1000,
            isInUse = true,
            usageState = PhoneUsageState.PLAYING
        )
        val data = request.getJSONObject("data")

        assertEquals("playing", data.getString("usage_state"))
        assertFalse(data.has("distraction_level"))
        assertFalse(data.has("phone_distraction_level"))
    }

    @Test
    fun `not in use omits usage state`() {
        val request = HostJsonCodec.phoneSync(9, "session_1", 1, 1000, false)

        assertFalse(request.getJSONObject("data").has("usage_state"))
    }

    @Test
    fun `ack status is read from response data`() {
        val result = HostJsonCodec.parseEventAck(
            """{"status":"success","id":3,"data":{"ack_status":"duplicate","event_id":"evt_1","stage":"presented"}}"""
        )

        assertEquals("duplicate", result.status)
        assertTrue(result.isSuccessful)
    }

    @Test
    fun `runtime snapshot focusing maps to running and wins over legacy fields`() {
        val result = HostJsonCodec.parseState(
            """{"status":"success","data":{"session_phase":"idle","runtime_snapshot":{"session":{"id":"session_2","phase":"FOCUSING","active":true,"runtime_active":true,"elapsed_ms":100,"remaining_ms":900},"phone":{"sequence":8}},"external_power_state":"UNKNOWN","external_power_present":null}}"""
        )

        assertEquals(HostSessionPhase.RUNNING, result.session.phase)
        assertEquals("session_2", result.session.sessionId)
        assertEquals(8L, result.session.phoneSequence)
        assertTrue(result.session.isApplicable)
        assertNull(result.power.present)
    }

    @Test
    fun `missing phase is explicit unknown and not applicable`() {
        val result = HostJsonCodec.parseState(
            """{"status":"success","data":{"session_id":"session_2"}}"""
        )

        assertEquals(HostSessionPhase.UNKNOWN, result.session.phase)
        assertFalse(result.session.isApplicable)
    }

    @Test
    fun `band availability wins when legacy bracelet fields conflict`() {
        val result = HostJsonCodec.parseState(
            """{"status":"success","data":{"session_phase":"idle","bracelet_connected":false,"runtime_snapshot":{"device":{"bracelet_connected":false}},"output_capability":{"band_available":true}}}"""
        )

        assertEquals(true, result.braceletConnected)
    }

    @Test
    fun `reliable event clamps remaining ttl`() {
        val event = HostJsonCodec.parseReliableEvent(
            """{"cmd":"push_alert","data":{"event_id":"evt_1","session_id":"session_1","created_at_ms":99,"ttl_ms":10000,"remaining_ttl_ms":12000}}"""
        )!!

        assertEquals(ReliableEventType.ALERT, event.type)
        assertEquals(10_000L, event.remainingTtlMs)
    }

    @Test
    fun `session end ack builder supports received without inventing user action`() {
        val request = HostJsonCodec.eventAck(
            requestId = 5,
            eventId = "evt_end",
            sessionId = "session_1",
            stage = EventAckStage.RECEIVED
        )

        assertEquals("received", request.getJSONObject("data").getString("stage"))
    }
}
