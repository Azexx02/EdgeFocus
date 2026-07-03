package com.edgefocus.app.data.remote

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BoardJsonParserTest {

    @Test
    fun buildSessionControlStartIncludesTargetDuration() {
        val json = BoardJsonParser.buildSessionControl("start", targetDurationMin = 25)

        assertEquals("session_control", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertEquals("start", data.getString("action"))
        assertEquals(25, data.getInt("target_duration_min"))
    }

    @Test
    fun buildPreferencesSupportsArraysBooleansAndNumbers() {
        val json = BoardJsonParser.buildUpdatePreferences(
            mapOf(
                "sound_volume" to 0.7f,
                "default_focus_duration_min" to 30,
                "gimbal_enabled" to false,
                "normal_channels" to listOf("screen", "band", "sound"),
                "silent_channels" to listOf("screen", "band")
            )
        )

        assertEquals("update_preferences", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertEquals(0.7, data.getDouble("sound_volume"), 0.001)
        assertEquals(30, data.getInt("default_focus_duration_min"))
        assertFalse(data.getBoolean("gimbal_enabled"))
        assertEquals("screen", data.getJSONArray("normal_channels").getString(0))
        assertEquals("band", data.getJSONArray("silent_channels").getString(1))
    }

    @Test
    fun parseChannelNamesNormalizesBoardUppercaseValues() {
        val array = org.json.JSONArray().apply {
            put("SCREEN")
            put("BAND")
            put("SOUND")
        }

        val channels = BoardJsonParser.parseChannelNames(array)

        assertEquals(setOf("screen", "band", "sound"), channels)
    }

    @Test
    fun parsePreferencesLoadsBoardHardwareAndChannelValues() {
        val data = org.json.JSONObject(
            """
                {
                  "system_mode":1,
                  "away_timeout_min":12,
                  "default_focus_duration_min":45,
                  "vibration_strength":0.25,
                  "led_brightness":62,
                  "reminder_sensitivity":0.8,
                  "sound_volume":0.35,
                  "normal_channels":["SCREEN","BAND","SOUND"],
                  "silent_channels":["SCREEN","BAND"],
                  "screen_output_route":"phone",
                  "sound_output_route":"board",
                  "gimbal_enabled":false
                }
            """.trimIndent()
        )

        val prefs = BoardJsonParser.parsePreferences(data)

        assertEquals(1, prefs.systemMode)
        assertEquals(12, prefs.awayTimeoutMin)
        assertEquals(45, prefs.defaultFocusDurationMin)
        assertEquals(0.25f, prefs.vibrationStrength, 0.001f)
        assertEquals(62, prefs.ledBrightness)
        assertEquals(0.8f, prefs.reminderSensitivity, 0.001f)
        assertEquals(0.35f, prefs.soundVolume, 0.001f)
        assertEquals(setOf("screen", "band", "sound"), prefs.normalChannels)
        assertEquals(setOf("screen", "band"), prefs.silentChannels)
        assertEquals("phone", prefs.screenOutputRoute)
        assertEquals("board", prefs.soundOutputRoute)
        assertFalse(prefs.gimbalEnabled)
    }

    @Test
    fun buildGimbalNudgeCommandUsesBoardStepPayload() {
        val json = BoardJsonParser.buildGimbalControl("nudge_left", stepDeg = 3)

        assertEquals("gimbal_control", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertEquals("nudge_left", data.getString("action"))
        assertEquals(3, data.getInt("step_deg"))
        assertFalse(data.has("x_deg"))
        assertFalse(data.has("y_deg"))
        assertFalse(data.has("speed"))
    }

    @Test
    fun buildSimpleCommandsUseExpectedNames() {
        assertEquals(
            "get_camera_preview_image",
            BoardJsonParser.buildCommand("get_camera_preview_image").getString("cmd")
        )
        assertEquals(
            "get_gimbal_status",
            BoardJsonParser.buildCommand("get_gimbal_status").getString("cmd")
        )
    }

    @Test
    fun parsePrecheckSessionWithGimbal() {
        val response = """
            {
              "status":"success",
              "data":{
                "success":true,
                "session_id":"session_1",
                "session_phase":"precheck",
                "remaining_ms":1500000,
                "target_duration_ms":1500000,
                "focus_state":7,
                "precheck":{
                  "camera":{"required":true},
                  "gimbal":{"enabled":true,"online":true,"safe":true,"angle_x":90,"angle_y":0}
                }
              }
            }
        """.trimIndent()

        val state = BoardJsonParser.parseSessionState(response)

        assertTrue(state.success)
        assertEquals("session_1", state.sessionId)
        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertEquals(1_500_000L, state.remainingMs)
        assertNotNull(state.precheck?.camera)
        assertNotNull(state.precheck?.gimbal)
        assertTrue(state.precheck!!.gimbal!!.online)
    }

    @Test
    fun parsePrecheckSessionWithoutGimbalKeepsGenericCameraPrecheck() {
        val response = """
            {
              "status":"success",
              "data":{
                "success":true,
                "session_phase":"precheck",
                "precheck":{"camera":{"required":true}}
              }
            }
        """.trimIndent()

        val state = BoardJsonParser.parseSessionState(response)

        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertNotNull(state.precheck?.camera)
        assertNull(state.precheck?.gimbal)
    }

    @Test
    fun parseSessionStateIncludesStatusPageFields() {
        val response = """
            {
              "status":"success",
              "data":{
                "active":true,
                "session_phase":"focusing",
                "focus_state":9,
                "elapsed_ms":30000,
                "remaining_ms":1470000,
                "target_duration_ms":1500000,
                "precheck_guidance":{"distance":"ok","posture":"lean_left"},
                "camera_preview":{"available":true,"width":320,"height":240,"sequence":12},
                "reminder":{"cooling_ms":12000,"last_level":2},
                "output_capability":{
                  "board_screen_available":false,
                  "board_sound_available":true,
                  "screen_output_route":"phone",
                  "sound_output_route":"board",
                  "band_available":true,
                  "last_attempted_channels":["screen","sound"],
                  "last_delivered_channels":["screen"]
                },
                "focus_reason":{"source":"phone"}
              }
            }
        """.trimIndent()

        val state = BoardJsonParser.parseSessionState(response)

        assertEquals(BoardSessionPhase.FOCUSING, state.sessionPhase)
        assertEquals(9, state.focusState)
        assertEquals(30_000L, state.elapsedMs)
        assertTrue(state.precheckGuidance!!.contains("lean_left"))
        assertTrue(state.cameraPreview!!.available)
        assertTrue(state.reminder!!.contains("cooling_ms"))
        assertEquals("phone", state.outputCapability!!.screenOutputRoute)
        assertEquals(listOf("screen", "sound"), state.outputCapability!!.lastAttemptedChannels)
        assertTrue(state.focusReason!!.contains("phone"))
    }

    @Test
    fun parseSessionStateAcceptsBoardCameraPreviewImageFields() {
        val response = """
            {
              "status":"success",
              "data":{
                "active":true,
                "session_phase":"precheck",
                "precheck":{
                  "camera":{
                    "required":true,
                    "preview":{
                      "camera_ok":true,
                      "image_available":true,
                      "image_sequence":42,
                      "image_timestamp_ms":123456,
                      "image_width":160,
                      "image_height":120,
                      "image_pixel_format":"RGB565",
                      "image_encoding":"base64",
                      "quality_score":0.91
                    }
                  }
                }
              }
            }
        """.trimIndent()

        val state = BoardJsonParser.parseSessionState(response)

        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertTrue(state.cameraPreview!!.available)
        assertEquals(42, state.cameraPreview!!.sequence)
        assertEquals(123456L, state.cameraPreview!!.timestampMs)
        assertEquals(160, state.cameraPreview!!.width)
        assertEquals(120, state.cameraPreview!!.height)
        assertEquals("RGB565", state.cameraPreview!!.pixelFormat)
        assertEquals("base64", state.cameraPreview!!.encoding)
        assertEquals(0.91, state.cameraPreview!!.quality!!, 0.001)
    }

    @Test
    fun parseCameraPreviewAcceptsBoardNestedQualityObject() {
        val response = """
            {
              "status":"success",
              "data":{
                "available":true,
                "timestamp_ms":222,
                "sequence":7,
                "width":160,
                "height":120,
                "quality":{"quality_score":0.66}
              }
            }
        """.trimIndent()

        val preview = BoardJsonParser.parseCameraPreview(response)

        assertTrue(preview.available)
        assertEquals(7, preview.sequence)
        assertEquals(0.66, preview.quality!!, 0.001)
    }

    @Test
    fun parsePushAlertIncludesDiagnosticFields() {
        val data = org.json.JSONObject(
            """
                {
                  "level":2,
                  "message":"请回到专注状态",
                  "phone_actions":["SCREEN","SOUND"],
                  "sound_volume":0.6,
                  "risk_score":0.82,
                  "phone_distraction_level":3,
                  "vision_signal":{"away":true},
                  "phone_signal":"playing",
                  "reason":"phone_playing",
                  "output_capability":{"screen_output_route":"phone","sound_output_route":"phone"}
                }
            """.trimIndent()
        )

        val alert = BoardJsonParser.parsePushAlert(data)

        assertEquals(2, alert.level)
        assertEquals(listOf("SCREEN", "SOUND"), alert.phoneActions)
        assertEquals(0.82, alert.riskScore!!, 0.001)
        assertEquals(3, alert.phoneDistractionLevel)
        assertTrue(alert.visionSignal!!.contains("away"))
        assertEquals("playing", alert.phoneSignal)
        assertEquals("phone_playing", alert.reason)
        assertEquals("phone", alert.outputCapability!!.soundOutputRoute)
    }
}
