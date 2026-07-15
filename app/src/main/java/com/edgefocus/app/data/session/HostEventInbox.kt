package com.edgefocus.app.data.session

import android.content.Context
import android.os.SystemClock
import android.provider.Settings
import androidx.room.withTransaction
import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.ReliableEventEnvelope
import com.edgefocus.app.data.local.AppDatabase
import com.edgefocus.app.data.local.entity.HostEventEntity
import dagger.hilt.android.qualifiers.ApplicationContext
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class HostEventInbox @Inject constructor(
    private val database: AppDatabase,
    @ApplicationContext private val context: Context
) {
    suspend fun store(envelope: ReliableEventEnvelope): HostEventEntity = database.withTransaction {
        val elapsed = SystemClock.elapsedRealtime()
        val bootCount = currentBootCount()
        val wallTime = System.currentTimeMillis()
        val deadline = ReliableEventPolicy.localDeadlineElapsedMs(
            receivedElapsedMs = elapsed,
            ttlMs = envelope.ttlMs,
            remainingTtlMs = envelope.remainingTtlMs
        )
        val entity = HostEventEntity(
            eventId = envelope.eventId,
            sessionId = envelope.sessionId,
            eventType = envelope.type.command,
            payloadJson = envelope.payloadJson,
            hostCreatedAtMs = envelope.createdAtMs,
            ttlMs = envelope.ttlMs.coerceAtLeast(0L),
            receivedElapsedMs = elapsed,
            localDeadlineElapsedMs = deadline,
            receivedBootCount = bootCount,
            receivedWallTimeMs = wallTime
        )
        val dao = database.hostEventDao()
        if (dao.insertIgnore(entity) == -1L) {
            dao.refreshDeliveryWindow(
                eventId = envelope.eventId,
                payloadJson = envelope.payloadJson,
                hostCreatedAtMs = envelope.createdAtMs,
                ttlMs = envelope.ttlMs.coerceAtLeast(0L),
                receivedElapsedMs = elapsed,
                deadlineElapsedMs = deadline,
                bootCount = bootCount,
                wallTimeMs = wallTime
            )
        }
        dao.get(envelope.eventId) ?: entity
    }

    suspend fun claim(eventId: String): HostEventEntity? = database.withTransaction {
        val dao = database.hostEventDao()
        val bootCount = currentBootCount()
        val nowElapsed = SystemClock.elapsedRealtime()
        dao.expireDue(bootCount, nowElapsed)
        if (dao.claimForDispatch(eventId, bootCount, nowElapsed) == 1) dao.get(eventId) else null
    }

    suspend fun reconcileDeviceBoot() {
        val bootCount = currentBootCount()
        database.withTransaction {
            database.hostEventDao().waitForReplayAfterDeviceReboot(bootCount)
            database.hostEventDao().markStartedEffectsUncertainAfterDeviceReboot(bootCount)
            database.hostEventDao().resetClaimsAfterProcessRestart(
                bootCount,
                SystemClock.elapsedRealtime()
            )
            database.hostEventDao().markStartedEffectsUncertainAfterProcessRestart()
            database.hostEventDao().expireDue(bootCount, SystemClock.elapsedRealtime())
        }
    }

    suspend fun markEffectStarted(eventId: String) = updateState(
        eventId,
        HostEventEntity.DispatchState.EFFECT_STARTED
    )

    suspend fun markPresented(eventId: String) = updateState(
        eventId,
        HostEventEntity.DispatchState.PRESENTED
    )

    suspend fun markDeliveryUncertain(eventId: String, error: String) = updateState(
        eventId,
        HostEventEntity.DispatchState.DELIVERY_UNCERTAIN,
        error
    )

    suspend fun advanceLocalAck(eventId: String, stage: EventAckStage) {
        database.hostEventDao().advanceLocalAckStage(eventId, stage.ordinal + 1)
    }

    suspend fun confirmAck(eventId: String, stage: EventAckStage, status: String) {
        database.hostEventDao().confirmAckStage(eventId, stage.ordinal + 1, status)
    }

    suspend fun recordAckFailure(eventId: String, status: String?, error: String) {
        database.hostEventDao().recordAckFailure(eventId, status, error)
    }

    suspend fun pendingAcks(): List<HostEventEntity> = database.hostEventDao().getPendingAcks()

    private suspend fun updateState(eventId: String, state: String, error: String? = null) {
        database.hostEventDao().updateDispatchState(eventId, state, error)
    }

    private fun currentBootCount(): Int = Settings.Global.getInt(
        context.contentResolver,
        Settings.Global.BOOT_COUNT,
        0
    )
}
