package com.edgefocus.app.presentation.viewmodel

data class BleMacAddressDecision(
    val preferredMac: String,
    val shouldMigrateLegacy: Boolean
)

object BleMacAddressPolicy {
    fun decide(roomMac: String, legacyMac: String): BleMacAddressDecision {
        val normalizedRoom = roomMac.trim()
        val normalizedLegacy = legacyMac.trim()
        return when {
            normalizedRoom.isNotBlank() -> BleMacAddressDecision(normalizedRoom, shouldMigrateLegacy = false)
            normalizedLegacy.isNotBlank() -> BleMacAddressDecision(normalizedLegacy, shouldMigrateLegacy = true)
            else -> BleMacAddressDecision("", shouldMigrateLegacy = false)
        }
    }
}
