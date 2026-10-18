package com.edgefocus.app.presentation.viewmodel

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DebugFeaturePolicyTest {

    @Test
    fun releaseNeverShowsDeveloperToolsOrTestRoute() {
        assertFalse(DebugFeaturePolicy.canShowDeveloperTools(isDebug = false, revealTapCount = 99))
        assertFalse(DebugFeaturePolicy.shouldRegisterTestRoute(isDebug = false))
        assertFalse(DebugFeaturePolicy.canNavigateToTest(isDebug = false))
    }

    @Test
    fun debugRequiresRevealTapsForDeveloperTools() {
        assertFalse(DebugFeaturePolicy.canShowDeveloperTools(isDebug = true, revealTapCount = 6))
        assertTrue(DebugFeaturePolicy.canShowDeveloperTools(isDebug = true, revealTapCount = 7))
        assertTrue(DebugFeaturePolicy.shouldRegisterTestRoute(isDebug = true))
        assertTrue(DebugFeaturePolicy.canNavigateToTest(isDebug = true))
    }
}
