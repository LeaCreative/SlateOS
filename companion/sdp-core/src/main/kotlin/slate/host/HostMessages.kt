package slate.host

/**
 * Process-boundary-safe host ↔ app contract (M8).
 *
 * All payloads are primitives, strings, or byte arrays. No Android Framework
 * objects, no shared mutable references, no synchronous callbacks into the
 * compositor's threads from app code — apps answer a dispatch and return
 * outbound messages (or a Kotlin/JS adapter translates the same shapes).
 *
 * Wire form for JS (androidx.javascriptengine) is JSON + base64 display lists;
 * see `docs/compositor.md`.
 */
sealed class HostInbound {
    data class Create(val watchProtocolVersion: Int) : HostInbound()
    data object Start : HostInbound()
    data object Focus : HostInbound()
    data object Blur : HostInbound()
    data object Stop : HostInbound()
    data object Destroy : HostInbound()

    /** Compositor asks for a fresh list (periodic / coalesced invalidate). */
    data object Render : HostInbound()

    /**
     * Decoded SDP input (§4.4). Coordinates and enums are plain ints matching
     * `sdp_opcodes` / `SdpWire` — never raw GATT buffers.
     */
    data class Input(
        val op: Int,
        val elemId: Int = 0xFFFF,
        val x: Int = 0,
        val y: Int = 0,
        val dir: Int = 0,
        val action: Int = 0,
        val count: Int = 0,
        val edge: Int = 0,
        val distance: Int = 0,
        val durationMs: Int = 0,
        val reason: Int = 0,
    ) : HostInbound()

    /** Adapter / system event (notification, media, …) — opaque JSON string. */
    data class SystemEvent(val source: String, val jsonPayload: String) : HostInbound()
}

sealed class HostOutbound {
    /** Full SDP display-list bytes (channel 1 payload). */
    data class PushDisplayList(val bytes: ByteArray) : HostOutbound() {
        override fun equals(other: Any?): Boolean =
            other is PushDisplayList && bytes.contentEquals(other.bytes)

        override fun hashCode(): Int = bytes.contentHashCode()
    }

    /** Mark dirty under OnChange / Periodic — compositor will Render later. */
    data object Invalidate : HostOutbound()

    /** Request compositor focus at [priority] (may be denied). */
    data class RequestFocus(val priority: PriorityClass) : HostOutbound()

    data object RelinquishFocus : HostOutbound()

    /** Input was handled; stop fallback routing. */
    data object InputHandled : HostOutbound()

    /** Input not handled; compositor may fall back. */
    data object InputUnhandled : HostOutbound()

    data class Log(val level: String, val message: String) : HostOutbound()

    /**
     * Host-side adapter command (notifications dismiss, media skip, …).
     * JSON payload — process-boundary safe for a future JS isolate.
     */
    data class AdapterCommand(
        val adapter: String,
        val command: String,
        val payloadJson: String = "{}",
    ) : HostOutbound()
}

/**
 * Endpoint an app runtime exposes to the compositor.
 *
 * Implementations MUST be side-effect-free w.r.t. Android UI and MUST NOT retain
 * Host/Compositor references beyond the call. State lives inside the endpoint
 * (or JS isolate); communication is only via [dispatch] return values.
 */
interface SlateAppEndpoint {
    val manifest: AppManifest

    /**
     * Handle one inbound message; return zero or more outbound messages.
     * Always async-friendly (suspend). Never blocks on BLE or disk without
     * going through host-provided bindings (future JS sandbox).
     */
    suspend fun dispatch(msg: HostInbound): List<HostOutbound>
}
