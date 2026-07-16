package com.edgefocus.app.data.remote

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Test
import java.util.Base64

class BleChunkAssemblerTest {

    @Test
    fun assemblesResponseChunksIntoOriginalJson() {
        val payload = """{"status":"success","id":7,"data":{"value":"abcdefghijklmnopqrstuvwxyz"}}"""
        val chunks = payload.toByteArray(Charsets.UTF_8).toList().chunked(18)
        val assembler = BleChunkAssembler()

        var assembled: BleChunkAssembler.AssembledMessage? = null
        chunks.forEachIndexed { index, bytes ->
            assembled = assembler.accept(
                JSONObject().apply {
                    put("status", "chunk")
                    put("id", 7)
                    put("chunk_id", "resp_7")
                    put("seq", index)
                    put("total", chunks.size)
                    put("encoding", "base64_utf8")
                    put("data", Base64.getEncoder().encodeToString(bytes.toByteArray()))
                }
            )
            if (index < chunks.lastIndex) {
                assertNull(assembled)
            }
        }

        assertEquals(BleChunkAssembler.MessageKind.RESPONSE, assembled!!.kind)
        assertEquals(7, assembled!!.responseId)
        assertEquals(payload, assembled!!.payload)
    }

    @Test
    fun assemblesPushChunksIntoOriginalPushJson() {
        val payload = """{"cmd":"push_state","data":{"session_phase":"precheck","remaining_ms":0}}"""
        val chunks = payload.toByteArray(Charsets.UTF_8).toList().chunked(16)
        val assembler = BleChunkAssembler()

        var assembled: BleChunkAssembler.AssembledMessage? = null
        chunks.forEachIndexed { index, bytes ->
            assembled = assembler.accept(
                JSONObject().apply {
                    put("cmd", "push_chunk")
                    put("data", JSONObject().apply {
                        put("chunk_id", "push_1")
                        put("seq", index)
                        put("total", chunks.size)
                        put("encoding", "base64_utf8")
                        put("data", Base64.getEncoder().encodeToString(bytes.toByteArray()))
                    })
                }
            )
        }

        assertEquals(BleChunkAssembler.MessageKind.PUSH, assembled!!.kind)
        assertEquals(payload, assembled!!.payload)
    }

    @Test
    fun conflictingMetadataDropsWholePendingMessage() {
        val assembler = BleChunkAssembler()
        assertNull(assembler.accept(responseChunk("same", 0, 2, "first")))
        assertNull(assembler.accept(responseChunk("same", 1, 3, "conflict")))
        assertNull(assembler.accept(responseChunk("same", 1, 2, "second")))
        val restarted = assembler.accept(responseChunk("same", 0, 2, "first"))
        assertNotNull(restarted)
        assertEquals("firstsecond", restarted!!.payload)
    }

    @Test
    fun conflictingDuplicateSequenceDropsMessage() {
        val assembler = BleChunkAssembler()
        assertNull(assembler.accept(responseChunk("duplicate", 0, 2, "first")))
        assertNull(assembler.accept(responseChunk("duplicate", 0, 2, "changed")))
        assertNull(assembler.accept(responseChunk("duplicate", 1, 2, "second")))
    }

    @Test
    fun oversizedChunkAndChunkCountAreRejected() {
        val assembler = BleChunkAssembler()
        assertNull(assembler.accept(responseChunk("large", 0, 1, "x".repeat(129))))
        assertNull(assembler.accept(responseChunk("many", 0, 8_193, "x")))
    }

    private fun responseChunk(
        chunkId: String,
        sequence: Int,
        total: Int,
        value: String
    ): JSONObject = JSONObject().apply {
        put("status", "chunk")
        put("id", 7)
        put("chunk_id", chunkId)
        put("seq", sequence)
        put("total", total)
        put("encoding", "base64_utf8")
        put("data", Base64.getEncoder().encodeToString(value.toByteArray()))
    }
}
