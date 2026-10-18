package com.edgefocus.app.presentation.shared

import com.edgefocus.app.domain.model.TimerState
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BlackScreenSaverPolicyTest {

    @Test
    fun enabledSettingOnlyAppliesToFormalSessionStates() {
        assertFalse(shouldEnableBlackScreenSaver(true, TimerState.IDLE))
        assertFalse(shouldEnableBlackScreenSaver(true, TimerState.PRECHECK))
        assertTrue(shouldEnableBlackScreenSaver(true, TimerState.RUNNING))
        assertTrue(shouldEnableBlackScreenSaver(true, TimerState.PAUSED))
        assertFalse(shouldEnableBlackScreenSaver(true, TimerState.FINISHED))
    }

    @Test
    fun disabledSettingStaysDisabledDuringSession() {
        assertFalse(shouldEnableBlackScreenSaver(false, TimerState.RUNNING))
        assertFalse(shouldEnableBlackScreenSaver(false, TimerState.PAUSED))
    }
}
