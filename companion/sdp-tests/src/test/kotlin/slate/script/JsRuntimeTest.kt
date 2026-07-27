package slate.script

import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import slate.host.HostInbound
import slate.host.HostOutbound

class JsUiGoldenTest {
    @Test
    fun timerFace_jsMatchesKotlin() {
        RhinoScriptEngine().use { eng ->
            eng.evaluate(ScriptResources.read(ScriptResources.UI_JS))
            val b64 = eng.evaluate(
                """
                slate.ui.displayList(function (b) {
                  b.palette(0, 0x0000);
                  b.palette(1, 0xffff);
                  b.palette(2, 0x07e0);
                  b.clear(slate.PAL(0));
                  b.text(0, 120, 80, 'CENTER', slate.PAL(1), '01:00');
                  b.element(1, 40, 160, 160, 40, function () {
                    b.rectRound(40, 160, 160, 40, 8, slate.PAL(2), slate.FILL);
                    b.text(0, 120, 172, 'CENTER', slate.PAL(0), 'Start');
                  });
                  b.commit();
                });
                """.trimIndent(),
            )
            val jsBytes = java.util.Base64.getDecoder().decode(b64)
            val ktBytes = JsUiScenes.timerFace(remainingSec = 60, running = false)
            assertContentEquals(
                ktBytes,
                jsBytes,
                "JS slate.ui diverged from Kotlin DSL — hex kt=${ktBytes.toHex()} js=${jsBytes.toHex()}",
            )
        }
    }

    private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
}

class GovernorTest {
    @Test
    fun renderOverrunKillsThenDisables() {
        val g = Governor(disableAfterViolations = 3)
        assertFalse(g.checkDuration(Governor.Kind.RenderTimeout, 51, 1))
        assertFalse(g.checkDuration(Governor.Kind.RenderTimeout, 51, 2))
        assertFalse(g.checkDuration(Governor.Kind.RenderTimeout, 51, 3))
        assertTrue(g.isDisabled)
    }

    @Test
    fun timerMinimumOneSecond() {
        val g = Governor()
        assertFalse(g.checkTimerInterval(999, 0))
        assertTrue(g.checkTimerInterval(1000, 0))
    }
}

class JsLifecycleTest {
    @Test
    fun timerApp_focusRenderInputPersist() = runBlocking {
        val timers = mutableMapOf<String, Long>()
        RhinoScriptEngine().use { eng ->
            val ep = JsSlateAppEndpoint.loadTimer(
                engine = eng,
                hostHeld = setOf(ScriptPermission.Storage),
                onTimerSet = { id, ms -> timers[id] = ms },
                onTimerClear = { id -> timers.remove(id) },
            )
            ep.installRuntime(appJs = ScriptResources.read(ScriptResources.TIMER_MAIN))
            val focusOut = ep.dispatch(HostInbound.Focus)
            assertTrue(focusOut.any { it is HostOutbound.PushDisplayList })
            val push = focusOut.filterIsInstance<HostOutbound.PushDisplayList>().first()
            assertContentEquals(JsUiScenes.timerFace(60, false), push.bytes)

            val tap = HostInbound.Input(op = 0x01, elemId = 1, x = 120, y = 180)
            val inOut = ep.dispatch(tap)
            assertTrue(inOut.any { it is HostOutbound.InputHandled })
            assertEquals(1000L, timers["tick"])

            ep.dispatch(HostInbound.SystemEvent("timer", """{"id":"tick"}"""))
            val render = ep.dispatch(HostInbound.Render)
            val after = render.filterIsInstance<HostOutbound.PushDisplayList>().firstOrNull()
                ?: error("expected push after tick")
            assertContentEquals(JsUiScenes.timerFace(59, true), after.bytes)
        }
    }
}
