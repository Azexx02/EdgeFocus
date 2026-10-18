package com.edgefocus.app.data.session

import org.junit.Assert.assertEquals
import org.junit.Test

class SequencePolicyTest {
    @Test
    fun nextSequenceAdvancesPastEveryKnownAuthority() {
        assertEquals(13L, SequencePolicy.next(8L, 12L, 10L))
        assertEquals(1L, SequencePolicy.next(null, null, null))
    }

    @Test
    fun invalidNegativeInputsCannotMoveSequenceBackwards() {
        assertEquals(6L, SequencePolicy.next(5L, -1L, null))
    }

    @Test
    fun gimbalSequenceUsesWallClockToJumpPastLostCheckpoint() {
        assertEquals(
            1_784_293_529_472L,
            GimbalSequencePolicy.next(
                localCheckpoint = 2L,
                hostSequence = null,
                nowMs = 1_784_293_529_471L
            )
        )
    }

    @Test
    fun gimbalSequenceDoesNotMoveBackwardsWhenClockRollsBack() {
        assertEquals(
            10_001L,
            GimbalSequencePolicy.next(
                localCheckpoint = 10_000L,
                hostSequence = 9_000L,
                nowMs = 8_000L
            )
        )
    }
}
