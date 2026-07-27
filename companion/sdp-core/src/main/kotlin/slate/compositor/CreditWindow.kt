package slate.compositor

/**
 * Credit-based DISPLAY flow control against the watch's advertised free buffer
 * (§4.7). Compositor never pushes a list larger than [freeBytes].
 */
class CreditWindow {
    @Volatile
    var freeBytes: Int = 4096
        private set

    fun setAdvertised(bytes: Int) {
        freeBytes = bytes.coerceAtLeast(0)
    }

    /** Reserve [size] bytes; returns false if insufficient credit. */
    fun tryReserve(size: Int): Boolean {
        if (size <= 0) return true
        if (size > freeBytes) return false
        freeBytes -= size
        return true
    }

    fun refund(size: Int) {
        if (size > 0) freeBytes += size
    }
}
