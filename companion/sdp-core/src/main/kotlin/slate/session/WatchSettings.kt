package slate.session

import slate.generated.SdpWire

/**
 * Bidirectional watch-settings sync (CONTROL `SETTINGS_SYNC` = 0x21).
 *
 * The exact mirror of the firmware's `src/settings_sync.cpp`. Both ends encode
 * the same bytes and — importantly — both run the same merge rule from
 * their own point of view, which is what stops them overwriting each other
 * forever when the user has edited on both sides.
 *
 * Wire v4: [op][version][revision:u32 LE][tiltEnabled][wakeSeconds]
 *          [showSteps][showDiag][hrEnabled]
 *          [uiChrome:u16 LE][faceBright:u16 LE][faceDim:u16 LE]
 *          [raiseSensitivity][shakeEnabled][shakeSensitivity]
 */
object WatchSettings {
    const val OP: Int = SdpWire.ControlOp.SETTINGS_SYNC
    const val WIRE_VERSION: Int = 4
    const val PAYLOAD_BYTES: Int = 20

    const val DEFAULT_UI_CHROME: Int = 0xFFFF
    const val DEFAULT_FACE_BRIGHT: Int = 0xFFFF
    const val DEFAULT_FACE_DIM: Int = 0x8410

    /** Soft=0 / Normal=1 / Hard=2. */
    const val SENS_SOFT: Int = 0
    const val SENS_NORMAL: Int = 1
    const val SENS_HARD: Int = 2

    /** Display-timeout choices, in the order the watch's own row cycles them. */
    val WAKE_SECONDS_CHOICES: List<Int> = listOf(10, 20, 30, 60, 120, 0)

    val SENSITIVITY_CHOICES: List<Int> = listOf(SENS_SOFT, SENS_NORMAL, SENS_HARD)

    fun sensitivityLabel(v: Int): String = when (clampSens(v)) {
        SENS_SOFT -> "Soft"
        SENS_HARD -> "Hard"
        else -> "Normal"
    }

    fun clampSens(v: Int): Int = if (v in 0..2) v else SENS_NORMAL

    /**
     * The synced subset of the watch's settings.
     *
     * Legacy BMA any-motion `tiltSensitivity` is not synced. Raise and shake
     * each have Soft/Normal/Hard. Shake defaults Off (InfiniTime parity).
     *
     * `hrEnabled` is a master gate: Off keeps the HRS3300 asleep; On allows
     * on-demand measurement (not continuous heart rate).
     *
     * Theme colours are RGB565 (same packing as SDP RGB ops).
     */
    data class Payload(
        val revision: Long = 0L,
        val tiltEnabled: Boolean = true,
        /** 0 means "never sleep on a timer". */
        val wakeSeconds: Int = 20,
        val showSteps: Boolean = true,
        /** Bring-up diagnostic lines on the watch face. */
        val showDiag: Boolean = true,
        /** Heart-rate sensor master enable. */
        val hrEnabled: Boolean = false,
        /** Button outlines + button/label text (non-face screens). */
        val uiChrome: Int = DEFAULT_UI_CHROME,
        /** Face time + battery fill. */
        val faceBright: Int = DEFAULT_FACE_BRIGHT,
        /** Face date, steps/HR, %, version/diag, battery track. */
        val faceDim: Int = DEFAULT_FACE_DIM,
        val raiseSensitivity: Int = SENS_NORMAL,
        val shakeEnabled: Boolean = false,
        val shakeSensitivity: Int = SENS_NORMAL,
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
        out[9] = if (p.showDiag) 1 else 0
        out[10] = if (p.hrEnabled) 1 else 0
        putU16Le(out, 11, p.uiChrome)
        putU16Le(out, 13, p.faceBright)
        putU16Le(out, 15, p.faceDim)
        out[17] = clampSens(p.raiseSensitivity).toByte()
        out[18] = if (p.shakeEnabled) 1 else 0
        out[19] = clampSens(p.shakeSensitivity).toByte()
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
            showDiag = (msg[9].toInt() and 0xFF) != 0,
            hrEnabled = (msg[10].toInt() and 0xFF) != 0,
            uiChrome = getU16Le(msg, 11),
            faceBright = getU16Le(msg, 13),
            faceDim = getU16Le(msg, 15),
            raiseSensitivity = clampSens(msg[17].toInt() and 0xFF),
            shakeEnabled = (msg[18].toInt() and 0xFF) != 0,
            shakeSensitivity = clampSens(msg[19].toInt() and 0xFF),
        )
    }

    /** True when the two carry different settings, revision aside. */
    fun differs(a: Payload, b: Payload): Boolean =
        a.tiltEnabled != b.tiltEnabled ||
            a.wakeSeconds != b.wakeSeconds ||
            a.showSteps != b.showSteps ||
            a.showDiag != b.showDiag ||
            a.hrEnabled != b.hrEnabled ||
            a.uiChrome != b.uiChrome ||
            a.faceBright != b.faceBright ||
            a.faceDim != b.faceDim ||
            a.raiseSensitivity != b.raiseSensitivity ||
            a.shakeEnabled != b.shakeEnabled ||
            a.shakeSensitivity != b.shakeSensitivity

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

    private fun putU16Le(out: ByteArray, off: Int, v: Int) {
        out[off] = (v and 0xFF).toByte()
        out[off + 1] = ((v shr 8) and 0xFF).toByte()
    }

    private fun getU16Le(msg: ByteArray, off: Int): Int =
        (msg[off].toInt() and 0xFF) or ((msg[off + 1].toInt() and 0xFF) shl 8)
}
