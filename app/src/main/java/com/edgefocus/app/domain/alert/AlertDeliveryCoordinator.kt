package com.edgefocus.app.domain.alert

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat
import com.edgefocus.app.MainActivity
import com.edgefocus.app.data.TimerStateHolder
import com.edgefocus.app.data.ble.ConnectionSupervisor
import com.edgefocus.app.data.ble.HostJsonCodec
import com.edgefocus.app.data.ble.model.EventAckStage
import com.edgefocus.app.data.ble.model.HostConnectionPhase
import com.edgefocus.app.data.ble.model.ReliableEventEnvelope
import com.edgefocus.app.data.ble.model.ReliableEventType
import com.edgefocus.app.data.local.dao.SettingsDao
import com.edgefocus.app.data.remote.BleManager
import com.edgefocus.app.data.remote.BoardJsonParser
import com.edgefocus.app.data.remote.PushAlert
import com.edgefocus.app.data.remote.ReliablePushEventHandler
import com.edgefocus.app.data.session.HostEventInbox
import com.edgefocus.app.data.session.ReliableEventPolicy
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.json.JSONObject
import javax.inject.Inject
import javax.inject.Singleton

data class ActiveHostAlert(
    val eventId: String,
    val sessionId: String,
    val alert: PushAlert
)

@Singleton
class AlertDeliveryCoordinator @Inject constructor(
    @ApplicationContext private val context: Context,
    private val bleManager: BleManager,
    private val connectionSupervisor: ConnectionSupervisor,
    private val inbox: HostEventInbox,
    private val timerStateHolder: TimerStateHolder,
    private val settingsDao: SettingsDao
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var started = false
    private val _activeAlert = MutableStateFlow<ActiveHostAlert?>(null)
    val activeAlert: StateFlow<ActiveHostAlert?> = _activeAlert.asStateFlow()

    private val pushHandler = ReliablePushEventHandler { event ->
        scope.launch { handle(event) }
    }

    fun start() {
        if (started) return
        started = true
        createNotificationChannel()
        bleManager.addReliablePushEventHandler(pushHandler)
        scope.launch { inbox.reconcileDeviceBoot() }
        scope.launch {
            connectionSupervisor.snapshot.collectLatest { snapshot ->
                if (snapshot.phase == HostConnectionPhase.READY) flushPendingAcks()
            }
        }
    }

    fun userAcknowledgeActiveAlert() {
        val active = _activeAlert.value ?: return
        _activeAlert.value = null
        timerStateHolder.clearReliableAlert()
        context.getSystemService(NotificationManager::class.java).cancel(active.eventId.hashCode())
        scope.launch {
            inbox.advanceLocalAck(active.eventId, EventAckStage.USER_ACK)
            flushPendingAcks()
        }
    }

    private suspend fun handle(event: ReliableEventEnvelope) {
        inbox.store(event)
        advanceAndFlush(event, EventAckStage.RECEIVED)
        val claimed = inbox.claim(event.eventId) ?: run {
            flushPendingAcks()
            return
        }

        inbox.markEffectStarted(event.eventId)
        when (event.type) {
            ReliableEventType.ALERT -> {
                val data = JSONObject(event.payloadJson).optJSONObject("data") ?: JSONObject()
                val alert = BoardJsonParser.parsePushAlert(data)
                timerStateHolder.applyReliableAlert(alert)
                _activeAlert.value = ActiveHostAlert(event.eventId, event.sessionId, alert)
                deliverNotification(event.eventId, alert)
            }
            ReliableEventType.SESSION_END -> timerStateHolder.applyReliableSessionEnd(event.payloadJson)
        }
        inbox.markPresented(claimed.eventId)
        advanceAndFlush(event, EventAckStage.PRESENTED)
    }

    private suspend fun advanceAndFlush(event: ReliableEventEnvelope, stage: EventAckStage) {
        if (!ReliableEventPolicy.supportsAckStage(event.type, stage)) return
        inbox.advanceLocalAck(event.eventId, stage)
        flushPendingAcks()
    }

    private suspend fun flushPendingAcks() {
        if (!connectionSupervisor.snapshot.value.isReady) return
        inbox.pendingAcks().forEach { item ->
            val type = ReliableEventType.entries.firstOrNull { it.command == item.eventType } ?: return@forEach
            for (stageNumber in (item.confirmedAckStage + 1)..item.localAckStage) {
                val stage = EventAckStage.entries.getOrNull(stageNumber - 1) ?: break
                if (!ReliableEventPolicy.supportsAckStage(type, stage)) continue
                val result = bleManager.eventAck(item.eventId, item.sessionId, stage.wireValue)
                val ack = result.getOrNull()?.let {
                    runCatching { HostJsonCodec.parseEventAck(String(it, Charsets.UTF_8)) }.getOrNull()
                }
                if (ack?.isSuccessful == true) {
                    inbox.confirmAck(item.eventId, stage, ack.status)
                } else {
                    inbox.recordAckFailure(
                        item.eventId,
                        ack?.status,
                        result.exceptionOrNull()?.message ?: "event_ack_rejected"
                    )
                    break
                }
            }
        }
    }

    private suspend fun deliverNotification(eventId: String, alert: PushAlert) {
        val settings = settingsDao.getSettingsOnce()
        val actions = alert.phoneActions.map { it.lowercase() }.toSet()
        if ("screen" !in actions && "sound" !in actions && "vibrate" !in actions) return
        if (settings?.appScreenAlertsEnabled == false && settings.appSoundAlertsEnabled == false) return

        val intent = Intent(context, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
        val pendingIntent = PendingIntent.getActivity(
            context,
            eventId.hashCode(),
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )
        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_alert)
            .setContentTitle("专注提醒")
            .setContentText(alert.message.ifBlank { "请回到专注状态" })
            .setStyle(NotificationCompat.BigTextStyle().bigText(alert.message))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setContentIntent(pendingIntent)
            .setAutoCancel(false)
            .setOnlyAlertOnce(true)
            .build()
        context.getSystemService(NotificationManager::class.java)
            .notify(eventId.hashCode(), notification)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val channel = NotificationChannel(
            CHANNEL_ID,
            "主机专注提醒",
            NotificationManager.IMPORTANCE_HIGH
        ).apply {
            enableVibration(true)
            description = "EdgeFocus 主机发送的可靠专注提醒"
        }
        context.getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    companion object {
        private const val CHANNEL_ID = "host_reliable_alerts"
    }
}
