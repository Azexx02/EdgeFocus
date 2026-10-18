package com.edgefocus.app.util

object TimeUtils {

    fun formatTime(millis: Long): String {
        val totalSeconds = millis / 1000
        val minutes = totalSeconds / 60
        val seconds = totalSeconds % 60
        return "%02d:%02d".format(minutes, seconds)
    }

    fun minutesToMillis(minutes: Int): Long {
        return minutes.toLong() * 60 * 1000
    }

    fun millisToMinutes(millis: Long): Int {
        return (millis / (60 * 1000)).toInt()
    }
}
