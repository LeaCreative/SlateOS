package slate.session

import slate.generated.SdpWire

/**
 * Phone-side M7 session layer (CONTROL channel 0).
 *
 * Wire layouts match firmware `session.cpp` and `docs/session.md`.
 */
class SessionClient(
    private val phoneId: ByteArray,
    private val hostVersion: String,
    /** Default HELLO profile — Ambient (500 ms) unless the catalog lacks it. */
    private val defaultProfileId: Int = PROFILE_ID_AMBIENT,
) {
    enum class State {
        Disconnected,
        Connected,
        Ready,
    }

    enum class PendingDisplay {
        Push,
        Replace,
    }

    data class ProfileDesc(
        val id: Int,
        val name: String,
        val backlight: Int,
        val intervalUnits: Int,
    )

    data class HelloOffer(
        val fwMajor: Int,
        val fwMinor: Int,
        val fwPatch: Int,
        val protocolVersion: Int,
        val width: Int,
        val height: Int,
        val opcodeBitmap: ByteArray,
        val freeDlBytes: Int,
        val profiles: List<ProfileDesc>,
        val flags: Int,
    )

    data class InboundResult(
        val outbound: List<ByteArray> = emptyList(),
    )

    var state: State = State.Disconnected
        private set

    var helloOffer: HelloOffer? = null
        private set

    var freeCreditBytes: Int = 0
        private set

    var confirmStatus: ConfirmStatus.Snapshot? = null
        private set

    var selectedProfileId: Int = defaultProfileId
        private set

    private var pendingDisplay: PendingDisplay = PendingDisplay.Replace

    fun onLinkUp() {
        state = State.Connected
        helloOffer = null
        freeCreditBytes = 0
        confirmStatus = null
        pendingDisplay = PendingDisplay.Replace
        selectedProfileId = defaultProfileId
    }

    fun onLinkDown() {
        state = State.Disconnected
        helloOffer = null
        freeCreditBytes = 0
        confirmStatus = null
        pendingDisplay = PendingDisplay.Replace
    }

    fun onControlMessage(msg: ByteArray): InboundResult {
        if (msg.isEmpty()) return InboundResult()
        return when (msg[0].toInt() and 0xFF) {
            SdpWire.ControlOp.HELLO_OFFER -> handleHelloOffer(msg)
            SdpWire.ControlOp.CREDIT -> {
                parseCredit(msg)
                // Watch sends CREDIT only after HELLO_ACCEPT lands and it enters
                // Ready (session.cpp). Phone used to flip Ready on HELLO_OFFER,
                // then enqueue ACCEPT — if that write returned rc=201 and was
                // dropped, the phone pushed DISPLAY while the watch stayed
                // Connected and discarded every list (++display_drops_).
                if (state == State.Connected && helloOffer != null) {
                    state = State.Ready
                }
                InboundResult()
            }
            SdpWire.ControlOp.CONFIRM_STATUS -> {
                ConfirmStatus.parse(msg)?.let { confirmStatus = it }
                InboundResult()
            }
            SdpWire.ControlOp.GOODBYE -> {
                onLinkDown()
                InboundResult()
            }
            else -> InboundResult()
        }
    }

    fun encodeHeartbeat(): ByteArray = byteArrayOf(SdpWire.ControlOp.HEARTBEAT.toByte())

    fun encodeConfirmStatusRequest(): ByteArray = ConfirmStatus.encodeRequest()

    fun encodeScreenPush(): ByteArray = byteArrayOf(SdpWire.ControlOp.SCREEN_PUSH.toByte())

    fun encodeScreenReplace(): ByteArray = byteArrayOf(SdpWire.ControlOp.SCREEN_REPLACE.toByte())

    fun encodeScreenPop(): ByteArray = byteArrayOf(SdpWire.ControlOp.SCREEN_POP.toByte())

    fun encodeGoodbye(): ByteArray = byteArrayOf(SdpWire.ControlOp.GOODBYE.toByte())

    /** Phone → watch HELLO_REJECT (mirrors firmware reject path). */
    fun encodeHelloReject(reason: Int = 0): ByteArray =
        byteArrayOf(SdpWire.ControlOp.HELLO_REJECT.toByte(), reason.toByte())

    fun encodeSetProfile(profileId: Int): ByteArray =
        byteArrayOf(SdpWire.ControlOp.SET_PROFILE.toByte(), profileId.toByte())

    /**
     * Select a catalog profile. Returns SET_PROFILE bytes when the id changes
     * and the session can carry CONTROL; null when unchanged.
     */
    fun selectProfile(profileId: Int): ByteArray? {
        if (profileId == selectedProfileId) return null
        selectedProfileId = profileId
        return encodeSetProfile(profileId)
    }

    fun scheduleDisplay(op: PendingDisplay) {
        pendingDisplay = op
    }

    /** CONTROL bytes to send immediately before the next DISPLAY payload. */
    fun takePreDisplayControl(): ByteArray = when (pendingDisplay) {
        PendingDisplay.Push -> encodeScreenPush()
        PendingDisplay.Replace -> encodeScreenReplace()
    }.also {
        pendingDisplay = PendingDisplay.Replace
    }

    fun encodeHelloAccept(
        profileId: Int = selectedProfileId,
        phoneIdBytes: ByteArray = phoneId,
        version: String = hostVersion,
    ): ByteArray {
        require(phoneIdBytes.size == PHONE_ID_BYTES) { "phone id must be $PHONE_ID_BYTES bytes" }
        val verBytes = version.toByteArray(Charsets.US_ASCII)
        require(verBytes.size <= 255) { "host version too long" }
        val out = ByteArray(1 + 1 + PHONE_ID_BYTES + 1 + verBytes.size)
        var i = 0
        out[i++] = SdpWire.ControlOp.HELLO_ACCEPT.toByte()
        out[i++] = profileId.toByte()
        phoneIdBytes.copyInto(out, i)
        i += PHONE_ID_BYTES
        out[i++] = verBytes.size.toByte()
        verBytes.copyInto(out, i)
        return out
    }

    private fun handleHelloOffer(msg: ByteArray): InboundResult {
        val offer = parseHelloOffer(msg) ?: return InboundResult()
        helloOffer = offer
        // Embedded free_dl is an advertisement only; stay Connected until the
        // post-accept CREDIT proves the watch processed HELLO_ACCEPT.
        freeCreditBytes = offer.freeDlBytes
        selectedProfileId = pickProfileId(offer.profiles)
        return InboundResult(outbound = listOf(encodeHelloAccept(selectedProfileId)))
    }

    private fun pickProfileId(profiles: List<ProfileDesc>): Int {
        if (profiles.any { it.id == defaultProfileId }) return defaultProfileId
        return profiles.firstOrNull()?.id ?: defaultProfileId
    }

    private fun parseCredit(msg: ByteArray) {
        if (msg.size < 3) return
        if (msg[0].toInt() and 0xFF != SdpWire.ControlOp.CREDIT) return
        freeCreditBytes = u16Le(msg, 1)
    }

    fun parseHelloOffer(msg: ByteArray): HelloOffer? {
        if (msg.size < 44) return null
        if (msg[0].toInt() and 0xFF != SdpWire.ControlOp.HELLO_OFFER) return null

        val fwMajor = msg[1].toInt() and 0xFF
        val fwMinor = msg[2].toInt() and 0xFF
        val fwPatch = msg[3].toInt() and 0xFF
        val protocolVersion = u16Le(msg, 4)
        val width = u16Le(msg, 6)
        val height = u16Le(msg, 8)
        val bitmap = msg.copyOfRange(10, 10 + OPCODE_BITMAP_BYTES)
        val freeDlBytes = u16Le(msg, 10 + OPCODE_BITMAP_BYTES)
        var off = 10 + OPCODE_BITMAP_BYTES + 2
        if (off >= msg.size) return null

        val profileCount = msg[off++].toInt() and 0xFF
        val profiles = ArrayList<ProfileDesc>(profileCount)
        repeat(profileCount) {
            if (off + 2 > msg.size) return null
            val id = msg[off++].toInt() and 0xFF
            val nameLen = msg[off++].toInt() and 0xFF
            if (nameLen > MAX_PROFILE_NAME_LEN || off + nameLen + 3 > msg.size) return null
            val name = msg.copyOfRange(off, off + nameLen).decodeToString()
            off += nameLen
            val backlight = msg[off++].toInt() and 0xFF
            val intervalUnits = u16Le(msg, off)
            off += 2
            profiles += ProfileDesc(id, name, backlight, intervalUnits)
        }
        if (off + 2 > msg.size) return null
        off++ // asset_pack_count — none until M11
        val flags = msg[off].toInt() and 0xFF
        return HelloOffer(
            fwMajor, fwMinor, fwPatch,
            protocolVersion, width, height,
            bitmap, freeDlBytes, profiles, flags,
        )
    }

    companion object {
        const val PHONE_ID_BYTES = 8
        const val OPCODE_BITMAP_BYTES = 32
        const val MAX_PROFILE_NAME_LEN = 16
        const val PROFILE_ID_AMBIENT = 0
        const val PROFILE_ID_ACTIVE = 1
        const val PROFILE_ID_STREAMING = 2

        fun u16Le(buf: ByteArray, off: Int): Int =
            (buf[off].toInt() and 0xFF) or ((buf[off + 1].toInt() and 0xFF) shl 8)

        fun putU16Le(out: ByteArray, off: Int, value: Int) {
            out[off] = (value and 0xFF).toByte()
            out[off + 1] = ((value shr 8) and 0xFF).toByte()
        }
    }
}
