package com.edgefocus.app.data.ble

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.async
import kotlinx.coroutines.delay
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BleRequestSchedulerTest {
    @Test
    fun `queued work is ordered by priority then fifo`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val scheduler = BleRequestScheduler(CoroutineScope(dispatcher))
        val executed = mutableListOf<String>()

        val preview = async(dispatcher) {
            scheduler.execute(HostRequestPriority.P4_PREVIEW) {
                executed += "preview"
                Result.success(byteArrayOf(4))
            }
        }
        val ack = async(dispatcher) {
            scheduler.execute(HostRequestPriority.P0_CRITICAL) {
                executed += "ack"
                Result.success(byteArrayOf(0))
            }
        }
        val state = async(dispatcher) {
            scheduler.execute(HostRequestPriority.P2_STATE) {
                executed += "state"
                Result.success(byteArrayOf(2))
            }
        }

        advanceUntilIdle()

        assertEquals(listOf("ack", "state", "preview"), executed)
        assertTrue(ack.await().isSuccess)
        assertTrue(state.await().isSuccess)
        assertTrue(preview.await().isSuccess)
    }

    @Test
    fun `queued previews can be cancelled`() = runTest {
        val dispatcher = StandardTestDispatcher(testScheduler)
        val caller = UnconfinedTestDispatcher(testScheduler)
        val scheduler = BleRequestScheduler(CoroutineScope(dispatcher))
        val preview = async(caller) {
            scheduler.execute(HostRequestPriority.P4_PREVIEW) {
                delay(1)
                Result.success(byteArrayOf())
            }
        }

        scheduler.cancelQueued(HostRequestPriority.P4_PREVIEW, "left precheck")
        advanceUntilIdle()

        assertTrue(preview.await().isFailure)
    }
}
