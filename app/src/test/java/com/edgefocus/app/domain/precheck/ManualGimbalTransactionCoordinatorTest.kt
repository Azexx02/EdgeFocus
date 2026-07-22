package com.edgefocus.app.domain.precheck

import com.edgefocus.app.data.remote.BoardSessionPhase
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class ManualGimbalTransactionCoordinatorTest {
    @Test
    fun authoritativeManualLeaseRunsCommandWithoutSkip() = runTest {
        val events = mutableListOf<String>()
        val coordinator = coordinator(
            observe = {
                events += "observe"
                observation(manualLease = true)
            },
            skip = { events += "skip:$it" }
        )

        val result = coordinator.runManual("session_1") {
            events += "command:$it"
            Result.success(Unit)
        }

        assertTrue(result.isSuccess)
        assertEquals(listOf("observe", "command:session_1"), events)
    }

    @Test
    fun explicitTakeoverSkipsOnceThenManualCommandRuns() = runTest {
        val events = mutableListOf<String>()
        var manualLease = false
        var now = 0L
        val coordinator = ManualGimbalTransactionCoordinator(
            observe = {
                events += "observe"
                Result.success(observation(manualLease))
            },
            requestSkipAuto = {
                events += "skip:$it"
                manualLease = true
                Result.success(Unit)
            },
            delayMs = {
                events += "delay"
                now += it
            },
            nowMs = { now }
        )

        val takeover = coordinator.takeOver("session_1")
        val result = coordinator.runManual("session_1") {
            events += "command:$it"
            Result.success(Unit)
        }

        assertTrue(takeover.isSuccess)
        assertTrue(result.isSuccess)
        assertEquals(
            listOf("observe", "skip:session_1", "delay", "observe", "observe", "command:session_1"),
            events
        )
    }

    @Test
    fun missingLeaseDoesNotImplicitlySkipOrRunManualCommand() = runTest {
        var skips = 0
        var commands = 0
        val coordinator = ManualGimbalTransactionCoordinator(
            observe = { Result.success(observation(manualLease = false)) },
            requestSkipAuto = {
                skips += 1
                Result.success(Unit)
            }
        )

        val result = coordinator.runManual("session_1") {
            commands += 1
            Result.success(Unit)
        }

        assertTrue(result.isFailure)
        assertEquals(0, skips)
        assertEquals(0, commands)
    }

    @Test
    fun rejectedSkipDoesNotRunCommand() = runTest {
        val coordinator = ManualGimbalTransactionCoordinator(
            observe = { Result.success(observation(manualLease = false)) },
            requestSkipAuto = { Result.failure(IllegalStateException("rejected")) }
        )

        val result = coordinator.takeOver("session_1")

        assertTrue(result.isFailure)
    }

    @Test
    fun confirmationTimeoutDoesNotResendSkipOrRunCommand() = runTest {
        var now = 0L
        var skips = 0
        val coordinator = ManualGimbalTransactionCoordinator(
            observe = { Result.success(observation(manualLease = false)) },
            requestSkipAuto = {
                skips += 1
                Result.success(Unit)
            },
            delayMs = { now += it },
            nowMs = { now },
            confirmationTimeoutMs = 500L,
            pollIntervalMs = 250L
        )

        val result = coordinator.takeOver("session_1")

        assertTrue(result.isFailure)
        assertEquals(1, skips)
    }

    @Test
    fun rapidManualInputWaitsInsteadOfFailing() = runTest {
        val gate = CompletableDeferred<Unit>()
        var commands = 0
        val coordinator = coordinator(
            observe = { observation(manualLease = true) },
            skip = {}
        )

        val first = async {
            coordinator.runManual("session_1") {
                commands += 1
                gate.await()
                Result.success(Unit)
            }
        }
        runCurrent()
        val second = async {
            coordinator.runManual("session_1") {
                commands += 1
                Result.success(Unit)
            }
        }
        runCurrent()

        assertEquals(1, commands)
        gate.complete(Unit)
        assertTrue(first.await().isSuccess)
        assertTrue(second.await().isSuccess)
        assertEquals(2, commands)
    }

    @Test
    fun terminalCommandWaitsForActiveManualCommand() = runTest {
        val gate = CompletableDeferred<Unit>()
        val events = mutableListOf<String>()
        val coordinator = coordinator(
            observe = { observation(manualLease = true) },
            skip = {}
        )

        val manual = async {
            coordinator.runManual("session_1") {
                events += "manual"
                gate.await()
                Result.success(Unit)
            }
        }
        runCurrent()
        val terminal = async {
            coordinator.runCommand {
                events += "terminal"
                Result.success(Unit)
            }
        }
        runCurrent()

        assertEquals(listOf("manual"), events)
        gate.complete(Unit)
        assertTrue(manual.await().isSuccess)
        assertTrue(terminal.await().isSuccess)
        assertEquals(listOf("manual", "terminal"), events)
    }

    private fun coordinator(
        observe: suspend () -> ManualLeaseObservation,
        skip: suspend (String) -> Unit
    ) = ManualGimbalTransactionCoordinator(
        observe = { Result.success(observe()) },
        requestSkipAuto = {
            skip(it)
            Result.success(Unit)
        }
    )

    private fun observation(manualLease: Boolean) = ManualLeaseObservation(
        sessionId = "session_1",
        phase = BoardSessionPhase.PRECHECK,
        manualLease = manualLease
    )
}
