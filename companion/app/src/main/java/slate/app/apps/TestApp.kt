package slate.app.apps

import slate.app.link.LinkLog
import slate.dsl.displayList
import slate.generated.SdpWire
import slate.host.AppManifest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.pal

/**
 * Reference app — the end-to-end proof that the phone can compose a screen and
 * the watch can render it and answer.
 *
 * Deliberately minimal: a CLEAR, one filled rect inside a touch-emitting
 * element, and text. Tapping the rect increments a counter and repaints, so a
 * full round-trip (phone → watch → phone → watch) is visible on the watch
 * itself without any instrumentation.
 */
class TestApp : KotlinSlateApp() {
    override val manifest = AppManifest(
        id = "slate.ref.test",
        name = "Test",
        version = "1.0.0",
        minProtocolVersion = 1,
        defaultPriority = PriorityClass.NORMAL,
        refresh = RefreshPolicy.Manual,
    )

    private var taps = 0

    override fun onFocus(out: MutableList<HostOutbound>) {
        taps = 0
        LinkLog.i("TestApp: onFocus — building screen (black bg, white 160x64 rect, taps=0)")
        out.push(render())
    }

    override fun onInput(msg: HostInbound.Input, out: MutableList<HostOutbound>): Boolean {
        LinkLog.i("TestApp: input op=${msg.op} elem=${msg.elemId}")
        if (msg.op != SdpWire.InputOp.TAP || msg.elemId != ELEM_BUTTON) return false
        taps++
        LinkLog.i("TestApp: tap accepted — repainting, taps=$taps")
        out.push(render())
        return true
    }

    override fun onBlur(out: MutableList<HostOutbound>) {
        // Which side gave up the screen is the open question (N-29 follow-up):
        // this line firing means the *host* dropped focus, not the watch.
        LinkLog.i("TestApp: onBlur — lost focus after $taps tap(s)")
    }

    private fun render() = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        clear(pal(0))
        // Scale 3: the base 3x5 font is unreadable on the panel. Font 0 has no
        // letters, so this shows as glyph boxes — the digits below are the part
        // that has to be readable.
        textScaled(
            x = 120,
            y = 30,
            align = Align.CENTER,
            color = pal(1),
            scale = 3,
            text = "8888",
        )
        // The tap target. EMIT_TOUCH is what makes the watch report the element
        // id back on channel 2; without it the rect is inert.
        element(
            id = ELEM_BUTTON,
            x = 40, y = 96, w = 160, h = 64,
            flags = SdpWire.ElemFlags.EMIT_TOUCH,
        ) {
            rect(x = 40, y = 96, w = 160, h = 64, color = pal(1))
        }
        // Tap count, below the target, at clock size so a round-trip is
        // readable across the room.
        textScaled(
            x = 120,
            y = 176,
            align = Align.CENTER,
            color = pal(1),
            scale = 6,
            text = taps.toString(),
        )
        commit()
    }

    private companion object {
        const val ELEM_BUTTON = 1
    }
}
