package com.edgefocus.app.presentation.viewmodel

import com.edgefocus.app.domain.model.TimerState
import org.junit.Assert.assertEquals
import org.junit.Test

class HomeStartActionPolicyTest {

    @Test
    fun precheckStartButtonSkipsPrecheckInsteadOfStartingNewSession() {
        assertEquals(
            HomeStartAction.SKIP_PRECHECK,
            HomeStartActionPolicy.decide(TimerState.PRECHECK, useBoard = true)
        )
    }

    @Test
    fun idleUsesSelectedModeAndPausedResumes() {
        assertEquals(HomeStartAction.START_EDGEFOCUS, HomeStartActionPolicy.decide(TimerState.IDLE, true))
        assertEquals(HomeStartAction.START_LOCAL, HomeStartActionPolicy.decide(TimerState.IDLE, false))
        assertEquals(HomeStartAction.RESUME, HomeStartActionPolicy.decide(TimerState.PAUSED, true))
    }
}
