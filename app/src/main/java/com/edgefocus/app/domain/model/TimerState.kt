package com.edgefocus.app.domain.model

enum class TimerState {
    IDLE,
    RUNNING,
    PAUSED,
    BREAK,
    FINISHED
}

enum class SessionType {
    WORK,
    SHORT_BREAK,
    LONG_BREAK
}