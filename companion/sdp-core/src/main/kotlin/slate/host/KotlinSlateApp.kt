package slate.host

/**
 * Convenience base for in-process Kotlin reference apps.
 *
 * Still process-boundary-safe: subclasses never receive Android types; they
 * only override lifecycle hooks and return [HostOutbound] via helpers.
 * A JS isolate would implement [SlateAppEndpoint] with a JSON bridge instead.
 */
abstract class KotlinSlateApp : SlateAppEndpoint {
    private var focused = false
    private var started = false

    protected val isFocused: Boolean get() = focused
    protected val isStarted: Boolean get() = started

    final override suspend fun dispatch(msg: HostInbound): List<HostOutbound> {
        val out = ArrayList<HostOutbound>()
        when (msg) {
            is HostInbound.Create -> onCreate(msg.watchProtocolVersion, out)
            HostInbound.Start -> {
                started = true
                onStart(out)
            }
            HostInbound.Focus -> {
                focused = true
                onFocus(out)
            }
            HostInbound.Blur -> {
                focused = false
                onBlur(out)
            }
            HostInbound.Stop -> {
                started = false
                onStop(out)
            }
            HostInbound.Destroy -> {
                focused = false
                started = false
                onDestroy(out)
            }
            HostInbound.Render -> onRender(out)
            is HostInbound.Input -> {
                val handled = onInput(msg, out)
                out += if (handled) HostOutbound.InputHandled else HostOutbound.InputUnhandled
            }
            is HostInbound.SystemEvent -> onSystemEvent(msg, out)
        }
        return out
    }

    protected open fun onCreate(watchProtocolVersion: Int, out: MutableList<HostOutbound>) {}
    protected open fun onStart(out: MutableList<HostOutbound>) {}
    protected open fun onFocus(out: MutableList<HostOutbound>) {}
    protected open fun onBlur(out: MutableList<HostOutbound>) {}
    protected open fun onStop(out: MutableList<HostOutbound>) {}
    protected open fun onDestroy(out: MutableList<HostOutbound>) {}
    protected open fun onRender(out: MutableList<HostOutbound>) {}
    /** @return true if handled */
    protected open fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean = false
    protected open fun onSystemEvent(msg: HostInbound.SystemEvent, out: MutableList<HostOutbound>) {}

    protected fun MutableList<HostOutbound>.push(bytes: ByteArray) {
        add(HostOutbound.PushDisplayList(bytes))
    }

    protected fun MutableList<HostOutbound>.invalidate() {
        add(HostOutbound.Invalidate)
    }
}
