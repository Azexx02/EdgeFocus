package com.edgefocus.app.presentation.viewmodel

import org.junit.Assert.assertEquals
import org.junit.Test

class BleMacAddressPolicyTest {

    @Test
    fun roomMacWinsAndLegacyOnlyMigratesWhenRoomIsBlank() {
        assertEquals(
            BleMacAddressDecision(preferredMac = "AA:BB", shouldMigrateLegacy = false),
            BleMacAddressPolicy.decide(roomMac = "AA:BB", legacyMac = "CC:DD")
        )
        assertEquals(
            BleMacAddressDecision(preferredMac = "CC:DD", shouldMigrateLegacy = true),
            BleMacAddressPolicy.decide(roomMac = "", legacyMac = "CC:DD")
        )
        assertEquals(
            BleMacAddressDecision(preferredMac = "", shouldMigrateLegacy = false),
            BleMacAddressPolicy.decide(roomMac = "", legacyMac = "")
        )
    }
}
