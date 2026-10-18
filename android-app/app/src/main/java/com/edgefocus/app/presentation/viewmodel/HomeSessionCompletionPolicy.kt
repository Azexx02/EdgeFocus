package com.edgefocus.app.presentation.viewmodel

import com.edgefocus.app.data.SessionMode
import com.edgefocus.app.domain.model.TimerState

object HomeSessionCompletionPolicy {
    fun shouldSaveLocalOnStop(
        sessionMode: SessionMode,
        timerState: TimerState,
        sessionStartTime: Long,
        currentSessionId: String?,
        hasSavedCurrentSession: Boolean
    ): Boolean {
        return sessionMode == SessionMode.LOCAL &&
            (timerState == TimerState.RUNNING || timerState == TimerState.PAUSED) &&
            sessionStartTime > 0L &&
            currentSessionId != null &&
            !hasSavedCurrentSession
    }

    fun shouldStopAppMonitorOnStop(timerState: TimerState): Boolean {
        return timerState == TimerState.RUNNING ||
            timerState == TimerState.PAUSED ||
            timerState == TimerState.PRECHECK
    }

    fun elapsedSeconds(
        isEdgeFocus: Boolean,
        boardTotalDuration: Int,
        totalTimeMs: Long,
        timeRemainingMs: Long
    ): Int {
        val value = if (isEdgeFocus && boardTotalDuration > 0) {
            boardTotalDuration
        } else {
            ((totalTimeMs - timeRemainingMs) / 1000L).toInt()
        }
        return value.coerceAtLeast(0)
    }

    fun focusSeconds(isEdgeFocus: Boolean, boardFocusDuration: Int, elapsedSeconds: Int): Int {
        return if (isEdgeFocus && boardFocusDuration > 0) {
            boardFocusDuration.coerceAtLeast(0)
        } else {
            elapsedSeconds.coerceAtLeast(0)
        }
    }

    fun distractionCountForSession(
        isEdgeFocus: Boolean,
        boardDistractionCount: Int,
        localDistractionCount: Int,
        phoneDistractionCount: Int
    ): Int {
        return if (isEdgeFocus) {
            boardDistractionCount.coerceAtLeast(0)
        } else {
            maxOf(localDistractionCount, phoneDistractionCount, 0)
        }
    }

    fun phoneDistractionCountForSession(
        isEdgeFocus: Boolean,
        boardPhoneDistractionCount: Int,
        localPhoneDistractionCount: Int
    ): Int {
        return if (isEdgeFocus) {
            boardPhoneDistractionCount.coerceAtLeast(0)
        } else {
            localPhoneDistractionCount.coerceAtLeast(0)
        }
    }

    fun defaultFeedback(): String? = null
}
