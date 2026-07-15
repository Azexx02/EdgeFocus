package com.edgefocus.app.data.local.dao

import androidx.room.Dao
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.Query
import com.edgefocus.app.data.local.entity.HostEventEntity

@Dao
interface HostEventDao {
    @Insert(onConflict = OnConflictStrategy.IGNORE)
    suspend fun insertIgnore(event: HostEventEntity): Long

    @Query("SELECT * FROM host_event_inbox WHERE eventId = :eventId")
    suspend fun get(eventId: String): HostEventEntity?

    @Query(
        """
        UPDATE host_event_inbox
        SET payloadJson = :payloadJson,
            hostCreatedAtMs = :hostCreatedAtMs,
            ttlMs = :ttlMs,
            receivedElapsedMs = :receivedElapsedMs,
            localDeadlineElapsedMs = :deadlineElapsedMs,
            receivedBootCount = :bootCount,
            receivedWallTimeMs = :wallTimeMs,
            dispatchState = CASE
                WHEN dispatchState = 'WAITING_REPLAY' THEN 'PENDING'
                ELSE dispatchState
            END,
            lastError = CASE
                WHEN dispatchState = 'WAITING_REPLAY' THEN NULL
                ELSE lastError
            END
        WHERE eventId = :eventId
        """
    )
    suspend fun refreshDeliveryWindow(
        eventId: String,
        payloadJson: String,
        hostCreatedAtMs: Long?,
        ttlMs: Long,
        receivedElapsedMs: Long,
        deadlineElapsedMs: Long,
        bootCount: Int,
        wallTimeMs: Long
    )

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'CLAIMED'
        WHERE eventId = :eventId
          AND dispatchState = 'PENDING'
          AND receivedBootCount = :bootCount
          AND localDeadlineElapsedMs > :nowElapsedMs
        """
    )
    suspend fun claimForDispatch(eventId: String, bootCount: Int, nowElapsedMs: Long): Int

    @Query("UPDATE host_event_inbox SET dispatchState = :state, lastError = :error WHERE eventId = :eventId")
    suspend fun updateDispatchState(eventId: String, state: String, error: String? = null)

    @Query(
        """
        UPDATE host_event_inbox
        SET localAckStage = CASE WHEN localAckStage < :stage THEN :stage ELSE localAckStage END
        WHERE eventId = :eventId
        """
    )
    suspend fun advanceLocalAckStage(eventId: String, stage: Int)

    @Query(
        """
        UPDATE host_event_inbox
        SET confirmedAckStage = CASE WHEN confirmedAckStage < :stage THEN :stage ELSE confirmedAckStage END,
            lastAckStatus = :ackStatus,
            lastError = NULL
        WHERE eventId = :eventId
        """
    )
    suspend fun confirmAckStage(eventId: String, stage: Int, ackStatus: String)

    @Query("UPDATE host_event_inbox SET lastAckStatus = :ackStatus, lastError = :error WHERE eventId = :eventId")
    suspend fun recordAckFailure(eventId: String, ackStatus: String?, error: String)

    @Query(
        """
        SELECT * FROM host_event_inbox
        WHERE confirmedAckStage < localAckStage
        ORDER BY receivedWallTimeMs ASC
        """
    )
    suspend fun getPendingAcks(): List<HostEventEntity>

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'EXPIRED'
        WHERE receivedBootCount = :bootCount
          AND localDeadlineElapsedMs <= :nowElapsedMs
          AND dispatchState IN ('PENDING', 'CLAIMED')
        """
    )
    suspend fun expireDue(bootCount: Int, nowElapsedMs: Long)

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'WAITING_REPLAY', lastError = 'device_reboot_waiting_host_replay'
        WHERE receivedBootCount != :currentBootCount
          AND dispatchState IN ('PENDING', 'CLAIMED')
        """
    )
    suspend fun waitForReplayAfterDeviceReboot(currentBootCount: Int)

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'DELIVERY_UNCERTAIN', lastError = 'device_reboot_after_effect_started'
        WHERE receivedBootCount != :currentBootCount
          AND dispatchState = 'EFFECT_STARTED'
        """
    )
    suspend fun markStartedEffectsUncertainAfterDeviceReboot(currentBootCount: Int)

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'PENDING', lastError = 'process_restart_before_effect'
        WHERE receivedBootCount = :currentBootCount
          AND localDeadlineElapsedMs > :nowElapsedMs
          AND dispatchState = 'CLAIMED'
        """
    )
    suspend fun resetClaimsAfterProcessRestart(currentBootCount: Int, nowElapsedMs: Long)

    @Query(
        """
        UPDATE host_event_inbox
        SET dispatchState = 'DELIVERY_UNCERTAIN', lastError = 'process_restart_after_effect_started'
        WHERE dispatchState = 'EFFECT_STARTED'
        """
    )
    suspend fun markStartedEffectsUncertainAfterProcessRestart()
}
