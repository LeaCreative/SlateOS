package slate.camera

/**
 * ARGB8888 → RGB332 (3-3-2). Phone-side conversion for 60×60 patches (3600 bytes).
 */
object Rgb332 {
    const val FORMAT_ID: Int = 1 // sdp patch_format::RGB332

    fun fromArgb(argb: Int): Int {
        val r = (argb ushr 16) and 0xff
        val g = (argb ushr 8) and 0xff
        val b = argb and 0xff
        return ((r ushr 5) shl 5) or ((g ushr 5) shl 2) or (b ushr 6)
    }

    fun convertArgb8888(src: IntArray, w: Int, h: Int): ByteArray {
        require(src.size >= w * h)
        val out = ByteArray(w * h)
        for (i in 0 until w * h) {
            out[i] = fromArgb(src[i]).toByte()
        }
        return out
    }

    /** Nearest-neighbour downscale then RGB332. */
    fun downscaleArgbToRgb332(
        src: IntArray,
        srcW: Int,
        srcH: Int,
        dstW: Int,
        dstH: Int,
    ): ByteArray {
        require(src.size >= srcW * srcH)
        val out = ByteArray(dstW * dstH)
        for (y in 0 until dstH) {
            val sy = (y * srcH) / dstH
            for (x in 0 until dstW) {
                val sx = (x * srcW) / dstW
                out[y * dstW + x] = fromArgb(src[sy * srcW + sx]).toByte()
            }
        }
        return out
    }
}
