package com.edgefocus.app.data

import com.edgefocus.app.domain.model.TimerState
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TimerPhoneDistractionPolicyTest {

    @Test
    fun localRunningCanIncrementPhoneDistraction() {
        assertTrue(
            TimerPhoneDistractionPolicy.shouldIncrementLocalPhoneDistraction(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.RUNNING
            )
        )
    }

    @Test
    fun edgeFocusAndInactiveLocalStatesDoNotIncrementPhoneDistraction() {
        assertFalse(
            TimerPhoneDistractionPolicy.shouldIncrementLocalPhoneDistraction(
                sessionMode = SessionMode.EDGEFOCUS,
                timerState = TimerState.RUNNING
            )
        )
        assertFalse(
            TimerPhoneDistractionPolicy.shouldIncrementLocalPhoneDistraction(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.PAUSED
            )
        )
        assertFalse(
            TimerPhoneDistractionPolicy.shouldIncrementLocalPhoneDistraction(
                sessionMode = SessionMode.LOCAL,
                timerState = TimerState.PRECHECK
            )
        )
    }
}
