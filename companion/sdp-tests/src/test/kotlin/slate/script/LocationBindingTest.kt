package slate.script

import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import slate.host.HostInbound
import slate.host.HostOutbound
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * The `slate.location` binding and the sub-app that uses it.
 *
 * The Android half (`LocationAdapter`) cannot run here — it needs a real
 * `LocationManager`. What this pins is everything either side of it: that the
 * permission gate actually bites, that the commands the script emits are the
 * ones `CompositorHost.handleLocationAdapter` switches on, and that the demo
 * survives every payload the host can hand it, including the states where no
 * fix will ever arrive.
 */
class LocationBindingTest {

    private fun manifest(perms: Set<ScriptPermission>) = ScriptManifest(
        id = "test.location",
        name = "Location test",
        permissions = perms,
    )

    private suspend fun endpoint(
        eng: RhinoScriptEngine,
        perms: Set<ScriptPermission>,
        appJs: String = ScriptResources.read(ScriptResources.LOCATION_MAIN),
    ): JsSlateAppEndpoint {
        val ep = JsSlateAppEndpoint(scriptManifest = manifest(perms), engine = eng)
        ep.installRuntime(appJs = appJs)
        return ep
    }

    private fun adapterCommands(out: List<HostOutbound>) =
        out.filterIsInstance<HostOutbound.AdapterCommand>().filter { it.adapter == "location" }

    /**
     * Without the permission the command must be dropped before it reaches the
     * host. This is the test that matters most: the binding exists in every
     * isolate, so the gate is the only thing standing between a downloaded
     * script and the user's position.
     */
    @Test
    fun locationCommandIsDroppedWithoutPermission() = runBlocking {
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Storage))
            val out = ep.dispatch(HostInbound.Focus)
            assertTrue(
                adapterCommands(out).isEmpty(),
                "a script without the location permission got a location command through",
            )
        }
    }

    @Test
    fun locationCommandPassesWithPermission() = runBlocking {
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Location, ScriptPermission.Storage))
            val cmds = adapterCommands(ep.dispatch(HostInbound.Focus))
            assertEquals(1, cmds.size, "expected exactly one location command on focus")
            assertEquals("subscribe", cmds.first().command)
            // The interval the host will floor at 1000 ms — see
            // LocationAdapter.MIN_INTERVAL_MS. Default setting is 5 s.
            val payload = JSONObject(cmds.first().payloadJson)
            assertEquals(5000, payload.getInt("minIntervalMs"))
        }
    }

    /**
     * Every command the script can emit must be one the host switches on.
     * A typo here is silent: `handleLocationAdapter` ignores unknown commands,
     * so the app would simply never receive a fix.
     */
    @Test
    fun everyEmittedCommandIsOneTheHostHandles() = runBlocking {
        val handled = setOf("subscribe", "unsubscribe", "request")
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Location, ScriptPermission.Storage))
            val seen = mutableSetOf<String>()
            seen += adapterCommands(ep.dispatch(HostInbound.Focus)).map { it.command }
            seen += adapterCommands(
                ep.dispatch(HostInbound.Input(op = 0x05, elemId = 1, x = 120, y = 170)),
            ).map { it.command }
            seen += adapterCommands(
                ep.dispatch(HostInbound.Input(op = 0x06, elemId = 0, x = 0, y = 0)),
            ).map { it.command }
            seen += adapterCommands(ep.dispatch(HostInbound.Blur)).map { it.command }
            assertEquals(handled, seen, "script emits commands the host does not handle, or vice versa")
        }
    }

    /**
     * Terminal states must not leave a stale fix on screen. If the stream has
     * stopped, coordinates that are no longer updating are worse than none —
     * the user has no way to tell they have gone stale.
     */
    @Test
    fun terminalStatusClearsAnyFixOnScreen() = runBlocking {
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Location, ScriptPermission.Storage))
            ep.dispatch(HostInbound.Focus)
            val withFix = ep.dispatch(
                HostInbound.SystemEvent(
                    "location",
                    """{"type":"fix","lat":-4.61667,"lon":55.45,"accuracyM":12.5,"provider":"gps"}""",
                ),
            ).filterIsInstance<HostOutbound.PushDisplayList>().first().bytes
            assertTrue(String(withFix, Charsets.ISO_8859_1).contains("-4.61667"), "fix not drawn")

            val afterDenied = ep.dispatch(
                HostInbound.SystemEvent("location", """{"type":"status","state":"denied"}"""),
            ).filterIsInstance<HostOutbound.PushDisplayList>().first().bytes
            val text = String(afterDenied, Charsets.ISO_8859_1)
            assertFalse(text.contains("-4.61667"), "stale coordinates survived a terminal status")
            assertTrue(text.contains("No permission"), "denied state not explained on screen")
        }
    }

    /**
     * A fix with no usable coordinates must draw nothing rather than 0,0 —
     * which is a real-looking position in the Gulf of Guinea.
     */
    @Test
    fun fixWithoutCoordinatesIsRejected() = runBlocking {
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Location, ScriptPermission.Storage))
            ep.dispatch(HostInbound.Focus)
            val out = ep.dispatch(
                HostInbound.SystemEvent("location", """{"type":"fix","provider":"gps"}"""),
            )
            assertTrue(
                out.filterIsInstance<HostOutbound.PushDisplayList>().isEmpty(),
                "a fix with no coordinates was drawn",
            )
        }
    }

    /** Worst-case screen stays well inside the §2 practical limit. */
    @Test
    fun worstCaseScreenIsWithinBudget() = runBlocking {
        RhinoScriptEngine().use { eng ->
            val ep = endpoint(eng, setOf(ScriptPermission.Location, ScriptPermission.Storage))
            ep.dispatch(HostInbound.Focus)
            val bytes = ep.dispatch(
                HostInbound.SystemEvent(
                    "location",
                    """{"type":"fix","lat":-179.12345,"lon":-179.98765,""" +
                        """"accuracyM":9999,"provider":"network"}""",
                ),
            ).filterIsInstance<HostOutbound.PushDisplayList>().first().bytes
            assertTrue(bytes.size <= 2048, "worst-case location screen is ${bytes.size} B")
        }
    }
}
