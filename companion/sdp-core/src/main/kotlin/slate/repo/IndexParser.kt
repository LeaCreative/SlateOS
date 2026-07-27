package slate.repo

import org.json.JSONObject
import slate.script.ScriptPermission

/** §6.6 repository index entry (one app version). */
data class IndexApp(
    val id: String,
    val version: String,
    val name: String,
    val description: String = "",
    val author: String = "",
    val minProtocolVersion: Int = 1,
    val minHostVersion: String = "0.1",
    val permissions: Set<ScriptPermission> = emptySet(),
    val size: Long = 0L,
    val sha256: String,
    val url: String,
    val screenshots: List<String> = emptyList(),
)

data class RepoIndex(
    val schema: Int,
    val updated: String,
    val apps: List<IndexApp>,
)

object IndexParser {
    const val SUPPORTED_SCHEMA = 1

    fun parse(json: String): RepoIndex {
        val o = try {
            JSONObject(json)
        } catch (t: Throwable) {
            throw IndexException("index is not valid JSON: ${t.message}")
        }
        val schema = o.optInt("schema", 1)
        if (schema > SUPPORTED_SCHEMA) {
            throw IndexException("index schema $schema not supported (max $SUPPORTED_SCHEMA)")
        }
        val updated = o.optString("updated", "")
        val arr = o.optJSONArray("apps") ?: throw IndexException("index missing apps[]")
        val apps = ArrayList<IndexApp>(arr.length())
        for (i in 0 until arr.length()) {
            val a = arr.getJSONObject(i)
            for (key in listOf("id", "version", "name", "sha256", "url")) {
                if (!a.has(key) || a.optString(key).isBlank()) {
                    throw IndexException("apps[$i] missing required field: $key")
                }
            }
            val sha = a.getString("sha256").lowercase()
            if (!Regex("^[0-9a-f]{64}$").matches(sha)) {
                throw IndexException("apps[$i].sha256 must be 64 hex chars")
            }
            val url = a.getString("url")
            if (!url.startsWith("https://")) {
                throw IndexException("apps[$i].url must be https://")
            }
            val perms = linkedSetOf<ScriptPermission>()
            val pArr = a.optJSONArray("permissions")
            if (pArr != null) {
                for (j in 0 until pArr.length()) {
                    val s = pArr.getString(j)
                    val p = ScriptPermission.parse(s)
                        ?: throw IndexException("apps[$i] unknown permission: $s")
                    perms += p
                }
            }
            val shots = ArrayList<String>()
            val sArr = a.optJSONArray("screenshots")
            if (sArr != null) {
                for (j in 0 until sArr.length()) {
                    val u = sArr.getString(j)
                    if (u.isNotBlank()) shots += u
                }
            }
            apps += IndexApp(
                id = a.getString("id"),
                version = a.getString("version"),
                name = a.getString("name"),
                description = a.optString("description", ""),
                author = a.optString("author", ""),
                minProtocolVersion = a.optInt("minProtocolVersion", 1),
                minHostVersion = a.optString("minHostVersion", "0.1"),
                permissions = perms,
                size = a.optLong("size", 0L),
                sha256 = sha,
                url = url,
                screenshots = shots,
            )
        }
        return RepoIndex(schema = schema, updated = updated, apps = apps)
    }
}

class IndexException(message: String) : Exception(message)
