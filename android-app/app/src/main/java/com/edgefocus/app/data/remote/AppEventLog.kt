package com.edgefocus.app.data.remote

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import javax.inject.Inject
import javax.inject.Singleton

data class AppEvent(
    val timestamp: Long = System.currentTimeMillis(),
    val type: String,
    val message: String
)

@Singleton
class AppEventLog @Inject constructor() {

    private val _events = MutableStateFlow<List<AppEvent>>(emptyList())
    val events: StateFlow<List<AppEvent>> = _events.asStateFlow()

    fun log(type: String, message: String) {
        _events.update { it + AppEvent(type = type, message = message) }
    }

    fun clear() {
        _events.value = emptyList()
    }
}
