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
            setOf(ScriptPermission.Navigation),
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
            setOf(ScriptPermission.Vibrate),
        ),
    )

    fun ensureOfficialDemos(context: Context): Int {
        val store = InstalledStore.create(context)
        var installed = 0
        for (demo in DEMOS) {
            if (store.get(demo.id) != null) continue
            val manifestText = ScriptResources.read(demo.manifestRes)
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
