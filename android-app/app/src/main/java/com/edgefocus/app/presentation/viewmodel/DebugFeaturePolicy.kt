package com.edgefocus.app.presentation.viewmodel

object DebugFeaturePolicy {
    fun canShowDeveloperTools(isDebug: Boolean, revealTapCount: Int): Boolean {
        return isDebug && revealTapCount >= 7
    }

    fun shouldRegisterTestRoute(isDebug: Boolean): Boolean = isDebug

    fun canNavigateToTest(isDebug: Boolean): Boolean = isDebug
}
