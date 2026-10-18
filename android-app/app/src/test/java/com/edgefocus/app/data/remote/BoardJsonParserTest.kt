package com.edgefocus.app.data.remote

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.json.JSONObject

class BoardJsonParserTest {
    @Test
    fun `strict phase parser rejects unknown values`() {
        assertEquals(BoardSessionPhase.IDLE, BoardSessionPhase.fromWireOrNull("IDLE"))
        assertNull(BoardSessionPhase.fromWireOrNull("future_phase"))
        assertNull(BoardSessionPhase.fromWireOrNull(""))
    }

    @Test
    fun `empty structured precheck guidance is not exposed as raw json`() {
        val state = BoardJsonParser.parseSessionState(
            JSONObject(
                """{"runtime_snapshot":{"session":{"phase":"PRECHECK","preparation_state":"READY"}},"precheck_guidance":{"distance_hint":"","pose_hint":"","frame_quality_reason":"","ready":false}}"""
            )
        )

        assertNull(state.precheckGuidance)
    }


    @Test
    fun buildSessionControlStartIncludesTargetDuration() {
        val json = BoardJsonParser.buildSessionControl(
            "start",
            targetDurationMin = 25,
            operationId = "android-123-1"
        )

        assertEquals("session_control", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertEquals("start", data.getString("action"))
        assertEquals(25, data.getInt("target_duration_min"))
        assertEquals("android-123-1", data.getString("operation_id"))
    }

    @Test
    fun buildSessionControlExitBindsExpectedSession() {
        val json = BoardJsonParser.buildSessionControl(
            action = "end",
            sessionId = "session_1"
        )

        val data = json.getJSONObject("data")
        assertEquals("end", data.getString("action"))
        assertEquals("session_1", data.getString("session_id"))
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
                  "notification_mode":"silent",
                  "power_profile":"go",
                  "away_timeout_min":12,
                  "default_focus_duration_min":45,
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
        assertEquals(NotificationMode.SILENT, prefs.notificationMode)
        assertEquals(PowerProfile.GO, prefs.powerProfile)
        assertEquals(12, prefs.awayTimeoutMin)
        assertEquals(45, prefs.defaultFocusDurationMin)
        assertEquals(0.35f, prefs.soundVolume, 0.001f)
        assertEquals(setOf("screen", "band", "sound"), prefs.normalChannels)
        assertEquals(setOf("screen", "band"), prefs.silentChannels)
        assertEquals("phone", prefs.screenOutputRoute)
        assertEquals("board", prefs.soundOutputRoute)
        assertFalse(prefs.gimbalEnabled)
    }

    @Test
    fun parsePreferencesKeepsLegacySystemModeCompatibilityWithoutOverridingNewFields() {
        val explicit = BoardJsonParser.parsePreferences(
            org.json.JSONObject(
                """
                    {
                      "system_mode":0,
                      "notification_mode":"silent",
                      "power_profile":"home"
                    }
                """.trimIndent()
            )
        )

        assertEquals(NotificationMode.SILENT, explicit.notificationMode)
        assertEquals(PowerProfile.HOME, explicit.powerProfile)

        val legacySilent = BoardJsonParser.parsePreferences(org.json.JSONObject("""{"system_mode":1}"""))
        assertEquals(NotificationMode.SILENT, legacySilent.notificationMode)
        assertEquals(PowerProfile.GO, legacySilent.powerProfile)

        val legacyHome = BoardJsonParser.parsePreferences(org.json.JSONObject("""{"system_mode":2}"""))
        assertEquals(NotificationMode.NORMAL, legacyHome.notificationMode)
        assertEquals(PowerProfile.HOME, legacyHome.powerProfile)
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
    fun parseSessionStateDistinguishesMissingRemainingFromExplicitZero() {
        val explicitZero = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "session_phase":"focusing",
                    "remaining_ms":0,
                    "target_duration_ms":1500000
                  }
                }
            """.trimIndent()
        )

        assertTrue(explicitZero.hasRemainingMs)
        assertEquals(0L, explicitZero.remainingMs)
        assertTrue(explicitZero.hasTargetDurationMs)
        assertEquals(1_500_000L, explicitZero.targetDurationMs)

        val missing = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{"session_phase":"focusing"}
                }
            """.trimIndent()
        )

        assertFalse(missing.hasRemainingMs)
        assertFalse(missing.hasTargetDurationMs)
    }

    @Test
    fun idleSnapshotWithExplicitZeroDoesNotRepresentActiveRuntime() {
        val state = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"session_phase":"IDLE","active":false,"remaining_ms":0,"target_duration_ms":0}}"""
        )

        assertEquals(BoardSessionPhase.IDLE, state.sessionPhase)
        assertTrue(state.hasRemainingMs)
        assertFalse(state.representsActiveRuntime())
        assertEquals(BoardSessionPhase.IDLE, state.effectivePhase())
    }

    @Test
    fun uppercasePrecheckRemainsAnActiveRuntime() {
        val state = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"session_phase":"PRECHECK","active":false,"remaining_ms":0}}"""
        )

        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertTrue(state.representsActiveRuntime())
        assertEquals(BoardSessionPhase.PRECHECK, state.effectivePhase())
    }

    @Test
    fun runtimeSnapshotPrecheckOverridesIdleCompatibilityProjection() {
        val state = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "success":true,
                    "session_phase":"idle",
                    "active":false,
                    "remaining_ms":0,
                    "target_duration_ms":0,
                    "runtime_snapshot":{
                      "session":{
                        "id":"session_runtime_1",
                        "phase":"PRECHECK",
                        "active":false,
                        "runtime_active":true,
                        "elapsed_ms":0,
                        "remaining_ms":1500000
                      }
                    }
                  }
                }
            """.trimIndent()
        )

        assertEquals("session_runtime_1", state.sessionId)
        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertEquals(1_500_000L, state.remainingMs)
        assertTrue(state.hasRemainingMs)
        assertFalse(state.active)
        assertFalse(state.calibrationCompleted)
        assertTrue(state.representsActiveRuntime())
    }

    @Test
    fun runtimeSnapshotParsesInitializingPreparationCorrelation() {
        val state = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "runtime_snapshot":{
                      "session":{
                        "id":"session_prepare_1",
                        "phase":"PRECHECK",
                        "remaining_ms":1500000,
                        "preparation_state":"INITIALIZING",
                        "start_generation":8,
                        "operation_id":"android-123-8"
                      }
                    }
                  }
                }
            """.trimIndent()
        )

        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertEquals(BoardPreparationState.INITIALIZING, state.preparationState)
        assertEquals(8L, state.startGeneration)
        assertEquals("android-123-8", state.operationId)
    }

    @Test
    fun legacyPrecheckWithoutPreparationStateDefaultsReady() {
        val state = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"session_phase":"precheck"}}"""
        )

        assertEquals(BoardPreparationState.READY, state.preparationState)
    }

    @Test
    fun unknownPreparationStateStaysInitializing() {
        val state = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"runtime_snapshot":{"session":{"phase":"PRECHECK","preparation_state":"FUTURE_VALUE"}}}}"""
        )

        assertEquals(BoardPreparationState.INITIALIZING, state.preparationState)
    }

    @Test
    fun responseEnvelopeErrorIsReturnedBeforeSessionParsing() {
        assertEquals(
            "runtime unavailable",
            BoardJsonParser.parseResponseError(
                """{"status":"error","error":"runtime unavailable"}"""
            )
        )
        assertNull(BoardJsonParser.parseResponseError("""{"status":"success","data":{}}"""))
    }

    @Test
    fun runtimeSnapshotFocusingOverridesStalePrecheckProjection() {
        val state = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "session_phase":"precheck",
                    "paused":true,
                    "calibration_completed":false,
                    "runtime_snapshot":{
                      "session":{
                        "id":"session_runtime_2",
                        "phase":"FOCUSING",
                        "active":true,
                        "runtime_active":true,
                        "elapsed_ms":5000,
                        "remaining_ms":1495000
                      }
                    }
                  }
                }
            """.trimIndent()
        )

        assertEquals(BoardSessionPhase.FOCUSING, state.sessionPhase)
        assertTrue(state.active)
        assertFalse(state.paused)
        assertTrue(state.calibrationCompleted)
        assertEquals(5_000L, state.elapsedMs)
        assertEquals(1_495_000L, state.remainingMs)
    }

    @Test
    fun runtimeSnapshotIdleOverridesStalePausedProjection() {
        val state = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "session_phase":"paused",
                    "paused":true,
                    "runtime_snapshot":{
                      "session":{
                        "phase":"IDLE",
                        "active":false,
                        "runtime_active":false,
                        "elapsed_ms":0,
                        "remaining_ms":0
                      }
                    }
                  }
                }
            """.trimIndent()
        )

        assertEquals(BoardSessionPhase.IDLE, state.sessionPhase)
        assertFalse(state.paused)
        assertEquals(BoardSessionPhase.IDLE, state.effectivePhase())
    }

    @Test
    fun parseSessionStateIncludesStartSourceAndPrecheckControl() {
        val state = BoardJsonParser.parseSessionState(
            """
                {
                  "status":"success",
                  "data":{
                    "active":true,
                    "session_phase":"precheck",
                    "session_start_source":"board",
                    "precheck_control":{
                      "owner":"board",
                      "app_can_skip_precheck":false,
                      "board_can_continue":true
                    }
                  }
                }
            """.trimIndent()
        )

        assertEquals("board", state.sessionStartSource)
        assertEquals("board", state.precheckControl!!.owner)
        assertFalse(state.precheckControl!!.appCanSkipPrecheck)
        assertTrue(state.precheckControl!!.boardCanContinue)
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
                "focus_state":3,
                "phone_signal":true,
                "phone_usage_state":"playing",
                "phone_distraction_level":2,
                "risk_score":50,
                "battery_percent":88,
                "battery_charging":true,
                "power_source":"external",
                "power_profile":"home",
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
        assertEquals(3, state.focusState)
        assertTrue(state.phoneSignal)
        assertEquals("playing", state.phoneUsageState)
        assertEquals(2, state.phoneDistractionLevel)
        assertEquals(50, state.riskScore)
        assertEquals(88, state.batteryPercent)
        assertTrue(state.batteryCharging)
        assertEquals("external", state.powerSource)
        assertEquals(PowerProfile.HOME, state.powerProfile)
        assertEquals(30_000L, state.elapsedMs)
        assertTrue(state.precheckGuidance!!.contains("lean_left"))
        assertTrue(state.cameraPreview!!.available)
        assertTrue(state.reminder!!.contains("cooling_ms"))
        assertEquals("phone", state.outputCapability!!.screenOutputRoute)
        assertEquals(listOf("screen", "sound"), state.outputCapability!!.lastAttemptedChannels)
        assertTrue(state.focusReason!!.contains("phone"))
    }

    @Test
    fun parseSessionStateDefaultsFailedBatteryReadToSixtySeven() {
        val missingBattery = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"session_phase":"focusing"}}"""
        )
        val invalidBattery = BoardJsonParser.parseSessionState(
            """{"status":"success","data":{"battery_percent":200,"power_profile":"go"}}"""
        )

        assertEquals(67, missingBattery.batteryPercent)
        assertEquals(67, invalidBattery.batteryPercent)
        assertEquals(PowerProfile.GO, invalidBattery.powerProfile)
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

    @Test
    fun parseTimerCompleteUsesFocusDurationAndFullReportFields() {
        val eventData = org.json.JSONObject(
            """
                {
                  "session_id":"board_1",
                  "start_time_ms":1000,
                  "end_time_ms":61000,
                  "total_duration":60,
                  "focus_duration":42,
                  "elapsed_ms":60000,
                  "target_duration_ms":60000,
                  "distraction_count":3,
                  "reminder_count":2,
                  "phone_distraction_count":1,
                  "end_reason":"timer_completed"
                }
            """.trimIndent()
        )

        val complete = BoardJsonParser.parseTimerComplete(eventData)

        assertEquals("board_1", complete.sessionId)
        assertEquals(1000L, complete.startTimeMs)
        assertEquals(61000L, complete.endTimeMs)
        assertEquals(60, complete.totalDurationSec)
        assertEquals(42, complete.focusDurationSec)
        assertEquals(60_000L, complete.elapsedMs)
        assertEquals(60_000L, complete.targetDurationMs)
        assertEquals(1, complete.phoneDistractionCount)
        assertEquals("timer_completed", complete.endReason)
    }

    @Test
    fun buildPhoneSyncIncludesOnlyRawHostFact() {
        val json = BoardJsonParser.buildPhoneSync(
            PhoneUsage(
                isInUse = true,
                appCategory = "learning",
                usageState = "learning",
                sessionId = "session_1",
                sequence = 4,
                timestampMs = 1000,
                distractionLevel = 0,
                appName = "com.edgefocus.app"
            )
        )

        assertEquals("phone_sync", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertTrue(data.getBoolean("is_in_use"))
        assertEquals("learning", data.getString("usage_state"))
        assertEquals("session_1", data.getString("session_id"))
        assertEquals(4L, data.getLong("sequence"))
        assertEquals(1000L, data.getLong("timestamp_ms"))
        assertFalse(data.has("app_category"))
        assertFalse(data.has("distraction_level"))
        assertFalse(data.has("app_name"))
    }

    @Test
    fun buildUploadLocalSessionUsesDedicatedCommand() {
        val json = BoardJsonParser.buildUploadLocalSession(
            LocalSessionUpload(
                sessionId = "app_local_1",
                startTimeMs = 1000L,
                endTimeMs = 61000L,
                totalDurationSec = 60,
                focusDurationSec = 55,
                distractionCount = 2,
                reminderCount = 1,
                phoneDistractionCount = 2
            )
        )

        assertEquals("upload_local_session", json.getString("cmd"))
        val data = json.getJSONObject("data")
        assertEquals("app_local_1", data.getString("session_id"))
        assertEquals(1000L, data.getLong("start_time_ms"))
        assertEquals(61000L, data.getLong("end_time_ms"))
        assertEquals(60, data.getInt("total_duration"))
        assertEquals(55, data.getInt("focus_duration"))
        assertEquals("app_local", data.getString("source"))
    }

    @Test
    fun parseSessionStateFallsBackToRuntimeGimbalWhenPrecheckGimbalIsMissing() {
        val response = """
            {
              "status":"success",
              "data":{
                "precheck":{"camera":{"required":true}},
                "runtime_snapshot":{
                  "session":{"id":"session_1","phase":"PRECHECK","preparation_state":"READY"},
                  "gimbal":{
                    "available":true,
                    "degraded":false,
                    "health_status":"ready",
                    "angle_x":315.0,
                    "angle_y":353.0,
                    "state":"manual",
                    "manual_lease":true,
                    "detail":"search_timeout_manual"
                  }
                }
              }
            }
        """.trimIndent()

        val state = BoardJsonParser.parseSessionState(response)
        val gimbal = state.precheck?.gimbal

        assertTrue(gimbal!!.enabled)
        assertTrue(gimbal.online)
        assertTrue(gimbal.safe)
        assertEquals(315.0, gimbal.angleX!!, 0.001)
        assertEquals(353.0, gimbal.angleY!!, 0.001)
        assertEquals("manual", gimbal.state)
        assertTrue(gimbal.manualLease)
        assertEquals("search_timeout_manual", gimbal.detail)
    }

    @Test
    fun parseSessionStateMergesRuntimeLeaseIntoPrecheckGimbal() {
        val response = """
            {
              "status":"success",
              "data":{
                "precheck":{"gimbal":{"enabled":true,"online":true,"safe":true}},
                "runtime_snapshot":{
                  "session":{"id":"session_2","phase":"PRECHECK","preparation_state":"READY"},
                  "gimbal":{"state":"manual","manual_lease":true,"detail":"automatic_search_skipped"}
                }
              }
            }
        """.trimIndent()

        val gimbal = BoardJsonParser.parseSessionState(response).precheck?.gimbal

        assertNotNull(gimbal)
        assertEquals("manual", gimbal!!.state)
        assertTrue(gimbal.manualLease)
        assertEquals("automatic_search_skipped", gimbal.detail)
    }

    @Test
    fun parseSessionStateInfersManualAvailabilityFromRuntimeDetail() {
        val response = """
            {
              "status":"success",
              "data":{
                "precheck":{"gimbal":{"enabled":true,"online":true,"safe":true}},
                "runtime_snapshot":{
                  "session":{"id":"session_2","phase":"PRECHECK","preparation_state":"READY"},
                  "gimbal":{"state":"idle","manual_lease":false,"detail":"automatic_search_skipped_manual_available"}
                }
              }
            }
        """.trimIndent()

        val gimbal = BoardJsonParser.parseSessionState(response).precheck?.gimbal

        assertNotNull(gimbal)
        assertFalse(gimbal!!.manualLease)
        assertTrue(gimbal.manualControlAvailable)
    }

    @Test
    fun `runtime gimbal controls manual enabled wins without lease`() {
        val response = """
            {
              "status":"success",
              "data":{
                "runtime_snapshot":{
                  "session":{"id":"session_controls","phase":"PRECHECK","preparation_state":"READY"},
                  "gimbal":{
                    "available":true,
                    "state":"manual_available",
                    "manual_lease":false,
                    "controls":{"manual_enabled":true}
                  }
                }
              }
            }
        """.trimIndent()

        val gimbal = BoardJsonParser.parseSessionState(response).precheck?.gimbal

        assertNotNull(gimbal)
        assertFalse(gimbal!!.manualLease)
        assertTrue(gimbal.manualControlAvailable)
    }

    @Test
    fun `runtime gimbal controls expose skip button during search`() {
        val response = """
            {
              "status":"success",
              "data":{
                "runtime_snapshot":{
                  "session":{"id":"session_search","phase":"PRECHECK","preparation_state":"READY"},
                  "gimbal":{
                    "available":true,
                    "state":"searching",
                    "manual_lease":false,
                    "search_completed":false,
                    "controls":{
                      "manual_enabled":false,
                      "skip_search_enabled":true
                    }
                  }
                }
              }
            }
        """.trimIndent()

        val gimbal = BoardJsonParser.parseSessionState(response).precheck?.gimbal

        assertNotNull(gimbal)
        assertFalse(gimbal!!.manualControlAvailable)
        assertFalse(gimbal.searchCompleted)
        assertTrue(gimbal.skipSearchEnabled)
    }

    @Test
    fun `push gimbal precheck state maps to precheck snapshot without full state`() {
        val state = BoardJsonParser.parseGimbalPrecheckState(
            org.json.JSONObject(
                """
                    {
                      "session_id":"session_push",
                      "state":"manual_available",
                      "degraded":false,
                      "health_status":"healthy",
                      "search_completed":true,
                      "controls":{
                        "manual_enabled":true,
                        "skip_search_enabled":false
                      }
                    }
                """.trimIndent()
            )
        )
        val gimbal = state.precheck?.gimbal

        assertEquals(BoardSessionPhase.PRECHECK, state.sessionPhase)
        assertEquals("session_push", state.sessionId)
        assertTrue(state.partialGimbalPrecheckPush)
        assertNotNull(gimbal)
        assertTrue(gimbal!!.manualControlAvailable)
        assertTrue(gimbal.searchCompleted)
        assertFalse(gimbal.skipSearchEnabled)
    }

    @Test
    fun `new gimbal fields win while legacy angles remain available`() {
        val response = """
            {"status":"success","data":{
              "runtime_snapshot":{
                "session":{"id":"session_v2","phase":"PRECHECK","preparation_state":"READY"},
                "gimbal":{
                  "available":true,"state":"moving","control_mode":"manual",
                  "communication_health":"healthy","hardware_fault_active":false,
                  "current_yaw_deg":359.4,"current_pitch_deg":2.5,
                  "target_yaw_deg":1.4,"target_pitch_deg":2.5,
                  "motion_generation":53,"settled":false,"manual_lease":true,
                  "calibration_valid":true,"origin_valid":true,
                  "calibration_epoch":7,"origin_epoch":3
                }
              }
            }}
        """.trimIndent()

        val gimbal = BoardJsonParser.parseSessionState(response).precheck?.gimbal!!

        assertEquals("manual", gimbal.controlMode)
        assertEquals("healthy", gimbal.communicationHealth)
        assertEquals(359.4, gimbal.currentYawDeg!!, 0.001)
        assertEquals(2.5, gimbal.angleX!!, 0.001)
        assertEquals(53L, gimbal.motionGeneration)
        assertFalse(gimbal.settled!!)
        assertTrue(gimbal.originValid!!)
    }
}
