package slate.script

import kotlinx.coroutines.runBlocking
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.repo.ManifestParser
import slate.repo.PackageManifest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/**
 * Mechanical half of the `docs/subapp-rules.md` §6 review checklist, applied to
 * every bundled demo.
 *
 * The manifest of a bundled demo is parsed on device by `BundledPackageSeeder`
 * at seed time, inside `ensureRegistered`. A malformed one — an unknown
 * permission id, an unknown `requires` token, a missing required field —
 * therefore throws on the phone during the first launch after install, not
 * here, where it is free to find. `examples/` is the source of truth for these
 * files; `sdp-core`'s `processResources` copies them onto the classpath.
 *
 * What this does NOT check is the half of the checklist that needs judgement or
 * hardware: whether the header describes what the app actually draws, whether a
 * loop is bounded, and whether the watch survived opening it.
 */
class BundledSubAppConformanceTest {
    private data class Demo(val manifestRes: String, val expectedId: String)

    private val demos = listOf(
        Demo(ScriptResources.TIMER_MANIFEST, "slate.timer"),
        Demo(ScriptResources.NAV_MANIFEST, "slate.navigation"),
        Demo(ScriptResources.CAMERA_MANIFEST, "slate.camera"),
        Demo(ScriptResources.VIBRATE_MANIFEST, "slate.vibrate"),
        Demo(ScriptResources.LOCATION_MANIFEST, "slate.location"),
        Demo(ScriptResources.MAP_MANIFEST, "slate.map"),
        Demo(ScriptResources.NEWS_MANIFEST, "slate.news"),
        Demo(ScriptResources.MEDIA_MANIFEST, "slate.media"),
        Demo(ScriptResources.WEATHER_MANIFEST, "slate.weather"),
        Demo(ScriptResources.HTTPDEMO_MANIFEST, "slate.httpdemo"),
    )

    private val entryScripts = mapOf(
        ScriptResources.TIMER_MANIFEST to ScriptResources.TIMER_MAIN,
        ScriptResources.NAV_MANIFEST to ScriptResources.NAV_MAIN,
        ScriptResources.CAMERA_MANIFEST to ScriptResources.CAMERA_MAIN,
        ScriptResources.VIBRATE_MANIFEST to ScriptResources.VIBRATE_MAIN,
        ScriptResources.LOCATION_MANIFEST to ScriptResources.LOCATION_MAIN,
        ScriptResources.MAP_MANIFEST to ScriptResources.MAP_MAIN,
        ScriptResources.NEWS_MANIFEST to ScriptResources.NEWS_MAIN,
        ScriptResources.MEDIA_MANIFEST to ScriptResources.MEDIA_MAIN,
        ScriptResources.WEATHER_MANIFEST to ScriptResources.WEATHER_MAIN,
        ScriptResources.HTTPDEMO_MANIFEST to ScriptResources.HTTPDEMO_MAIN,
    )

    private fun parse(res: String): PackageManifest =
        ManifestParser.parse(ScriptResources.read(res))

    @Test
    fun everyBundledManifestParses() {
        for (demo in demos) {
            val m = parse(demo.manifestRes)
            assertEquals(demo.expectedId, m.id, "id in ${demo.manifestRes}")
            assertTrue(m.version.isNotBlank(), "${m.id} needs a version")
            assertTrue(m.entry.isNotBlank(), "${m.id} needs an entry script")
        }
    }

    /**
     * §5.2: "MUST declare the storage permission to read settings." Settings are
     * seeded into the sub-app's store, so an app that declares settings without
     * storage is asking to read a store it is not allowed to write, and the
     * mismatch is invisible until someone changes a value and nothing happens.
     */
    @Test
    fun settingsImplyStoragePermission() {
        for (demo in demos) {
            val manifestJson = ScriptResources.read(demo.manifestRes)
            val settings = SubAppSetting.parseAll(manifestJson)
            if (settings.isEmpty()) continue
            val m = ManifestParser.parse(manifestJson)
            assertTrue(
                ScriptPermission.Storage in m.permissions,
                "${m.id} declares ${settings.size} setting(s) but not the storage permission",
            )
        }
    }

    /**
     * A declared default must survive [SubAppSetting.sanitise] unchanged. If it
     * does not, the manifest's own default is out of the range the manifest
     * declares — the host silently substitutes a different value and the app
     * starts on something its author never chose.
     */
    @Test
    fun declaredDefaultsSurviveSanitise() {
        var checked = 0
        for (demo in demos) {
            for (s in SubAppSetting.parseAll(ScriptResources.read(demo.manifestRes))) {
                assertFalse(s.key.isBlank(), "${demo.expectedId} has a setting with no key")
                assertFalse(s.label.isBlank(), "${demo.expectedId}/${s.key} needs a label")
                assertEquals(
                    s.defaultValue,
                    s.sanitise(s.defaultValue),
                    "${demo.expectedId}/${s.key}: default ${s.defaultValue} is outside its own bounds",
                )
                if (s.type == SubAppSetting.Type.CHOICE) {
                    assertNotNull(
                        s.options.firstOrNull { it.value == s.defaultValue },
                        "${demo.expectedId}/${s.key}: default is not one of the options",
                    )
                }
                checked++
            }
        }
        // Guards against the whole test passing because nothing was found —
        // a resource path typo would otherwise read as green.
        assertTrue(checked > 0, "no settings found in any bundled demo; check the resource paths")
    }

    /**
     * Every entry script is present on the classpath and non-trivial. Catches a
     * `processResources` wiring mistake, which otherwise surfaces as
     * "Package not installed" on the phone.
     */
    @Test
    fun everyBundledEntryScriptLoads() {
        val mains = listOf(
            ScriptResources.TIMER_MAIN,
            ScriptResources.NAV_MAIN,
            ScriptResources.CAMERA_MAIN,
            ScriptResources.VIBRATE_MAIN,
            ScriptResources.LOCATION_MAIN,
            ScriptResources.MAP_MAIN,
            ScriptResources.NEWS_MAIN,
            ScriptResources.MEDIA_MAIN,
            ScriptResources.WEATHER_MAIN,
            ScriptResources.HTTPDEMO_MAIN,
        )
        for (res in mains) {
            val js = ScriptResources.read(res)
            assertTrue(js.length > 200, "$res looks empty")
            assertTrue(js.contains("onInput"), "$res has no onInput — BACK cannot be handled")
            // §2.3: BACK must be handled or the user can only leave via the
            // side button. 0x06 is the BACK op the compositor sends.
            assertTrue(js.contains("0x06"), "$res never mentions the BACK op (0x06)")
        }
    }

    /**
     * A changed setting must reach a **running** sub-app.
     *
     * §5.2 tells script authors that settings are read at focus. The host did
     * not keep that: the store was seeded when the endpoint was constructed and
     * `ensureRegistered` returned early ever after, so a setting changed while
     * the link service was up did nothing at all. Every sub-app that declares
     * settings was affected, and it presented as "changing the map radius does
     * not change the map" (7 Aug).
     */
    @Test
    fun changedSettingsReachAnAlreadyRunningApp() = runBlocking {
        val manifestJson = ScriptResources.read(ScriptResources.MAP_MANIFEST)
        val manifest = ManifestParser.parse(manifestJson).toScriptManifest()
        val appJs = ScriptResources.read(ScriptResources.MAP_MAIN)
        RhinoScriptEngine().use { eng ->
            val ep = JsSlateAppEndpoint(
                scriptManifest = manifest,
                engine = eng,
                initialStore = mapOf("radiusM" to "400"),
            )
            ep.installRuntime(appJs = appJs)

            fun subscribedRadius(out: List<HostOutbound>): Int =
                out.filterIsInstance<HostOutbound.AdapterCommand>()
                    .first { it.adapter == "map" && it.command == "subscribe" }
                    .let { org.json.JSONObject(it.payloadJson).getInt("radiusM") }

            assertEquals(400, subscribedRadius(ep.dispatch(HostInbound.Focus)))

            // The user edits the setting while the app is still registered.
            ep.seedSettings(mapOf("radiusM" to "140"))

            assertEquals(
                140,
                subscribedRadius(ep.dispatch(HostInbound.Focus)),
                "the app re-focused with the old radius — a changed setting did not reach it",
            )
        }
    }

    /**
     * Focus each bundled demo for real, then send it BACK.
     *
     * This is the closest a host test gets to "opened on hardware at least once
     * and the watch survived" (§6). It will not catch a rendering fault, but it
     * does catch a syntax error, a binding the app calls that does not exist,
     * and a BACK that never relinquishes — the last of which strands the user
     * on a screen they can only leave with the side button.
     *
     * Settings are deliberately NOT seeded here: `initialStore` stays empty, so
     * every app runs its §5.2 "missing value" path. That is the case a device
     * hits the first time a sub-app is installed and nobody has opened its
     * settings screen.
     */
    @Test
    fun everyBundledAppFocusesAndHandlesBack() = runBlocking {
        for (demo in demos) {
            val manifest = parse(demo.manifestRes).toScriptManifest()
            val appJs = ScriptResources.read(entryScripts.getValue(demo.manifestRes))
            RhinoScriptEngine().use { eng ->
                val ep = JsSlateAppEndpoint(scriptManifest = manifest, engine = eng)
                ep.installRuntime(appJs = appJs)

                val focus = ep.dispatch(HostInbound.Focus)
                assertTrue(
                    focus.any { it is HostOutbound.PushDisplayList },
                    "${manifest.id} drew nothing on focus",
                )
                val bytes = focus.filterIsInstance<HostOutbound.PushDisplayList>().first().bytes
                assertTrue(
                    bytes.size <= 2048,
                    "${manifest.id} focus list is ${bytes.size} B, over the 2048 B " +
                        "practical limit in docs/subapp-rules.md §2",
                )

                val back = ep.dispatch(HostInbound.Input(op = 0x06, elemId = 0, x = 0, y = 0))
                assertTrue(
                    back.any { it is HostOutbound.RelinquishFocus },
                    "${manifest.id} did not relinquish focus on BACK (§2.3)",
                )
            }
        }
    }
}
