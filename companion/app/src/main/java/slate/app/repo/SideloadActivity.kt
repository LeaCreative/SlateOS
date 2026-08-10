package slate.app.repo

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.TextView
import java.security.MessageDigest
import java.util.zip.ZipInputStream
import slate.app.link.LinkLog
import slate.app.ota.SlateOtaActivity
import slate.repo.ManifestParser
import slate.repo.RepoTrust

/**
 * Install a JS sub-app — or hand a DFU zip to SDP OTA — from a `.zip` opened
 * on the phone.
 *
 * Package a directory containing `manifest.json` + `main.js`, or a
 * `slate-dfu.zip`, send it by any means, and open it. Android routes the file
 * here; [ZipIntake] decides which path applies.
 *
 * Sub-app trust is deliberately the third-party tier: a file arriving from
 * outside the app has not been through the repository.
 */
class SideloadActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val view = TextView(this).apply {
            setPadding(32, 96, 32, 32)
            textSize = 15f
        }
        setContentView(view)

        val uri = intent?.data
            ?: intent?.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        if (uri == null) {
            view.text = "No file supplied."
            return
        }

        view.text = try {
            val files = readZip(uri)
            when (ZipIntake.classify(files)) {
                ZipKind.Dfu -> {
                    openOta(uri)
                    "Opening firmware update…"
                }
                ZipKind.SubApp -> installSubApp(files)
                ZipKind.Unknown ->
                    "Could not recognise this zip.\n\n" +
                        "Expected either a Slate sub-app (manifest.json with id / " +
                        "minProtocolVersion + entry script) or a slate-dfu.zip " +
                        "(Nordic DFU manifest + slate-mcuboot-image.bin)."
            }
        } catch (t: Throwable) {
            LinkLog.w("sideload failed: ${t.message}")
            "Could not open this package.\n\n${t.message}\n\n" +
                "Expected a .zip containing a Slate sub-app or slate-dfu.zip."
        }
    }

    private fun installSubApp(files: Map<String, ByteArray>): String {
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
        return "Installed ${manifest.name} ${manifest.version}\n\n" +
            "id: ${manifest.id}\nentry: ${manifest.entry}\n" +
            "files: ${files.keys.sorted().joinToString(", ")}\n\n" +
            "Open it from “Sub-app repository” on the main screen."
    }

    private fun openOta(uri: Uri) {
        val launch = Intent(this, SlateOtaActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            data = uri
            putExtra(SlateOtaActivity.EXTRA_PACKAGE_URI, uri.toString())
            addFlags(
                Intent.FLAG_ACTIVITY_CLEAR_TOP or
                    Intent.FLAG_ACTIVITY_SINGLE_TOP or
                    Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
            // ClipData carries the URI grant across the activity boundary on
            // modern Android when only Intent.data is set inconsistently.
            clipData = android.content.ClipData.newUri(contentResolver, "dfu", uri)
        }
        LinkLog.i("sideload: DFU zip → SlateOtaActivity ($uri)")
        startActivity(launch)
        finish()
    }

    /** Flat read; entries in subdirectories keep their relative path. */
    private fun readZip(uri: Uri): Map<String, ByteArray> {
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
