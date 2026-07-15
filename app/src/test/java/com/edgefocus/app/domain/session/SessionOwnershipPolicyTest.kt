package com.edgefocus.app.domain.session

import com.edgefocus.app.data.SessionMode
import org.junit.Assert.assertEquals
import org.junit.Test

class SessionOwnershipPolicyTest {
    @Test
    fun activeOwnerCannotBeChangedByOpeningAnotherSurface() {
        assertEquals(SessionMode.LOCAL, SessionOwnershipPolicy.effectiveOwner(SessionMode.LOCAL, SessionMode.EDGEFOCUS))
        assertEquals(SessionMode.EDGEFOCUS, SessionOwnershipPolicy.effectiveOwner(SessionMode.EDGEFOCUS, SessionMode.LOCAL))
    }

    @Test
    fun idleSessionUsesRequestedOwner() {
        assertEquals(SessionMode.EDGEFOCUS, SessionOwnershipPolicy.effectiveOwner(null, SessionMode.EDGEFOCUS))
    }
}
