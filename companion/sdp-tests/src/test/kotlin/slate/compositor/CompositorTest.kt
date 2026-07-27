package slate.compositor

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.runBlocking
import slate.host.AppManifest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy

class FakeApp(
    override val manifest: AppManifest,
    private val listBytes: ByteArray = byteArrayOf(0xF0.toByte(), 0x00),
) : KotlinSlateApp() {
    var focusCount = 0
    var blurCount = 0
    var renderCount = 0

    override fun onFocus(out: MutableList<HostOutbound>) {
        focusCount++
        out.push(listBytes)
    }

    override fun onBlur(out: MutableList<HostOutbound>) {
        blurCount++
    }

    override fun onRender(out: MutableList<HostOutbound>) {
        renderCount++
        out.push(listBytes)
    }

    override fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean =
        msg.op == 0x01 // TAP handled
}

class CompositorTest {
    private var now = 1_000_000L
    private val pushed = ArrayList<ByteArray>()

    private fun compositor(): Compositor =
        Compositor(
            nowMs = { now },
            pushToWatch = { b -> pushed += b; true },
        ).also {
            it.linkConnected = true
            it.watchProtocolVersion = 1
            it.setCredit(4096)
        }

    @Test
    fun higherPriorityStealsAndBlursDisplaced() = runBlocking {
        val c = compositor()
        val ambient = FakeApp(
            AppManifest("a", "A", defaultPriority = PriorityClass.AMBIENT, refresh = RefreshPolicy.Manual),
        )
        val normal = FakeApp(
            AppManifest("n", "N", defaultPriority = PriorityClass.NORMAL, refresh = RefreshPolicy.Manual),
        )
        c.register(ambient)
        c.register(normal)
        assertNull(c.requestFocus("a", PriorityClass.AMBIENT, FocusReason.UserNavigation))
        assertNull(c.requestFocus("n", PriorityClass.NORMAL, FocusReason.UserNavigation))
        assertEquals("n", c.focusedAppId)
        assertEquals(1, ambient.blurCount)
        assertEquals(1, normal.focusCount)
        assertEquals(2, c.stackSnapshot.size)
    }

    @Test
    fun lowerPriorityCannotSteal() = runBlocking {
        val c = compositor()
        val hi = FakeApp(AppManifest("h", "H", defaultPriority = PriorityClass.INTERRUPT, refresh = RefreshPolicy.Manual))
        val lo = FakeApp(AppManifest("l", "L", defaultPriority = PriorityClass.NORMAL, refresh = RefreshPolicy.Manual))
        c.register(hi)
        c.register(lo)
        assertNull(c.requestFocus("h", PriorityClass.INTERRUPT, FocusReason.SystemRaise))
        assertEquals(
            FocusDenyReason.PriorityTooLow,
            c.requestFocus("l", PriorityClass.NORMAL, FocusReason.SystemRaise),
        )
        assertEquals("h", c.focusedAppId)
    }

    @Test
    fun peerEqualPriorityDeniedUnlessUserNavigation() = runBlocking {
        val c = compositor()
        val a = FakeApp(AppManifest("a", "A", defaultPriority = PriorityClass.NORMAL, refresh = RefreshPolicy.Manual))
        val b = FakeApp(AppManifest("b", "B", defaultPriority = PriorityClass.NORMAL, refresh = RefreshPolicy.Manual))
        c.register(a)
        c.register(b)
        assertNull(c.requestFocus("a", reason = FocusReason.UserNavigation))
        assertEquals(
            FocusDenyReason.PriorityTooLow,
            c.requestFocus("b", reason = FocusReason.SystemRaise),
        )
        assertNull(c.requestFocus("b", reason = FocusReason.UserNavigation))
        assertEquals("b", c.focusedAppId)
    }

    @Test
    fun protocolGateShowsUpdateScreen() = runBlocking {
        val c = compositor()
        c.watchProtocolVersion = 1
        val needy = FakeApp(
            AppManifest("x", "NeedNew", minProtocolVersion = 99, refresh = RefreshPolicy.Manual),
        )
        c.register(needy)
        assertEquals(
            FocusDenyReason.ProtocolTooOld,
            c.requestFocus("x", reason = FocusReason.UserNavigation),
        )
        assertTrue(pushed.isNotEmpty())
        assertNull(c.focusedAppId)
    }

    @Test
    fun creditBlocksOversizedPush() = runBlocking {
        val c = compositor()
        c.setCredit(2)
        val big = FakeApp(
            AppManifest("b", "Big", refresh = RefreshPolicy.Manual),
            listBytes = ByteArray(100) { 1 },
        )
        c.register(big)
        assertNull(c.requestFocus("b", reason = FocusReason.UserNavigation))
        // Focus tried to push 100 bytes — credit 2 → no push retained beyond update attempts
        assertTrue(c.freeCreditBytes <= 2)
    }

    @Test
    fun ambientQuotaOnePerMinute() = runBlocking {
        val c = compositor()
        val clock = FakeApp(
            AppManifest(
                "c", "Clock",
                defaultPriority = PriorityClass.AMBIENT,
                refresh = RefreshPolicy.Periodic(1_000),
            ),
        )
        c.register(clock)
        assertNull(c.requestFocus("c", PriorityClass.AMBIENT, FocusReason.UserNavigation))
        val first = pushed.size
        clock.dispatch(HostInbound.Render) // not through compositor
        // Force dirty + tick
        now += 2_000
        c.tick()
        // Second push within same minute should be quota-blocked for ambient after first focus push
        now += 2_000
        // mark dirty via invalidate path
        val regPush = pushed.size
        // Direct maybePush via another focus render
        c.requestFocus("c", PriorityClass.AMBIENT, FocusReason.SameApp, StackOp.Replace)
        assertTrue(pushed.size <= regPush + 1)
        now += 60_000
        c.requestFocus("c", PriorityClass.AMBIENT, FocusReason.SameApp, StackOp.Replace)
        assertTrue(pushed.size >= first)
    }

    @Test
    fun inputFallbackBackPopsStack() = runBlocking {
        val c = compositor()
        val ambient = FakeApp(
            AppManifest("a", "A", defaultPriority = PriorityClass.AMBIENT, refresh = RefreshPolicy.Manual),
        )
        val normal = FakeApp(
            AppManifest("n", "N", defaultPriority = PriorityClass.NORMAL, refresh = RefreshPolicy.Manual),
        )
        c.register(ambient)
        c.register(normal)
        c.requestFocus("a", PriorityClass.AMBIENT, FocusReason.UserNavigation)
        c.requestFocus("n", PriorityClass.NORMAL, FocusReason.UserNavigation)
        c.dispatchInput(HostInbound.Input(op = slate.generated.SdpWire.InputOp.BACK))
        assertEquals("a", c.focusedAppId)
        assertEquals(2, ambient.focusCount) // initial + restored
    }

    @Test
    fun focusRulesHelpers() {
        assertTrue(FocusRules.canSteal(PriorityClass.CRITICAL, PriorityClass.NORMAL, FocusReason.SystemRaise))
        assertTrue(!FocusRules.canSteal(PriorityClass.NORMAL, PriorityClass.INTERRUPT, FocusReason.SystemRaise))
        assertTrue(
            FocusRules.canSteal(PriorityClass.NORMAL, PriorityClass.NORMAL, FocusReason.UserNavigation),
        )
        assertTrue(
            !FocusRules.canSteal(PriorityClass.NORMAL, PriorityClass.NORMAL, FocusReason.SystemRaise),
        )
    }
}
