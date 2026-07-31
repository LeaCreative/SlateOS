package slate.session

import slate.generated.SdpWire

/** Shared HELLO_OFFER builders for SessionClient tests (firmware layout). */
object SessionTestFixtures {
    data class Profile(
        val id: Int,
        val name: String,
        val backlight: Int,
        val intervalUnits: Int,
    )

    fun defaultProfiles(): List<Profile> = listOf(
        Profile(0, "ambient", 0, 400),
        Profile(1, "active", 55, 24),
        Profile(2, "streaming", 70, 12),
    )

    fun encodeHelloOffer(
        fwMajor: Int = 0,
        fwMinor: Int = 11,
        fwPatch: Int = 0,
        protocolVersion: Int = SdpWire.PROTOCOL_VERSION,
        width: Int = SdpWire.DISPLAY_SIZE,
        height: Int = SdpWire.DISPLAY_SIZE,
        freeDlBytes: Int = SdpWire.MAX_LIST_BYTES,
        profiles: List<Profile> = defaultProfiles(),
        diagAvailable: Boolean = true,
    ): ByteArray {
        val out = ArrayList<Byte>()
        out += SdpWire.ControlOp.HELLO_OFFER.toByte()
        out += fwMajor.toByte()
        out += fwMinor.toByte()
        out += fwPatch.toByte()
        out += u16Le(protocolVersion)
        out += u16Le(width)
        out += u16Le(height)
        out += opcodeBitmap()
        out += u16Le(freeDlBytes)
        out += profiles.size.toByte()
        for (p in profiles) {
            val nameBytes = p.name.toByteArray(Charsets.US_ASCII)
            require(nameBytes.size <= SessionClient.MAX_PROFILE_NAME_LEN)
            out += p.id.toByte()
            out += nameBytes.size.toByte()
            nameBytes.forEach { out += it }
            out += p.backlight.toByte()
            out += u16Le(p.intervalUnits)
        }
        out += 0 // asset_pack_count
        out += if (diagAvailable) 1 else 0
        return out.toByteArray()
    }

    fun opcodeBitmap(): List<Byte> {
        val bitmap = ByteArray(SessionClient.OPCODE_BITMAP_BYTES)
        val ops = intArrayOf(
            SdpWire.Op.CLEAR, SdpWire.Op.SET_PALETTE, SdpWire.Op.RECT, SdpWire.Op.RECT_ROUND,
            SdpWire.Op.LINE, SdpWire.Op.CIRCLE, SdpWire.Op.ARC, SdpWire.Op.POLYLINE,
            SdpWire.Op.CLIP_RECT, SdpWire.Op.CLIP_CLEAR, SdpWire.Op.TEXT, SdpWire.Op.TEXT_BOX,
            SdpWire.Op.ICON, SdpWire.Op.IMAGE, SdpWire.Op.PROGRESS_BAR, SdpWire.Op.PROGRESS_ARC,
            SdpWire.Op.BEGIN_ELEM, SdpWire.Op.END_ELEM, SdpWire.Op.SCROLL_REGION,
            SdpWire.Op.PATCH, SdpWire.Op.PATCH_REF, SdpWire.Op.HAPTIC, SdpWire.Op.BACKLIGHT,
            SdpWire.Op.COMMIT, SdpWire.Op.RETAIN,
        )
        for (op in ops) {
            bitmap[op shr 3] =
                (bitmap[op shr 3].toInt() or (1 shl (op and 7))).toByte()
        }
        return bitmap.toList()
    }

    fun u16Le(value: Int): List<Byte> = listOf(
        (value and 0xFF).toByte(),
        ((value shr 8) and 0xFF).toByte(),
    )
}
