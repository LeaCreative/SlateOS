package slate.compositor

import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.host.SlateAppEndpoint
import slate.host.UpdateWatchScreen

/**
 * Screen stack, focus arbitration, credit/quota gating, and render coalescing.
 *
 * Transport is injected: [pushToWatch] must send DISPLAY bytes (and optional
 * SCREEN_PUSH/REPLACE CONTROL). No Android types here.
 */
class Compositor(
    private val nowMs: () -> Long = { System.currentTimeMillis() },
    private val pushToWatch: suspend (bytes: ByteArray) -> Boolean,
    private val onAdapterCommand: (HostOutbound.AdapterCommand) -> Unit = {},
    private val onScreenStackOp: (StackOp) -> Unit = {},
    private val onScreenPop: () -> Unit = {},
) {
    data class StackEntry(
        val appId: String,
        val priority: PriorityClass,
    )

    private data class Registered(
        val endpoint: SlateAppEndpoint,
        var dirty: Boolean = false,
        var lastPeriodicMs: Long = 0L,
        var created: Boolean = false,
        var started: Boolean = false,
    )

    private val apps = LinkedHashMap<String, Registered>()
    private val stack = ArrayList<StackEntry>()
    private val credit = CreditWindow()
    private val quota = PushQuota(nowMs)

    @Volatile
    var watchProtocolVersion: Int = 1

    @Volatile
    var linkConnected: Boolean = false

    val stackSnapshot: List<StackEntry> get() = stack.toList()
    val focusedAppId: String? get() = stack.lastOrNull()?.appId
    val freeCreditBytes: Int get() = credit.freeBytes

    fun setCredit(freeBytes: Int) = credit.setAdvertised(freeBytes)

    fun register(endpoint: SlateAppEndpoint) {
        apps[endpoint.manifest.id] = Registered(endpoint)
    }

    fun unregister(appId: String) {
        apps.remove(appId)
        stack.removeAll { it.appId == appId }
        quota.reset(appId)
    }

    suspend fun ensureStarted(appId: String) {
        val reg = apps[appId] ?: return
        if (!reg.created) {
            applyOutbound(reg, reg.endpoint.dispatch(HostInbound.Create(watchProtocolVersion)))
            reg.created = true
        }
        if (!reg.started) {
            applyOutbound(reg, reg.endpoint.dispatch(HostInbound.Start))
            reg.started = true
        }
    }

    /**
     * Request focus for [appId]. Returns null on success, or a deny reason.
     * On protocol mismatch, pushes the update-watch screen and denies focus.
     */
    suspend fun requestFocus(
        appId: String,
        priority: PriorityClass? = null,
        reason: FocusReason = FocusReason.SystemRaise,
        op: StackOp = StackOp.Push,
    ): FocusDenyReason? {
        if (!linkConnected) return FocusDenyReason.NotConnected
        val reg = apps[appId] ?: return FocusDenyReason.UnknownApp
        val prio = priority ?: reg.endpoint.manifest.defaultPriority
        val required = reg.endpoint.manifest.minProtocolVersion
        if (required > watchProtocolVersion) {
            val bytes = UpdateWatchScreen.displayListBytes(
                reg.endpoint.manifest.name,
                required,
                watchProtocolVersion,
            )
            maybePush(appId = SYSTEM_UI_ID, priority = PriorityClass.NORMAL, focused = true, bytes)
            return FocusDenyReason.ProtocolTooOld
        }

        val current = stack.lastOrNull()
        val currentPrio = current?.priority
        val effectiveReason =
            if (current?.appId == appId) FocusReason.SameApp else reason
        if (!FocusRules.canSteal(prio, currentPrio, effectiveReason)) {
            return FocusDenyReason.PriorityTooLow
        }

        ensureStarted(appId)
        onScreenStackOp(op)

        when (op) {
            StackOp.Push -> {
                if (current != null && current.appId != appId) {
                    blur(current.appId)
                }
                if (stack.lastOrNull()?.appId != appId) {
                    // Ambient: replace existing ambient base instead of stacking.
                    if (prio == PriorityClass.AMBIENT) {
                        stack.removeAll { it.priority == PriorityClass.AMBIENT }
                        stack.add(0, StackEntry(appId, prio))
                    } else {
                        stack.add(StackEntry(appId, prio))
                    }
                }
            }
            StackOp.Replace -> {
                if (stack.isNotEmpty()) {
                    val top = stack.removeAt(stack.lastIndex)
                    if (top.appId != appId) {
                        blur(top.appId)
                        if (top.priority != PriorityClass.AMBIENT) {
                            // stop non-ambient replaced entry
                            apps[top.appId]?.let { r ->
                                applyOutbound(r, r.endpoint.dispatch(HostInbound.Stop))
                                r.started = false
                            }
                        }
                    }
                }
                stack.add(StackEntry(appId, prio))
            }
        }

        applyOutbound(reg, reg.endpoint.dispatch(HostInbound.Focus))
        reg.dirty = true
        flushApp(appId)
        return null
    }

    suspend fun relinquishFocus(appId: String) {
        if (stack.lastOrNull()?.appId != appId) return
        stack.removeAt(stack.lastIndex)
        onScreenPop()
        blur(appId)
        val next = stack.lastOrNull() ?: return
        apps[next.appId]?.let { reg ->
            applyOutbound(reg, reg.endpoint.dispatch(HostInbound.Focus))
            reg.dirty = true
            flushApp(next.appId)
        }
    }

    /**
     * Drop every screen without telling the watch.
     *
     * For link loss. The watch falls back to its own face the moment the
     * session ends (and again on reboot), so a stack held past that point is a
     * belief about a screen that no longer exists. Left stale it does real
     * damage: a reserved gesture that asks "is the launcher already focused?"
     * answers yes forever, and the gesture stops working until the app is
     * restarted.
     *
     * No SCREEN_POP is sent — there is nobody to send it to.
     */
    suspend fun resetStack() {
        while (stack.isNotEmpty()) {
            val top = stack.removeAt(stack.lastIndex)
            blur(top.appId)
        }
    }

    suspend fun dispatchSystemEvent(appId: String, source: String, jsonPayload: String) {
        val reg = apps[appId] ?: return
        applyOutbound(reg, reg.endpoint.dispatch(HostInbound.SystemEvent(source, jsonPayload)))
    }

    suspend fun dispatchInput(input: HostInbound.Input) {
        val focusId = focusedAppId ?: return
        val reg = apps[focusId] ?: return
        val out = reg.endpoint.dispatch(input)
        val handled = out.any { it is HostOutbound.InputHandled }
        applyOutbound(reg, out.filterNot {
            it is HostOutbound.InputHandled || it is HostOutbound.InputUnhandled
        })
        if (handled) return

        // Fallback: BACK pops the stack; otherwise offer to ambient base.
        // Pop the last app too. Requiring size > 1 meant a single focused app
        // could not be dismissed, so the watch stayed on that remote screen
        // forever — the user's only way back to the watch face was to drop the
        // link. The watch now pops locally regardless (session::local_back);
        // this keeps the host's view in step rather than leaving it believing
        // it still owns a screen it no longer has.
        if (input.op == slate.generated.SdpWire.InputOp.BACK && stack.isNotEmpty()) {
            relinquishFocus(focusId)
            return
        }
        val ambient = stack.firstOrNull { it.priority == PriorityClass.AMBIENT } ?: return
        if (ambient.appId == focusId) return
        val areg = apps[ambient.appId] ?: return
        applyOutbound(areg, areg.endpoint.dispatch(input).filterNot {
            it is HostOutbound.InputHandled || it is HostOutbound.InputUnhandled
        })
    }

    /** Scheduler tick — coalesces dirty apps and fires periodic refresh. */
    suspend fun tick() {
        if (!linkConnected) return
        val t = nowMs()
        for ((id, reg) in apps) {
            when (val policy = reg.endpoint.manifest.refresh) {
                is RefreshPolicy.Periodic -> {
                    if (t - reg.lastPeriodicMs >= policy.intervalMs) {
                        reg.lastPeriodicMs = t
                        reg.dirty = true
                    }
                }
                else -> Unit
            }
        }
        val focus = focusedAppId
        if (focus != null) flushApp(focus)
        // Ambient may still update when buried (1/min quota).
        val ambientId = stack.firstOrNull { it.priority == PriorityClass.AMBIENT }?.appId
        if (ambientId != null && ambientId != focus) {
            val reg = apps[ambientId] ?: return
            if (reg.dirty) flushApp(ambientId)
        }
    }

    private suspend fun blur(appId: String) {
        val reg = apps[appId] ?: return
        applyOutbound(reg, reg.endpoint.dispatch(HostInbound.Blur))
    }

    private suspend fun applyOutbound(reg: Registered, messages: List<HostOutbound>) {
        for (m in messages) {
            when (m) {
                is HostOutbound.PushDisplayList -> {
                    reg.dirty = false
                    val entry = stack.find { it.appId == reg.endpoint.manifest.id }
                    val focused = focusedAppId == reg.endpoint.manifest.id
                    val prio = entry?.priority ?: reg.endpoint.manifest.defaultPriority
                    maybePush(reg.endpoint.manifest.id, prio, focused, m.bytes)
                }
                HostOutbound.Invalidate -> {
                    if (reg.endpoint.manifest.refresh !is RefreshPolicy.Manual) {
                        reg.dirty = true
                    }
                }
                is HostOutbound.RequestFocus -> {
                    requestFocus(reg.endpoint.manifest.id, m.priority, FocusReason.SystemRaise)
                }
                HostOutbound.RelinquishFocus -> relinquishFocus(reg.endpoint.manifest.id)
                is HostOutbound.Log -> Unit
                is HostOutbound.AdapterCommand -> onAdapterCommand(m)
                HostOutbound.InputHandled, HostOutbound.InputUnhandled -> Unit
            }
        }
    }

    private suspend fun flushApp(appId: String) {
        val reg = apps[appId] ?: return
        if (!reg.dirty) return
        if (reg.endpoint.manifest.refresh is RefreshPolicy.Manual) {
            // Manual only pushes via explicit PushDisplayList.
            return
        }
        val out = reg.endpoint.dispatch(HostInbound.Render)
        applyOutbound(reg, out)
        // If render produced nothing, clear dirty to avoid hot loop.
        if (out.none { it is HostOutbound.PushDisplayList }) {
            reg.dirty = false
        }
    }

    /**
     * Privileged host push (camera PATCH stream). Only while [appId] is focused;
     * still subject to credit / quota.
     */
    suspend fun pushHostDisplayList(appId: String, bytes: ByteArray): Boolean {
        if (focusedAppId != appId) return false
        val entry = stack.lastOrNull() ?: return false
        return maybePush(appId, entry.priority, focused = true, bytes)
    }

    private suspend fun maybePush(
        appId: String,
        priority: PriorityClass,
        focused: Boolean,
        bytes: ByteArray,
    ): Boolean {
        if (!linkConnected && appId != SYSTEM_UI_ID) return false
        if (appId != SYSTEM_UI_ID) {
            if (!quota.tryConsume(appId, priority, focused || priority == PriorityClass.AMBIENT)) {
                return false
            }
        }
        if (!credit.tryReserve(bytes.size)) {
            return false
        }
        val ok = pushToWatch(bytes)
        if (!ok) {
            credit.refund(bytes.size)
        }
        return ok
    }

    companion object {
        const val SYSTEM_UI_ID = "slate.system.ui"
    }
}
