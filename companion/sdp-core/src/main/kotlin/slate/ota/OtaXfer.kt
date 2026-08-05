package slate.ota

/**
 * Pure Kotlin codec for SDP channel-5 OTA (firmware/ota_xfer.hpp).
 *
 * This file contains:
 *  - Wire constants matching the C++ receiver exactly.
 *  - Encoding helpers (phone → watch): BEGIN, CHUNK, ABORT, COMMIT.
 *  - Decoding helpers (watch → phone): CREDIT, ACK, NAK.
 *  - [OtaSenderState]: a pure state machine the app drives; no I/O.
 *
 * Threading: [OtaSenderState] is not thread-safe. Call [onWatchMessage] and
 * all send helpers from the same coroutine.
 */

// ── Opcodes ───────────────────────────────────────────────────────────────────

const val OTA_OP_BEGIN: Byte = 0x01
const val OTA_OP_CHUNK: Byte = 0x02
const val OTA_OP_ABORT: Byte = 0x03
const val OTA_OP_COMMIT: Byte = 0x04

const val OTA_OP_CREDIT: Byte = 0x10.toByte()
const val OTA_OP_ACK: Byte = 0x11.toByte()
const val OTA_OP_NAK: Byte = 0x12.toByte()

// ── Window constants ──────────────────────────────────────────────────────────

/** Firmware initial and refill window in bytes. */
const val OTA_WINDOW_BYTES: Int = 2048

/** Firmware hard cap on CHUNK data length. */
const val OTA_MAX_CHUNK_BYTES: Int = 512

/** Firmware secondary slot capacity. */
const val OTA_MAX_IMAGE_BYTES: Int = 475_136

// ── NAK reasons ───────────────────────────────────────────────────────────────

enum class NakReason(val code: Int) {
    Ok(0),
    Busy(1),
    BadMessage(2),
    HashFail(3),
    TooLarge(4),
    NoStorage(5),
    LowBattery(6),
    Yield(7),

    /**
     * The running watch image is still on trial (I-13). Stacking a new image
     * on an unconfirmed one would leave a revert landing on firmware nobody
     * validated, so the watch refuses until IMAGE_OK is written.
     */
    Unconfirmed(8);

    companion object {
        fun fromCode(code: Int): NakReason =
            values().firstOrNull { it.code == code } ?: BadMessage
    }
}

// ── Encode: phone → watch ─────────────────────────────────────────────────────

/**
 * Encode an OTA BEGIN message (43 bytes).
 *
 * @param id       Transfer id (u16 LE); reuse the same id when resuming.
 * @param total    Image size in bytes (u32 LE).
 * @param sha256   SHA-256 of the entire image (32 bytes).
 * @param version  Reserved version field; pass 0.
 */
fun encodeBegin(id: Int, total: Int, sha256: ByteArray, version: Int = 0): ByteArray {
    require(sha256.size == 32) { "SHA-256 must be exactly 32 bytes" }
    val out = ByteArray(43)
    out[0] = OTA_OP_BEGIN
    wrU16(out, 1, id)
    wrU32(out, 3, total)
    sha256.copyInto(out, 7)
    wrU32(out, 39, version)
    return out
}

/**
 * Encode an OTA CHUNK message (7 + data bytes, data ≤ [OTA_MAX_CHUNK_BYTES]).
 *
 * @param id     Transfer id.
 * @param offset Byte offset this chunk starts at.
 * @param data   Image data slice.
 */
fun encodeChunk(id: Int, offset: Int, data: ByteArray, fromIndex: Int = 0, length: Int = data.size - fromIndex): ByteArray {
    require(length > 0) { "CHUNK must carry at least 1 data byte" }
    require(length <= OTA_MAX_CHUNK_BYTES) { "CHUNK data length $length exceeds OTA_MAX_CHUNK_BYTES" }
    val out = ByteArray(7 + length)
    out[0] = OTA_OP_CHUNK
    wrU16(out, 1, id)
    wrU32(out, 3, offset)
    data.copyInto(out, 7, fromIndex, fromIndex + length)
    return out
}

/** Encode an OTA ABORT message (1 byte). */
fun encodeAbort(): ByteArray = byteArrayOf(OTA_OP_ABORT)

/**
 * Encode an OTA COMMIT message (3 bytes).
 *
 * Send only after all chunks have been acknowledged and received == total.
 * Firmware will verify SHA-256, write the InfiniTime pending-image magic, and
 * reboot — no ACK/CREDIT will be received after a successful commit.
 */
fun encodeCommit(id: Int): ByteArray {
    val out = ByteArray(3)
    out[0] = OTA_OP_COMMIT
    wrU16(out, 1, id)
    return out
}

// ── Decode: watch → phone ─────────────────────────────────────────────────────

/**
 * Decoded CREDIT message. Carries the new credit window in bytes.
 */
data class OtaCredit(val creditBytes: Int)

/**
 * Decoded ACK message. [received] is the next expected byte offset (resume
 * point), not necessarily equal to the end of the last chunk sent.
 */
data class OtaAck(val id: Int, val received: Int)

/**
 * Decoded NAK message.
 */
data class OtaNak(val reason: NakReason)

/** Union of possible decoded watch→phone messages for channel 5. */
sealed class OtaWatchMessage {
    data class Credit(val credit: OtaCredit) : OtaWatchMessage()
    data class Ack(val ack: OtaAck) : OtaWatchMessage()
    data class Nak(val nak: OtaNak) : OtaWatchMessage()
    data class Unknown(val opcode: Int) : OtaWatchMessage()
}

/**
 * Decode a reassembled channel-5 message from the watch.
 * Returns null if [msg] is empty.
 */
fun decodeWatchMessage(msg: ByteArray): OtaWatchMessage? {
    if (msg.isEmpty()) return null
    return when (msg[0]) {
        OTA_OP_CREDIT -> {
            if (msg.size < 3) OtaWatchMessage.Unknown(msg[0].toInt() and 0xFF)
            else OtaWatchMessage.Credit(OtaCredit(rdU16(msg, 1)))
        }
        OTA_OP_ACK -> {
            if (msg.size < 7) OtaWatchMessage.Unknown(msg[0].toInt() and 0xFF)
            else OtaWatchMessage.Ack(OtaAck(rdU16(msg, 1), rdU32(msg, 3)))
        }
        OTA_OP_NAK -> {
            if (msg.size < 2) OtaWatchMessage.Unknown(msg[0].toInt() and 0xFF)
            else OtaWatchMessage.Nak(OtaNak(NakReason.fromCode(msg[1].toInt() and 0xFF)))
        }
        else -> OtaWatchMessage.Unknown(msg[0].toInt() and 0xFF)
    }
}

// ── Pure sender state machine ─────────────────────────────────────────────────

/**
 * Transfer result: what the app layer should do after receiving a watch message.
 */
sealed class OtaSendAction {
    /** Send the next CHUNK(s) up to the current credit limit. */
    data class SendChunks(val fromOffset: Int, val creditBytes: Int) : OtaSendAction()
    /** Transfer is complete — send COMMIT now. [id] is the transfer id. */
    data class SendCommit(val id: Int) : OtaSendAction()
    /** Transfer failed; reason describes what happened. */
    data class Fail(val reason: String) : OtaSendAction()
    /** Retransmit from the offset the watch gave us. */
    data class Resend(val fromOffset: Int) : OtaSendAction()
    /** No action needed this cycle (credit received before ACK, etc.). */
    object Wait : OtaSendAction()
}

/**
 * Pure state machine for the phone side of channel-5 OTA.
 *
 * Usage:
 * 1. Construct with [imageSize] and a fresh [id].
 * 2. Call [onBeginAcknowledged] after the watch responds to BEGIN with ACK+CREDIT.
 * 3. Call [onWatchMessage] for every decoded [OtaWatchMessage] from the watch.
 *    The returned [OtaSendAction] tells the caller what to do next.
 * 4. When [OtaSendAction.SendCommit] is returned, send [encodeCommit] and wait
 *    for disconnect (the watch reboots).
 *
 * This class carries no I/O; it is fully testable without Android.
 */
class OtaSenderState(
    val id: Int,
    val imageSize: Int,
) {
    var creditBytes: Int = 0
        private set
    var sentOffset: Int = 0
        private set
    var acknowledgedOffset: Int = 0
        private set

    /** True once BEGIN has been acknowledged and chunks may be sent. */
    var active: Boolean = false
        private set

    fun onBeginAcknowledged(creditFromWatch: Int, ackedOffset: Int = 0) {
        active = true
        creditBytes = creditFromWatch
        sentOffset = ackedOffset
        acknowledgedOffset = ackedOffset
    }

    /**
     * Process a decoded watch message and return what the caller should do.
     */
    fun onWatchMessage(msg: OtaWatchMessage): OtaSendAction {
        return when (msg) {
            is OtaWatchMessage.Credit -> {
                creditBytes = msg.credit.creditBytes
                if (active && acknowledgedOffset == imageSize) {
                    OtaSendAction.SendCommit(id)
                } else if (active) {
                    OtaSendAction.SendChunks(sentOffset, creditBytes)
                } else {
                    OtaSendAction.Wait
                }
            }
            is OtaWatchMessage.Ack -> {
                if (msg.ack.id != id) {
                    return OtaSendAction.Fail("ACK for unknown id ${msg.ack.id}")
                }
                acknowledgedOffset = msg.ack.received
                if (acknowledgedOffset > imageSize) {
                    return OtaSendAction.Fail("ACK offset ${acknowledgedOffset} beyond image size $imageSize")
                }
                // Offset mismatch (idempotent resume hint): rewind sent pointer.
                if (msg.ack.received < sentOffset) {
                    sentOffset = msg.ack.received
                }
                if (acknowledgedOffset == imageSize) {
                    OtaSendAction.SendCommit(id)
                } else {
                    OtaSendAction.SendChunks(sentOffset, creditBytes)
                }
            }
            is OtaWatchMessage.Nak -> {
                when (msg.nak.reason) {
                    NakReason.Yield -> {
                        // Firmware accepted first 512 bytes; ACK will follow.
                        OtaSendAction.Wait
                    }
                    NakReason.Busy -> OtaSendAction.Fail("Watch busy (another transfer active?)")
                    NakReason.LowBattery -> OtaSendAction.Fail("Watch battery too low (< 30%)")
                    NakReason.Unconfirmed -> OtaSendAction.Fail(
                        "Watch image is still on trial — keep the watch connected " +
                            "until the amber bar clears, then retry",
                    )
                    NakReason.TooLarge -> OtaSendAction.Fail("Image too large for watch secondary slot")
                    NakReason.NoStorage -> OtaSendAction.Fail("Watch storage error (erase or write failed)")
                    NakReason.HashFail -> OtaSendAction.Fail("Hash mismatch at commit")
                    NakReason.BadMessage -> OtaSendAction.Fail("Watch rejected a message (protocol error)")
                    NakReason.Ok -> OtaSendAction.Wait
                }
            }
            is OtaWatchMessage.Unknown -> {
                OtaSendAction.Fail("Unknown watch opcode ${msg.opcode}")
            }
        }
    }

    /**
     * Advance [sentOffset] by [bytes] after transmitting a CHUNK.
     * Call this immediately after each [encodeChunk] call.
     */
    fun onChunkSent(bytes: Int) {
        creditBytes -= bytes
        sentOffset += bytes
    }

    /** Number of bytes that can be sent right now without exceeding credit. */
    val sendable: Int
        get() = minOf(creditBytes, imageSize - sentOffset)
}

// ── Little-endian helpers ─────────────────────────────────────────────────────

private fun wrU16(buf: ByteArray, offset: Int, value: Int) {
    buf[offset] = (value and 0xFF).toByte()
    buf[offset + 1] = ((value ushr 8) and 0xFF).toByte()
}

private fun wrU32(buf: ByteArray, offset: Int, value: Int) {
    buf[offset] = (value and 0xFF).toByte()
    buf[offset + 1] = ((value ushr 8) and 0xFF).toByte()
    buf[offset + 2] = ((value ushr 16) and 0xFF).toByte()
    buf[offset + 3] = ((value ushr 24) and 0xFF).toByte()
}

internal fun rdU16(buf: ByteArray, offset: Int): Int =
    (buf[offset].toInt() and 0xFF) or ((buf[offset + 1].toInt() and 0xFF) shl 8)

internal fun rdU32(buf: ByteArray, offset: Int): Int =
    (buf[offset].toInt() and 0xFF) or
        ((buf[offset + 1].toInt() and 0xFF) shl 8) or
        ((buf[offset + 2].toInt() and 0xFF) shl 16) or
        ((buf[offset + 3].toInt() and 0xFF) shl 24)
