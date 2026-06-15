package com.edgefocus.app.data.remote

import com.edgefocus.app.domain.model.AlertInfo
import com.edgefocus.app.domain.model.FocusState

interface PushEventHandler {
    fun onPushState(state: FocusState)
    fun onPushAlert(alert: AlertInfo)
    fun onPushSessionEnd(summary: String)
}
