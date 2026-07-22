package com.edgefocus.app.data.ble

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.util.PriorityQueue
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicLong

enum class HostRequestPriority(val rank: Int) {
    P0_CRITICAL(0),
    P1_GIMBAL_CONTROL(1),
    P1_PHONE_HEARTBEAT(2),
    P2_STATE(3),
    P3_BACKGROUND(4),
    P4_PREVIEW(5)
}

class BleRequestScheduler(
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    private data class QueuedRequest(
        val priority: HostRequestPriority,
        val sequence: Long,
        val operation: suspend () -> Result<ByteArray>,
        val result: CompletableDeferred<Result<ByteArray>>
    )

    private val queueMutex = Mutex()
    private val queue = PriorityQueue<QueuedRequest>(
        compareBy<QueuedRequest> { it.priority.rank }.thenBy { it.sequence }
    )
    private val sequence = AtomicLong(0)
    private val wake = Channel<Unit>(Channel.CONFLATED)
    private var consecutiveNonPreviewRequests = 0
    private var activeRequest: QueuedRequest? = null
    private var activeJob: Deferred<Result<ByteArray>>? = null

    init {
        scope.launch {
            for (ignored in wake) {
                drain()
            }
        }
    }

    suspend fun execute(
        priority: HostRequestPriority,
        operation: suspend () -> Result<ByteArray>
    ): Result<ByteArray> {
        val deferred = CompletableDeferred<Result<ByteArray>>()
        queueMutex.withLock {
            queue += QueuedRequest(priority, sequence.incrementAndGet(), operation, deferred)
        }
        wake.trySend(Unit)
        return deferred.await()
    }

    suspend fun cancelQueued(priority: HostRequestPriority, reason: String) {
        val cancelled = mutableListOf<QueuedRequest>()
        queueMutex.withLock {
            val iterator = queue.iterator()
            while (iterator.hasNext()) {
                val request = iterator.next()
                if (request.priority == priority) {
                    iterator.remove()
                    cancelled += request
                }
            }
        }
        cancelled.forEach {
            it.result.complete(Result.failure(CancellationException(reason)))
        }
    }

    suspend fun cancelQueuedAndActive(priority: HostRequestPriority, reason: String) {
        val cancelled = mutableListOf<QueuedRequest>()
        val active = queueMutex.withLock {
            val iterator = queue.iterator()
            while (iterator.hasNext()) {
                val request = iterator.next()
                if (request.priority == priority) {
                    iterator.remove()
                    cancelled += request
                }
            }
            activeJob?.takeIf { activeRequest?.priority == priority }
        }
        cancelled.forEach {
            it.result.complete(Result.failure(CancellationException(reason)))
        }
        active?.cancel(CancellationException(reason))
    }

    private suspend fun drain() {
        while (true) {
            val request = queueMutex.withLock { pollNextLocked() } ?: return
            if (request.result.isCancelled) continue
            val operation = scope.async(start = CoroutineStart.LAZY) { request.operation() }
            queueMutex.withLock {
                activeRequest = request
                activeJob = operation
            }
            operation.start()
            val result = runCatching { operation.await() }
                .getOrElse { Result.failure(it) }
            queueMutex.withLock {
                if (activeRequest === request) {
                    activeRequest = null
                    activeJob = null
                }
            }
            request.result.complete(result)
        }
    }

    private fun pollNextLocked(): QueuedRequest? {
        if (queue.isEmpty()) return null
        val hasCriticalWork = queue.any {
            it.priority == HostRequestPriority.P0_CRITICAL ||
                it.priority == HostRequestPriority.P1_GIMBAL_CONTROL ||
                it.priority == HostRequestPriority.P1_PHONE_HEARTBEAT
        }
        val agedPreview = if (consecutiveNonPreviewRequests >= MAX_NON_PREVIEW_BURST && !hasCriticalWork) {
            queue.asSequence()
                .filter { it.priority == HostRequestPriority.P4_PREVIEW }
                .minByOrNull { it.sequence }
        } else {
            null
        }
        val request = if (agedPreview != null) {
            queue.remove(agedPreview)
            agedPreview
        } else {
            queue.poll()
        }
        if (request.priority == HostRequestPriority.P4_PREVIEW) {
            consecutiveNonPreviewRequests = 0
        } else {
            consecutiveNonPreviewRequests++
        }
        return request
    }

    private companion object {
        const val MAX_NON_PREVIEW_BURST = 2
    }
}
