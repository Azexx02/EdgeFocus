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
    fun `ten rapid taps remain ten host fine-step commands`() = runTest {
        val firstAck = CompletableDeferred<Unit>()
        val sent = mutableListOf<Pair<ManualGimbalDirection, Boolean>>()
        val coordinator = ManualGimbalInputCoordinator(backgroundScope) { _, direction, fineTune ->
            sent += direction to fineTune
            if (sent.size == 1) firstAck.await()
            Result.success(GimbalCommandAcceptance(accepted = true))
        }

        val requests = (1..10).map {
            async { coordinator.enqueue("session_1", 9, ManualGimbalDirection.LEFT, fineTune = true) }
        }
        runCurrent()
        firstAck.complete(Unit)
        advanceUntilIdle()

        requests.forEach { assertTrue(it.await().isSuccess) }
        assertEquals(List(10) { ManualGimbalDirection.LEFT to true }, sent)
    }

    @Test
    fun `opposite inputs preserve ordered host intent`() = runTest {
        val sent = mutableListOf<Pair<ManualGimbalDirection, Boolean>>()
        val coordinator = ManualGimbalInputCoordinator(backgroundScope) { _, direction, fineTune ->
            sent += direction to fineTune
            Result.success(GimbalCommandAcceptance(accepted = true))
        }

        val requests = listOf(
            async { coordinator.enqueue("session_1", 9, ManualGimbalDirection.LEFT, fineTune = true) },
            async { coordinator.enqueue("session_1", 9, ManualGimbalDirection.RIGHT, fineTune = false) }
        )
        advanceUntilIdle()

        requests.forEach { assertTrue(it.await().isSuccess) }
        assertEquals(
            listOf(ManualGimbalDirection.LEFT to true, ManualGimbalDirection.RIGHT to false),
            sent
        )
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
            coordinator.enqueue("session_old", 1, ManualGimbalDirection.LEFT, fineTune = true)
        }
        runCurrent()
        val oldQueued = async {
            coordinator.enqueue("session_old", 1, ManualGimbalDirection.LEFT, fineTune = true)
        }
        runCurrent()
        val current = async {
            coordinator.enqueue("session_new", 2, ManualGimbalDirection.RIGHT, fineTune = false)
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
