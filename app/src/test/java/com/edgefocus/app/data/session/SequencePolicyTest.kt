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
}
