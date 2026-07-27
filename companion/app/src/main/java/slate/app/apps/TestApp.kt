package slate.app.apps

import slate.dsl.displayList
import slate.host.AppManifest
import slate.host.HostOutbound
import slate.host.KotlinSlateApp
import slate.host.PriorityClass
import slate.host.RefreshPolicy
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.pal

/** No-op reference app — proves lifecycle + focus without radio chatter. */
class TestApp : KotlinSlateApp() {
    override val manifest = AppManifest(
        id = "slate.ref.test",
        name = "Test",
        version = "1.0.0",
        minProtocolVersion = 1,
        defaultPriority = PriorityClass.NORMAL,
        refresh = RefreshPolicy.Manual,
    )

    override fun onFocus(out: MutableList<HostOutbound>) {
        out.push(
            displayList {
                palette(0, Colors.BLACK)
                palette(1, Colors.WHITE)
                clear(pal(0))
                text(
                    font = Font.LARGE,
                    x = 120,
                    y = 110,
                    align = Align.CENTER,
                    color = pal(1),
                    text = "TestApp",
                )
                commit()
            },
        )
    }
}
