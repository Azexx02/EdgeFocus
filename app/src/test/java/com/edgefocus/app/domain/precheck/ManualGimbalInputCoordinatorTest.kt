package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.ble.model.GimbalCommandAcceptance
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ManualGimbalInputCoordinatorTest {
    @Test
    fun `ten rapid taps preserve thirty degree net target`() = runTest {
        val firstAck = CompletableDeferred<Unit>()
        val sent = mutableListOf<Pair<ManualGimbalDirection, Double>>()
        val coordinator = ManualGimbalInputCoordinator(backgroundScope) { _, direction, step ->
            sent += direction to step
            if (sent.size == 1) firstAck.await()
            Result.success(GimbalCommandAcceptance(accepted = true))
        }

        val requests = (1..10).map {
            async {
                coordinator.enqueue("session_1", 9, ManualGimbalDirection.LEFT, 3.0)
            }
        }
        runCurrent()
        firstAck.complete(Unit)
        advanceUntilIdle()

        requests.forEach { assertTrue(it.await().isSuccess) }
        assertEquals(30.0, sent.sumOf { it.second }, 0.001)
        assertTrue(sent.all { it.first == ManualGimbalDirection.LEFT })
    }

    @Test
    fun `opposite inputs are merged to net direction`() = runTest {
        val firstAck = CompletableDeferred<Unit>()
        val sent = mutableListOf<Pair<ManualGimbalDirection, Double>>()
        val coordinator = ManualGimbalInputCoordinator(backgroundScope) { _, direction, step ->
            sent += direction to step
            if (sent.size == 1) firstAck.await()
            Result.success(GimbalCommandAcceptance(accepted = true))
        }

        val requests = buildList {
            repeat(5) {
                add(async { coordinator.enqueue("session_1", 9, ManualGimbalDirection.LEFT, 3.0) })
            }
            repeat(3) {
                add(async { coordinator.enqueue("session_1", 9, ManualGimbalDirection.RIGHT, 3.0) })
            }
        }
        runCurrent()
        firstAck.complete(Unit)
        advanceUntilIdle()

        requests.forEach { assertTrue(it.await().isSuccess) }
        val signed = sent.sumOf { (direction, step) ->
            if (direction == ManualGimbalDirection.LEFT) -step else step
        }
        assertEquals(-6.0, signed, 0.001)
    }

    @Test
    fun `context change never replays old queued input`() = runTest {
        val firstAck = CompletableDeferred<Unit>()
        val sentSessions = mutableListOf<String>()
        val coordinator = ManualGimbalInputCoordinator(backgroundScope) { sessionId, _, _ ->
            sentSessions += sessionId
            if (sentSessions.size == 1) firstAck.await()
            Result.success(GimbalCommandAcceptance(accepted = true))
        }

        val oldActive = async {
            coordinator.enqueue("session_old", 1, ManualGimbalDirection.LEFT, 3.0)
        }
        runCurrent()
        val oldQueued = async {
            coordinator.enqueue("session_old", 1, ManualGimbalDirection.LEFT, 3.0)
        }
        runCurrent()
        val current = async {
            coordinator.enqueue("session_new", 2, ManualGimbalDirection.RIGHT, 3.0)
        }
        runCurrent()
        firstAck.complete(Unit)
        advanceUntilIdle()

        assertTrue(oldActive.await().isSuccess)
        assertTrue(oldQueued.await().isFailure)
        assertTrue(current.await().isSuccess)
        assertEquals(listOf("session_old", "session_new"), sentSessions)
    }
}
