package com.edgefocus.app.data.remote

import org.json.JSONObject
import java.util.Base64

class BleChunkAssembler {
    enum class MessageKind {
        RESPONSE,
        PUSH
    }

    data class AssembledMessage(
        val kind: MessageKind,
        val responseId: Int?,
        val payload: String
    )

    private data class PendingChunks(
        val kind: MessageKind,
        val responseId: Int?,
        val total: Int,
        val createdAtMs: Long,
        val parts: MutableMap<Int, ByteArray> = mutableMapOf(),
        var accumulatedBytes: Int = 0
    )

    private val pending = linkedMapOf<String, PendingChunks>()

    @Synchronized
    fun accept(json: JSONObject, nowMs: Long = System.currentTimeMillis()): AssembledMessage? {
        expire(nowMs)
        val chunkId = rawChunkId(json)
        val chunk = try {
            extractChunk(json)
        } catch (_: IllegalArgumentException) {
            chunkId?.let { pending.remove(it) }
            return null
        }
        if (chunk == null) {
            chunkId?.let { pending.remove(it) }
            return null
        }
        val existing = pending[chunk.chunkId]
        if (existing != null &&
            (existing.kind != chunk.kind || existing.responseId != chunk.responseId ||
                existing.total != chunk.total)
        ) {
            pending.remove(chunk.chunkId)
            return null
        }
        if (existing == null && pending.size >= MAX_PENDING_MESSAGES) {
            pending.entries.firstOrNull()?.key?.let { pending.remove(it) }
        }
        val pendingChunks = pending.getOrPut(chunk.chunkId) {
            PendingChunks(
                kind = chunk.kind,
                responseId = chunk.responseId,
                total = chunk.total,
                createdAtMs = nowMs
            )
        }
        val previous = pendingChunks.parts[chunk.seq]
        if (previous != null && !previous.contentEquals(chunk.bytes)) {
            pending.remove(chunk.chunkId)
            return null
        }
        if (previous == null) {
            if (pendingChunks.accumulatedBytes + chunk.bytes.size > MAX_MESSAGE_BYTES) {
                pending.remove(chunk.chunkId)
                return null
            }
            pendingChunks.parts[chunk.seq] = chunk.bytes
            pendingChunks.accumulatedBytes += chunk.bytes.size
        }
        if (pendingChunks.parts.size < pendingChunks.total) {
            return null
        }

        val payloadBytes = ByteArray(pendingChunks.accumulatedBytes)
        var offset = 0
        for (index in 0 until pendingChunks.total) {
            val bytes = pendingChunks.parts[index] ?: return null
            bytes.copyInto(payloadBytes, destinationOffset = offset)
            offset += bytes.size
        }
        pending.remove(chunk.chunkId)
        return AssembledMessage(
            kind = pendingChunks.kind,
            responseId = pendingChunks.responseId,
            payload = payloadBytes.toString(Charsets.UTF_8)
        )
    }

    @Synchronized
    fun clear() {
        pending.clear()
    }

    private fun expire(nowMs: Long) {
        val expired = pending.filterValues { nowMs - it.createdAtMs > CHUNK_TIMEOUT_MS }.keys
        expired.forEach { pending.remove(it) }
    }

    private fun extractChunk(json: JSONObject): Chunk? {
        if (json.optString("status") == "chunk") {
            return parseChunkData(
                data = json,
                kind = MessageKind.RESPONSE,
                responseId = json.optInt("id", -1).takeIf { it >= 0 }
            )
        }
        if (json.optString("cmd") == "push_chunk") {
            val data = json.optJSONObject("data") ?: return null
            return parseChunkData(data, MessageKind.PUSH, null)
        }
        return null
    }

    private fun parseChunkData(data: JSONObject, kind: MessageKind, responseId: Int?): Chunk? {
        if (data.optString("encoding") != "base64_utf8") return null
        val chunkId = data.optString("chunk_id")
            .takeIf { it.isNotBlank() && it.length <= MAX_CHUNK_ID_LENGTH }
            ?: return null
        val seq = data.optInt("seq", -1)
        val total = data.optInt("total", -1)
        if (seq < 0 || total <= 0 || total > MAX_CHUNKS || seq >= total) return null
        val encoded = data.optString("data")
            .takeIf { it.isNotBlank() && it.length <= MAX_ENCODED_CHUNK_CHARS }
            ?: return null
        val bytes = Base64.getDecoder().decode(encoded)
        if (bytes.size > MAX_CHUNK_BYTES) return null
        return Chunk(kind, responseId, chunkId, seq, total, bytes)
    }

    private fun rawChunkId(json: JSONObject): String? = when {
        json.optString("status") == "chunk" -> json.optString("chunk_id")
        json.optString("cmd") == "push_chunk" ->
            json.optJSONObject("data")?.optString("chunk_id")
        else -> null
    }?.takeIf { it.isNotBlank() }

    private data class Chunk(
        val kind: MessageKind,
        val responseId: Int?,
        val chunkId: String,
        val seq: Int,
        val total: Int,
        val bytes: ByteArray
    )

    private companion object {
        const val CHUNK_TIMEOUT_MS = 15_000L
        const val MAX_CHUNKS = 8_192
        const val MAX_MESSAGE_BYTES = 768 * 1024
        const val MAX_PENDING_MESSAGES = 4
        const val MAX_CHUNK_BYTES = 128
        const val MAX_ENCODED_CHUNK_CHARS = 192
        const val MAX_CHUNK_ID_LENGTH = 128
    }
}
