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
        val parts: MutableMap<Int, ByteArray> = mutableMapOf()
    )

    private val pending = mutableMapOf<String, PendingChunks>()

    fun accept(json: JSONObject, nowMs: Long = System.currentTimeMillis()): AssembledMessage? {
        expire(nowMs)
        val chunk = extractChunk(json) ?: return null
        val pendingChunks = pending.getOrPut(chunk.chunkId) {
            PendingChunks(
                kind = chunk.kind,
                responseId = chunk.responseId,
                total = chunk.total,
                createdAtMs = nowMs
            )
        }
        pendingChunks.parts[chunk.seq] = chunk.bytes
        if (pendingChunks.parts.size < pendingChunks.total) {
            return null
        }

        val payloadBytes = buildList {
            for (index in 0 until pendingChunks.total) {
                add(pendingChunks.parts[index] ?: return null)
            }
        }.fold(ByteArray(0)) { acc, bytes -> acc + bytes }
        pending.remove(chunk.chunkId)
        return AssembledMessage(
            kind = pendingChunks.kind,
            responseId = pendingChunks.responseId,
            payload = payloadBytes.toString(Charsets.UTF_8)
        )
    }

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
        val chunkId = data.optString("chunk_id").takeIf { it.isNotBlank() } ?: return null
        val seq = data.optInt("seq", -1)
        val total = data.optInt("total", -1)
        if (seq < 0 || total <= 0 || seq >= total) return null
        val encoded = data.optString("data").takeIf { it.isNotBlank() } ?: return null
        val bytes = Base64.getDecoder().decode(encoded)
        return Chunk(kind, responseId, chunkId, seq, total, bytes)
    }

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
    }
}
