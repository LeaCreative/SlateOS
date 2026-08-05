package slate.app.repo

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import java.security.MessageDigest
import java.util.zip.ZipInputStream
import slate.app.link.LinkLog
import slate.repo.ManifestParser
import slate.repo.RepoTrust

/**
 * Install a JS sub-app from a `.zip` opened on the phone.
 *
 * The point is to iterate on sub-apps without reinstalling the APK: package a
 * directory containing `manifest.json` + `main.js`, send it to the phone by any
 * means (email, Drive, USB), and open it. Android routes the file here and the
 * app lands in [InstalledStore] alongside repository-installed ones.
 *
 * Trust is deliberately the third-party tier, not the built-in tier: a file
 * arriving from outside the app has not been through the repository, so it gets
 * the restricted permission set regardless of what its manifest asks for.
 */
class SideloadActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val view = TextView(this).apply {
            setPadding(32, 96, 32, 32)
            textSize = 15f
        }
        setContentView(view)

        val uri = intent?.data ?: intent?.getParcelableExtra(Intent.EXTRA_STREAM, android.net.Uri::class.java)
        if (uri == null) {
            view.text = "No file supplied."
            return
        }

        view.text = try {
            val files = readZip(uri)
            val manifestJson = files["manifest.json"]
                ?: error("no manifest.json at the top level of the zip")
            val manifest = ManifestParser.parse(String(manifestJson, Charsets.UTF_8))
            require(files.containsKey(manifest.entry)) {
                "manifest entry '${manifest.entry}' is not in the zip"
            }

            val sha = MessageDigest.getInstance("SHA-256").let { md ->
                files.toSortedMap().forEach { (name, bytes) ->
                    md.update(name.toByteArray(Charsets.UTF_8)); md.update(bytes)
                }
                md.digest().joinToString("") { "%02x".format(it) }
            }

            InstalledStore.create(this).install(
                packageFiles = files,
                manifest = manifest,
                repoId = REPO_ID,
                trust = RepoTrust.ThirdParty,
                sha256 = sha,
                effectivePermissions = emptySet(),
            )
            LinkLog.i("sideload: installed ${manifest.id} v${manifest.version} (${files.size} files)")
            "Installed ${manifest.name} ${manifest.version}\n\n" +
                "id: ${manifest.id}\nentry: ${manifest.entry}\n" +
                "files: ${files.keys.sorted().joinToString(", ")}\n\n" +
                "Open it from “Sub-app repository” on the main screen."
        } catch (t: Throwable) {
            LinkLog.w("sideload failed: ${t.message}")
            "Could not install this package.\n\n${t.message}\n\n" +
                "Expected a .zip containing manifest.json and the entry script " +
                "at the top level."
        }
    }

    /** Flat read; entries in subdirectories keep their relative path. */
    private fun readZip(uri: android.net.Uri): Map<String, ByteArray> {
        val out = LinkedHashMap<String, ByteArray>()
        contentResolver.openInputStream(uri).use { raw ->
            requireNotNull(raw) { "cannot open $uri" }
            ZipInputStream(raw).use { zin ->
                while (true) {
                    val e = zin.nextEntry ?: break
                    if (e.isDirectory) continue
                    // Reject traversal before anything touches the filesystem.
                    val name = e.name.removePrefix("./")
                    require(!name.startsWith("/") && !name.contains("..")) {
                        "unsafe path in zip: ${e.name}"
                    }
                    out[name] = zin.readBytes()
                }
            }
        }
        require(out.isNotEmpty()) { "zip is empty" }
        return out
    }

    private companion object {
        const val REPO_ID = "sideload"
    }
}
