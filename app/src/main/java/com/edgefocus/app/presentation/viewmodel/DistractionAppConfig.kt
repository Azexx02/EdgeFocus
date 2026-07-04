package com.edgefocus.app.presentation.viewmodel

object DistractionAppConfig {
    const val PREFS_NAME = "distraction_app_prefs"
    const val LEVELS_KEY = "distraction_levels"

    fun parseLevels(serialized: String): Map<String, Int> {
        if (serialized.isBlank()) return emptyMap()
        return serialized.split(";")
            .asSequence()
            .filter { it.isNotBlank() }
            .mapNotNull { entry ->
                val parts = entry.split("=", limit = 2)
                val packageName = parts.getOrNull(0)?.takeIf { it.isNotBlank() } ?: return@mapNotNull null
                val level = parts.getOrNull(1)?.toIntOrNull()?.coerceIn(0, 3) ?: 3
                packageName to level
            }
            .toMap()
    }

    fun serializeLevels(levels: Map<String, Int>): String {
        return levels.entries
            .filter { it.key.isNotBlank() }
            .joinToString(";") { "${it.key}=${it.value.coerceIn(0, 3)}" }
    }
}
