package slate.app.repo

import android.content.Context
import slate.repo.Digests
import slate.repo.ManifestParser
import slate.repo.RepoTrust
import slate.script.ScriptPermission
import slate.script.ScriptResources
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * Bootstraps official demo packages into [InstalledStore] via the same install
 * path as the M13 repository client (parse `.slate` zip → store files + meta).
 *
 * Until the hosted official index serves these URLs, this is how Navigation /
 * Camera land on-device as **downloaded JS**, not compiled-in Kotlin apps.
 */
object BundledPackageSeeder {
    data class Bundled(
        val id: String,
        val manifestRes: String,
        val mainRes: String,
        val permissions: Set<ScriptPermission>,
    )

    /**
     * These permission sets MUST match the `permissions` array in each demo's
     * `manifest.json` under companion/examples. They are what gets recorded as
     * the installed grant; the manifest is what the runtime re-derives from at
     * registration. Letting them drift means the repository screen shows one
     * thing and the sub-app is bound with another. Any app that declares
     * `settings` needs Storage here as well, because reading a setting is
     * reading the store (docs/subapp-rules.md §5.2).
     */
    val DEMOS: List<Bundled> = listOf(
        Bundled(
            "slate.timer",
            ScriptResources.TIMER_MANIFEST,
            ScriptResources.TIMER_MAIN,
            setOf(ScriptPermission.Storage),
        ),
        Bundled(
            "slate.navigation",
            ScriptResources.NAV_MANIFEST,
            ScriptResources.NAV_MAIN,
            setOf(ScriptPermission.Navigation, ScriptPermission.Storage),
        ),
        Bundled(
            "slate.camera",
            ScriptResources.CAMERA_MANIFEST,
            ScriptResources.CAMERA_MAIN,
            setOf(ScriptPermission.Camera),
        ),
        Bundled(
            "slate.vibrate",
            ScriptResources.VIBRATE_MANIFEST,
            ScriptResources.VIBRATE_MAIN,
            setOf(ScriptPermission.Vibrate, ScriptPermission.Storage),
        ),
        Bundled(
            "slate.location",
            ScriptResources.LOCATION_MANIFEST,
            ScriptResources.LOCATION_MAIN,
            setOf(ScriptPermission.Location, ScriptPermission.Storage),
        ),
        Bundled(
            "slate.map",
            ScriptResources.MAP_MANIFEST,
            ScriptResources.MAP_MAIN,
            setOf(ScriptPermission.Location, ScriptPermission.Storage),
        ),
    )

    fun ensureOfficialDemos(context: Context): Int {
        val store = InstalledStore.create(context)
        var installed = 0
        for (demo in DEMOS) {
            val manifestText = ScriptResources.read(demo.manifestRes)
            // Reinstall when the bundled version differs from what is installed.
            // This used to skip any id already present, so editing a bundled
            // demo's manifest — adding settings, say — never reached a device
            // that had an older copy, and the change looked like it had done
            // nothing.
            val existing = store.get(demo.id)
            if (existing != null) {
                val bundledVersion = ManifestParser.parse(manifestText).version
                if (existing.version == bundledVersion) continue
            }
            val mainText = ScriptResources.read(demo.mainRes)
            val zip = zipOf(
                "manifest.json" to manifestText.toByteArray(Charsets.UTF_8),
                "main.js" to mainText.toByteArray(Charsets.UTF_8),
            )
            val sha = Digests.sha256Hex(zip)
            val manifest = ManifestParser.parse(manifestText)
            store.install(
                packageFiles = mapOf(
                    "manifest.json" to manifestText.toByteArray(Charsets.UTF_8),
                    "main.js" to mainText.toByteArray(Charsets.UTF_8),
                ),
                manifest = manifest,
                repoId = "official-bundled",
                trust = RepoTrust.Official,
                sha256 = sha,
                effectivePermissions = demo.permissions,
            )
            installed++
        }
        return installed
    }

    private fun zipOf(vararg files: Pair<String, ByteArray>): ByteArray {
        val bos = ByteArrayOutputStream()
        ZipOutputStream(bos).use { zos ->
            for ((name, bytes) in files) {
                zos.putNextEntry(ZipEntry(name))
                zos.write(bytes)
                zos.closeEntry()
            }
        }
        return bos.toByteArray()
    }
}
