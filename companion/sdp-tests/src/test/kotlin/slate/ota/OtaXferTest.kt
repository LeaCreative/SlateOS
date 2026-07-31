package slate.ota

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

class OtaXferTest {

    // ── encodeBegin wire layout ───────────────────────────────────────────────

    @Test
    fun `encodeBegin produces 43 bytes with correct opcode and fields`() {
        val sha = ByteArray(32) { it.toByte() }
        val msg = encodeBegin(id = 0x1234, total = 0x00074000, sha256 = sha, version = 7)
        assertEquals(43, msg.size)
        assertEquals(OTA_OP_BEGIN, msg[0])
        // id LE
        assertEquals(0x34, msg[1].toInt() and 0xFF)
        assertEquals(0x12, msg[2].toInt() and 0xFF)
        // total LE (475136 = 0x00074000)
        assertEquals(0x00, msg[3].toInt() and 0xFF)
        assertEquals(0x40, msg[4].toInt() and 0xFF)
        assertEquals(0x07, msg[5].toInt() and 0xFF)
        assertEquals(0x00, msg[6].toInt() and 0xFF)
        // sha256 at offset 7
        for (i in 0 until 32) assertEquals(i.toByte(), msg[7 + i])
        // version at offset 39
        assertEquals(7, msg[39].toInt() and 0xFF)
    }

    @Test
    fun `encodeBegin fails with wrong sha256 length`() {
        val result = runCatching { encodeBegin(1, 100, ByteArray(31)) }
        assertTrue(result.isFailure)
    }

    // ── encodeChunk wire layout ───────────────────────────────────────────────

    @Test
    fun `encodeChunk correct header plus data`() {
        val data = byteArrayOf(0xAA.toByte(), 0xBB.toByte(), 0xCC.toByte())
        val msg = encodeChunk(id = 0x0001, offset = 0x00000200, data = data)
        assertEquals(10, msg.size)
        assertEquals(OTA_OP_CHUNK, msg[0])
        // id = 1
        assertEquals(0x01, msg[1].toInt() and 0xFF)
        assertEquals(0x00, msg[2].toInt() and 0xFF)
        // offset = 512 = 0x200
        assertEquals(0x00, msg[3].toInt() and 0xFF)
        assertEquals(0x02, msg[4].toInt() and 0xFF)
        assertEquals(0x00, msg[5].toInt() and 0xFF)
        assertEquals(0x00, msg[6].toInt() and 0xFF)
        assertEquals(0xAA.toByte(), msg[7])
        assertEquals(0xBB.toByte(), msg[8])
        assertEquals(0xCC.toByte(), msg[9])
    }

    @Test
    fun `encodeChunk rejects oversized payload`() {
        val result = runCatching { encodeChunk(1, 0, ByteArray(OTA_MAX_CHUNK_BYTES + 1)) }
        assertTrue(result.isFailure)
    }

    @Test
    fun `encodeChunk with fromIndex and length`() {
        val data = byteArrayOf(0, 1, 2, 3, 4)
        val msg = encodeChunk(id = 1, offset = 0, data = data, fromIndex = 1, length = 3)
        assertEquals(10, msg.size)
        assertEquals(1, msg[7].toInt())
        assertEquals(2, msg[8].toInt())
        assertEquals(3, msg[9].toInt())
    }

    // ── encodeAbort / encodeCommit ────────────────────────────────────────────

    @Test
    fun `encodeAbort is single byte`() {
        val msg = encodeAbort()
        assertEquals(1, msg.size)
        assertEquals(OTA_OP_ABORT, msg[0])
    }

    @Test
    fun `encodeCommit is 3 bytes with id`() {
        val msg = encodeCommit(id = 0xABCD)
        assertEquals(3, msg.size)
        assertEquals(OTA_OP_COMMIT, msg[0])
        assertEquals(0xCD, msg[1].toInt() and 0xFF)
        assertEquals(0xAB, msg[2].toInt() and 0xFF)
    }

    // ── decodeWatchMessage ────────────────────────────────────────────────────

    @Test
    fun `decode CREDIT extracts creditBytes`() {
        val msg = byteArrayOf(OTA_OP_CREDIT, 0x00.toByte(), 0x08.toByte()) // 2048 LE
        val decoded = decodeWatchMessage(msg)
        assertIs<OtaWatchMessage.Credit>(decoded)
        assertEquals(2048, decoded.credit.creditBytes)
    }

    @Test
    fun `decode ACK extracts id and received`() {
        val msg = ByteArray(7)
        msg[0] = OTA_OP_ACK
        wrU16Test(msg, 1, 5)       // id = 5
        wrU32Test(msg, 3, 1024)    // received = 1024
        val decoded = decodeWatchMessage(msg)
        assertIs<OtaWatchMessage.Ack>(decoded)
        assertEquals(5, decoded.ack.id)
        assertEquals(1024, decoded.ack.received)
    }

    @Test
    fun `decode NAK LowBattery`() {
        val msg = byteArrayOf(OTA_OP_NAK, NakReason.LowBattery.code.toByte())
        val decoded = decodeWatchMessage(msg)
        assertIs<OtaWatchMessage.Nak>(decoded)
        assertEquals(NakReason.LowBattery, decoded.nak.reason)
    }

    @Test
    fun `decode NAK Yield`() {
        val msg = byteArrayOf(OTA_OP_NAK, NakReason.Yield.code.toByte())
        val decoded = decodeWatchMessage(msg)
        assertIs<OtaWatchMessage.Nak>(decoded)
        assertEquals(NakReason.Yield, decoded.nak.reason)
    }

    @Test
    fun `decode empty returns null`() {
        assertEquals(null, decodeWatchMessage(ByteArray(0)))
    }

    @Test
    fun `decode unknown opcode returns Unknown`() {
        val decoded = decodeWatchMessage(byteArrayOf(0x77))
        assertIs<OtaWatchMessage.Unknown>(decoded)
        assertEquals(0x77, decoded.opcode)
    }

    @Test
    fun `decode truncated CREDIT returns Unknown`() {
        val decoded = decodeWatchMessage(byteArrayOf(OTA_OP_CREDIT, 0))
        assertIs<OtaWatchMessage.Unknown>(decoded)
    }

    @Test
    fun `decode truncated ACK returns Unknown`() {
        val decoded = decodeWatchMessage(byteArrayOf(OTA_OP_ACK, 0))
        assertIs<OtaWatchMessage.Unknown>(decoded)
    }

    // ── OtaSenderState happy path ─────────────────────────────────────────────

    @Test
    fun `sender happy path produces SendChunks then SendCommit`() {
        val imageSize = 600
        val state = OtaSenderState(id = 1, imageSize = imageSize)
        state.onBeginAcknowledged(creditFromWatch = 2048, ackedOffset = 0)

        // First ACK after the initial credit: should ask for chunks
        val ack1 = OtaWatchMessage.Ack(OtaAck(id = 1, received = 0))
        val action1 = state.onWatchMessage(ack1)
        assertIs<OtaSendAction.SendChunks>(action1)
        assertEquals(0, action1.fromOffset)

        // Simulate sending 512 bytes
        state.onChunkSent(512)
        assertEquals(512, state.sentOffset)
        assertEquals(2048 - 512, state.creditBytes)

        // Watch ACKs 512
        val ack2 = OtaWatchMessage.Ack(OtaAck(id = 1, received = 512))
        val action2 = state.onWatchMessage(ack2)
        assertIs<OtaSendAction.SendChunks>(action2)

        // Simulate sending remaining 88 bytes
        state.onChunkSent(88)

        // Watch ACKs full 600 = total
        val ack3 = OtaWatchMessage.Ack(OtaAck(id = 1, received = 600))
        val action3 = state.onWatchMessage(ack3)
        assertIs<OtaSendAction.SendCommit>(action3)
        assertEquals(1, action3.id)
    }

    @Test
    fun `sender handles Yield NAK then continues on ACK`() {
        val state = OtaSenderState(id = 2, imageSize = 1000)
        state.onBeginAcknowledged(creditFromWatch = 2048, ackedOffset = 0)
        state.onChunkSent(600) // sent oversized (firmware clamps to 512)

        // Firmware: Yield NAK then ACK with 512 accepted
        val yield = OtaWatchMessage.Nak(OtaNak(NakReason.Yield))
        val action1 = state.onWatchMessage(yield)
        assertIs<OtaSendAction.Wait>(action1)

        // Following ACK from firmware carries received = 512
        val ack = OtaWatchMessage.Ack(OtaAck(id = 2, received = 512))
        val action2 = state.onWatchMessage(ack)
        assertIs<OtaSendAction.SendChunks>(action2)
        // sentOffset should have been rewound to 512
        assertEquals(512, state.sentOffset)
    }

    @Test
    fun `sender handles resume via offset rewind on ACK`() {
        val state = OtaSenderState(id = 3, imageSize = 800)
        state.onBeginAcknowledged(creditFromWatch = 2048, ackedOffset = 0)
        state.onChunkSent(512)
        state.onChunkSent(288)  // sent 800 bytes

        // Watch ACKs only 400 (partial write persisted)
        val ack = OtaWatchMessage.Ack(OtaAck(id = 3, received = 400))
        val action = state.onWatchMessage(ack)
        assertIs<OtaSendAction.SendChunks>(action)
        // sentOffset should be rewound
        assertEquals(400, state.sentOffset)
    }

    @Test
    fun `sender fails on LowBattery NAK`() {
        val state = OtaSenderState(id = 4, imageSize = 100)
        state.onBeginAcknowledged(creditFromWatch = 2048)
        val action = state.onWatchMessage(OtaWatchMessage.Nak(OtaNak(NakReason.LowBattery)))
        assertIs<OtaSendAction.Fail>(action)
        assertTrue("battery" in action.reason.lowercase())
    }

    @Test
    fun `sender fails on HashFail NAK`() {
        val state = OtaSenderState(id = 5, imageSize = 100)
        state.onBeginAcknowledged(creditFromWatch = 2048)
        val action = state.onWatchMessage(OtaWatchMessage.Nak(OtaNak(NakReason.HashFail)))
        assertIs<OtaSendAction.Fail>(action)
        assertTrue("hash" in action.reason.lowercase())
    }

    @Test
    fun `sender fails on wrong ACK id`() {
        val state = OtaSenderState(id = 6, imageSize = 100)
        state.onBeginAcknowledged(creditFromWatch = 2048)
        val action = state.onWatchMessage(OtaWatchMessage.Ack(OtaAck(id = 99, received = 0)))
        assertIs<OtaSendAction.Fail>(action)
    }

    @Test
    fun `sendable accounts for credit and image remaining`() {
        val state = OtaSenderState(id = 7, imageSize = 1000)
        state.onBeginAcknowledged(creditFromWatch = 300)
        // credit 300, remaining 1000, sendable = min(300, 1000) = 300
        assertEquals(300, state.sendable)
        state.onChunkSent(300)
        // credit = 0, remaining = 700, sendable = 0
        assertEquals(0, state.sendable)
    }

    @Test
    fun `sender Credit message updates creditBytes and returns SendChunks when active`() {
        val state = OtaSenderState(id = 8, imageSize = 4096)
        state.onBeginAcknowledged(creditFromWatch = 0) // start with no credit
        val action = state.onWatchMessage(OtaWatchMessage.Credit(OtaCredit(2048)))
        assertIs<OtaSendAction.SendChunks>(action)
        assertEquals(2048, state.creditBytes)
    }

    // ── NakReason codec ───────────────────────────────────────────────────────

    @Test
    fun `NakReason roundtrip for all defined codes`() {
        for (reason in NakReason.values()) {
            assertEquals(reason, NakReason.fromCode(reason.code))
        }
    }

    @Test
    fun `NakReason unknown code maps to BadMessage`() {
        assertEquals(NakReason.BadMessage, NakReason.fromCode(0xFF))
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun wrU16Test(buf: ByteArray, offset: Int, value: Int) {
        buf[offset] = (value and 0xFF).toByte()
        buf[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    private fun wrU32Test(buf: ByteArray, offset: Int, value: Int) {
        buf[offset] = (value and 0xFF).toByte()
        buf[offset + 1] = ((value ushr 8) and 0xFF).toByte()
        buf[offset + 2] = ((value ushr 16) and 0xFF).toByte()
        buf[offset + 3] = ((value ushr 24) and 0xFF).toByte()
    }
}
