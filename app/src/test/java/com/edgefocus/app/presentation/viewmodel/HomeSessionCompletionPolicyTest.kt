package com.edgefocus.app.presentation.viewmodel

import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.domain.model.TimerState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HomeSessionCompletionPolicyTest {

    @Test
    fun localPausedStopShouldSaveSession() {
        assertTrue(
            HomeSessionCompletionPolicy.shouldSaveLocalOnStop(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.PAUSED,
                sessionStartTime = 123L,
                currentSessionId = "local_session",
                hasSavedCurrentSession = false
            )
        )
    }

    @Test
    fun localStopDoesNotSaveWithoutActiveSessionIdentity() {
        assertFalse(
            HomeSessionCompletionPolicy.shouldSaveLocalOnStop(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.PAUSED,
                sessionStartTime = 123L,
                currentSessionId = null,
                hasSavedCurrentSession = false
            )
        )
        assertFalse(
            HomeSessionCompletionPolicy.shouldSaveLocalOnStop(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.PAUSED,
                sessionStartTime = 0L,
                currentSessionId = "local_session",
                hasSavedCurrentSession = false
            )
        )
        assertFalse(
            HomeSessionCompletionPolicy.shouldSaveLocalOnStop(
                sessionMode = SessionMode.EDGEFOCUS,
                timerState = TimerState.PAUSED,
                sessionStartTime = 123L,
                currentSessionId = "edge_session",
                hasSavedCurrentSession = false
            )
        )
    }

    @Test
    fun stoppingActiveStatesStopsAppMonitor() {
        assertTrue(HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(TimerState.RUNNING))
        assertTrue(HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(TimerState.PAUSED))
        assertTrue(HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(TimerState.PRECHECK))
        assertFalse(HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(TimerState.IDLE))
        assertFalse(HomeSessionCompletionPolicy.shouldStopAppMonitorOnStop(TimerState.FINISHED))
    }

    @Test
    fun localSessionUsesPhoneCountAsDistractionFloorAndNullFeedback() {
        assertEquals(
            3,
            HomeSessionCompletionPolicy.distractionCountForSession(
                isEdgeFocus = false,
                boardDistractionCount = 9,
                localDistractionCount = 1,
                phoneDistractionCount = 3
            )
        )
        assertEquals(3, HomeSessionCompletionPolicy.phoneDistractionCountForSession(false, 9, 3))
        assertNull(HomeSessionCompletionPolicy.defaultFeedback())
    }

    @Test
    fun elapsedAndFocusSecondsAreNeverNegative() {
        val elapsed = HomeSessionCompletionPolicy.elapsedSeconds(
            isEdgeFocus = false,
            boardTotalDuration = 0,
            totalTimeMs = 10_000L,
            timeRemainingMs = 20_000L
        )
        assertEquals(0, elapsed)
        assertEquals(0, HomeSessionCompletionPolicy.focusSeconds(false, 0, elapsed))
    }
}
