package slate.app.repo

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import slate.repo.PackageManifest
import slate.repo.ManifestParser
import slate.repo.RepoTrust
import slate.script.ScriptPermission
import java.io.File

/**
 * Local cache of installed `.slate` packages. Running does not need network.
 */
class InstalledStore(private val root: File) {
    private val appsDir = File(root, "subapps").also { it.mkdirs() }
    private val metaFile = File(root, "installed.json")

    data class InstalledApp(
        val id: String,
        val version: String,
        val name: String,
        val repoId: String,
        val trust: RepoTrust,
        val permissions: Set<ScriptPermission>,
        val sha256: String,
        val dir: File,
    ) {
        fun manifest(): PackageManifest =
            ManifestParser.parse(File(dir, "manifest.json").readText(Charsets.UTF_8))

        fun entryJs(): String {
            val m = manifest()
            val f = File(dir, m.entry)
            return if (f.isFile) f.readText(Charsets.UTF_8) else File(dir, "main.js").readText(Charsets.UTF_8)
        }
    }

    fun list(): List<InstalledApp> = loadMeta().mapNotNull { row ->
        val dir = File(appsDir, row.id)
        if (!dir.isDirectory) return@mapNotNull null
        InstalledApp(
            id = row.id,
            version = row.version,
            name = row.name,
            repoId = row.repoId,
            trust = row.trust,
            permissions = row.permissions,
            sha256 = row.sha256,
            dir = dir,
        )
    }

    fun get(id: String): InstalledApp? = list().firstOrNull { it.id == id }

    fun install(
        packageFiles: Map<String, ByteArray>,
        manifest: PackageManifest,
        repoId: String,
        trust: RepoTrust,
        sha256: String,
        effectivePermissions: Set<ScriptPermission>,
    ) {
        val dir = File(appsDir, manifest.id)
        if (dir.exists()) dir.deleteRecursively()
        dir.mkdirs()
        for ((path, bytes) in packageFiles) {
            val out = File(dir, path)
            out.parentFile?.mkdirs()
            out.writeBytes(bytes)
        }
        val rows = loadMeta().filterNot { it.id == manifest.id }.toMutableList()
        rows += MetaRow(
            id = manifest.id,
            version = manifest.version,
            name = manifest.name,
            repoId = repoId,
            trust = trust,
            permissions = effectivePermissions,
            sha256 = sha256,
        )
        saveMeta(rows)
    }

    fun remove(id: String) {
        File(appsDir, id).deleteRecursively()
        saveMeta(loadMeta().filterNot { it.id == id })
    }

    private data class MetaRow(
        val id: String,
        val version: String,
        val name: String,
        val repoId: String,
        val trust: RepoTrust,
        val permissions: Set<ScriptPermission>,
        val sha256: String,
    )

    private fun loadMeta(): List<MetaRow> {
        if (!metaFile.isFile) return emptyList()
        return try {
            val arr = JSONArray(metaFile.readText())
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                val perms = linkedSetOf<ScriptPermission>()
                val p = o.optJSONArray("permissions")
                if (p != null) {
                    for (j in 0 until p.length()) {
                        ScriptPermission.parse(p.getString(j))?.let { perms += it }
                    }
                }
                MetaRow(
                    id = o.getString("id"),
                    version = o.getString("version"),
                    name = o.getString("name"),
                    repoId = o.optString("repoId", "unknown"),
                    trust = if (o.optString("trust") == "Official") RepoTrust.Official else RepoTrust.ThirdParty,
                    permissions = perms,
                    sha256 = o.optString("sha256", ""),
                )
            }
        } catch (_: Throwable) {
            emptyList()
        }
    }

    private fun saveMeta(rows: List<MetaRow>) {
        val arr = JSONArray()
        for (r in rows) {
            arr.put(
                JSONObject()
                    .put("id", r.id)
                    .put("version", r.version)
                    .put("name", r.name)
                    .put("repoId", r.repoId)
                    .put("trust", r.trust.name)
                    .put("sha256", r.sha256)
                    .put(
                        "permissions",
                        JSONArray().also { a -> r.permissions.forEach { a.put(it.id) } },
                    ),
            )
        }
        metaFile.writeText(arr.toString(2))
    }

    companion object {
        fun create(context: Context): InstalledStore =
            InstalledStore(File(context.filesDir, "repo"))
    }
}
