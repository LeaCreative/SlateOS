package slate.input

import slate.generated.SdpWire

/** Encode SDP input events (channel 2, §4.4) as byte arrays. */
object InputEventEncoder {
    fun tap(elemId: Int, x: Int, y: Int): ByteArray = byteArrayOf(
        SdpWire.InputOp.TAP.toByte(),
        (elemId and 0xFF).toByte(),
        ((elemId shr 8) and 0xFF).toByte(),
        x.toByte(),
        y.toByte(),
    )

    fun longPress(elemId: Int): ByteArray = byteArrayOf(
        SdpWire.InputOp.LONG_PRESS.toByte(),
        (elemId and 0xFF).toByte(),
        ((elemId shr 8) and 0xFF).toByte(),
    )

    fun swipe(dir: Int): ByteArray = byteArrayOf(
        SdpWire.InputOp.SWIPE.toByte(),
        dir.toByte(),
    )

    fun button(action: Int): ByteArray = byteArrayOf(
        SdpWire.InputOp.BUTTON.toByte(),
        action.toByte(),
    )

    fun back(): ByteArray = byteArrayOf(SdpWire.InputOp.BACK.toByte())
}
