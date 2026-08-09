package slate.session

import slate.generated.SdpWire

/**
 * Bidirectional watch-settings sync (CONTROL `SETTINGS_SYNC` = 0x21).
 *
 * The exact mirror of the firmware's `src/settings_sync.cpp`. Both ends encode
 * the same ten bytes and — importantly — both run the same merge rule from
 * their own point of view, which is what stops them overwriting each other
 * forever when the user has edited on both sides.
 *
 * Wire: [op][version][revision:u32 LE][tiltEnabled][wakeSeconds][showSteps][reserved]
 */
object WatchSettings {
    const val OP: Int = SdpWire.ControlOp.SETTINGS_SYNC
    const val WIRE_VERSION: Int = 1
    const val PAYLOAD_BYTES: Int = 10

    /** Display-timeout choices, in the order the watch's own row cycles them. */
    val WAKE_SECONDS_CHOICES: List<Int> = listOf(10, 20, 30, 60, 120, 0)

    /**
     * The synced subset of the watch's settings.
     *
     * `tiltSensitivity` is deliberately absent, matching the firmware: it
     * configured the any-motion threshold that raise-to-wake replaced, so it no
     * longer does anything. Offering a control with no effect would be worse
     * than not offering it.
     */
    data class Payload(
        val revision: Long = 0L,
        val tiltEnabled: Boolean = true,
        /** 0 means "never sleep on a timer". */
        val wakeSeconds: Int = 20,
        val showSteps: Boolean = true,
    )

    fun encode(p: Payload): ByteArray {
        val out = ByteArray(PAYLOAD_BYTES)
        out[0] = OP.toByte()
        out[1] = WIRE_VERSION.toByte()
        out[2] = (p.revision and 0xFF).toByte()
        out[3] = ((p.revision shr 8) and 0xFF).toByte()
        out[4] = ((p.revision shr 16) and 0xFF).toByte()
        out[5] = ((p.revision shr 24) and 0xFF).toByte()
        out[6] = if (p.tiltEnabled) 1 else 0
        out[7] = (p.wakeSeconds and 0xFF).toByte()
        out[8] = if (p.showSteps) 1 else 0
        out[9] = 0 // reserved; a later setting goes here without a version bump
        return out
    }

    /** Null on a short buffer, the wrong opcode, or an unknown wire version. */
    fun decode(msg: ByteArray): Payload? {
        if (msg.size < PAYLOAD_BYTES) return null
        if (msg[0].toInt() and 0xFF != OP) return null
        // Unknown future versions are ignored rather than guessed at: misreading
        // a settings message silently changes what the watch does.
        if (msg[1].toInt() and 0xFF != WIRE_VERSION) return null
        val rev = (msg[2].toLong() and 0xFF) or
            ((msg[3].toLong() and 0xFF) shl 8) or
            ((msg[4].toLong() and 0xFF) shl 16) or
            ((msg[5].toLong() and 0xFF) shl 24)
        return Payload(
            revision = rev,
            tiltEnabled = (msg[6].toInt() and 0xFF) != 0,
            wakeSeconds = msg[7].toInt() and 0xFF,
            showSteps = (msg[8].toInt() and 0xFF) != 0,
        )
    }

    /** True when the two carry different settings, revision aside. */
    fun differs(a: Payload, b: Payload): Boolean =
        a.tiltEnabled != b.tiltEnabled ||
            a.wakeSeconds != b.wakeSeconds ||
            a.showSteps != b.showSteps

    /**
     * Should [incoming] replace [current]?
     *
     * Higher revision wins. Equal revision with identical content is a no-op.
     * Equal revision with *different* content is a real conflict — both sides
     * edited without seeing each other — and the watch wins, so this side (the
     * phone) yields. The firmware runs the identical rule with
     * `selfIsWatch = true`, so the two reach the same verdict independently.
     */
    fun shouldApply(
        current: Payload,
        incoming: Payload,
        selfIsWatch: Boolean = false,
    ): Boolean {
        if (incoming.revision > current.revision) return true
        if (incoming.revision < current.revision) return false
        if (!differs(current, incoming)) return false
        return !selfIsWatch
    }

    /**
     * Revision to stamp on a local edit: one past anything either side has shown.
     *
     * This is what makes the counter a Lamport clock rather than a per-side
     * sequence number, and it is the whole reason last-writer-wins still holds
     * after both ends have been offline and edited.
     */
    fun nextRevision(localRev: Long, highestSeen: Long): Long {
        val base = maxOf(localRev, highestSeen)
        // Saturate rather than wrap: a wrap would make an ancient revision
        // outrank a current one and silently resurrect old settings.
        return if (base >= 0xFFFFFFFFL) 0xFFFFFFFFL else base + 1L
    }
}
