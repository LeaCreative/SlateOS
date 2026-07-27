package slate.repo

import org.json.JSONArray
import org.json.JSONObject
import slate.script.ScriptPermission

/**
 * Strict §6.2 manifest parser.
 *
 * - Required fields must be present and well-typed
 * - Unknown permission IDs → reject (not silently ignored)
 * - Unknown entries in `requires` → reject ("unknown-but-required")
 * - Unknown top-level keys are allowed for forward compatibility
 * - `manifestFormat` / schema > [SUPPORTED_FORMAT] → reject
 */
object ManifestParser {
    const val SUPPORTED_FORMAT = 1

    private val REQUIRED = setOf(
        "id", "name", "version", "minProtocolVersion", "minHostVersion",
    )

    fun parse(json: String): PackageManifest {
        val o = try {
            JSONObject(json)
        } catch (t: Throwable) {
            throw ManifestException("manifest.json is not valid JSON: ${t.message}")
        }
        val format = o.optInt("manifestFormat", o.optInt("schema", 1))
        if (format > SUPPORTED_FORMAT) {
            throw ManifestException(
                "manifestFormat $format not supported (max $SUPPORTED_FORMAT) — update the companion",
            )
        }
        for (key in REQUIRED) {
            if (!o.has(key) || o.isNull(key)) {
                throw ManifestException("missing required field: $key")
            }
        }
        val id = o.getString("id").trim()
        if (id.isEmpty() || id.any { it.isWhitespace() }) {
            throw ManifestException("id must be a non-empty token without whitespace")
        }
        val name = o.getString("name").trim()
        if (name.isEmpty()) throw ManifestException("name must be non-empty")
        val version = o.getString("version").trim()
        if (version.isEmpty()) throw ManifestException("version must be non-empty")

        val minProtocol = o.getInt("minProtocolVersion")
        if (minProtocol < 1) throw ManifestException("minProtocolVersion must be >= 1")
        val minHost = o.getString("minHostVersion").trim()
        if (minHost.isEmpty()) throw ManifestException("minHostVersion must be non-empty")

        val permissions = parsePermissions(o.opt("permissions"))
        val hosts = linkedSetOf<String>()
        val http = o.optJSONObject("http")
        val hostArr = http?.optJSONArray("allowedHosts") ?: o.optJSONArray("allowedHosts")
        if (hostArr != null) {
            for (i in 0 until hostArr.length()) {
                val h = hostArr.getString(i).trim()
                if (h.isNotEmpty()) hosts += h
            }
        }
        if (ScriptPermission.Http in permissions && hosts.isEmpty()) {
            throw ManifestException("permission http requires http.allowedHosts")
        }

        val requires = parseRequires(o.opt("requires"))
        val unknownReq = requires - HostCapabilities.ALL
        if (unknownReq.isNotEmpty()) {
            throw ManifestException(
                "unknown-but-required capabilities: ${unknownReq.sorted().joinToString()}",
            )
        }

        val refresh = o.optJSONObject("refresh")
        val refreshPolicy = when {
            refresh != null -> refresh.optString("policy", "on-change")
            else -> o.optString("refreshPolicy", "on-change")
        }
        val refreshMs = when {
            refresh != null -> refresh.optLong("intervalMs", 0L)
            else -> o.optLong("refreshIntervalMs", 0L)
        }
        if (refreshPolicy.equals("periodic", true) && refreshMs < 1000L) {
            throw ManifestException("periodic refresh requires intervalMs >= 1000")
        }

        val assets = ArrayList<RequiredAsset>()
        val assetArr = o.optJSONArray("requiredAssets")
        if (assetArr != null) {
            for (i in 0 until assetArr.length()) {
                val a = assetArr.getJSONObject(i)
                val atlas = a.getString("atlas")
                val sha = a.getString("sha256").lowercase()
                if (!SHA256_HEX.matches(sha)) {
                    throw ManifestException("requiredAssets[$i].sha256 must be 64 hex chars")
                }
                assets += RequiredAsset(atlas, sha)
            }
        }

        return PackageManifest(
            id = id,
            name = name,
            version = version,
            author = o.optString("author", ""),
            license = o.optString("license", ""),
            description = o.optString("description", ""),
            minProtocolVersion = minProtocol,
            minHostVersion = minHost,
            entry = o.optString("entry", "main.js").ifBlank { "main.js" },
            priority = o.optString("priority", "normal"),
            refreshPolicy = refreshPolicy,
            refreshIntervalMs = refreshMs,
            permissions = permissions,
            allowedHosts = hosts,
            requiredAssets = assets,
            requires = requires,
            icon = o.optString("icon", "icon.png"),
            storageQuotaBytes = o.optInt("storageQuotaBytes", 256 * 1024).coerceIn(0, 256 * 1024),
        )
    }

    private fun parsePermissions(raw: Any?): Set<ScriptPermission> {
        if (raw == null || raw == JSONObject.NULL) return emptySet()
        if (raw !is JSONArray) throw ManifestException("permissions must be an array")
        val out = linkedSetOf<ScriptPermission>()
        for (i in 0 until raw.length()) {
            val s = raw.getString(i)
            val p = ScriptPermission.parse(s)
                ?: throw ManifestException("unknown permission: $s")
            out += p
        }
        return out
    }

    private fun parseRequires(raw: Any?): Set<String> {
        if (raw == null || raw == JSONObject.NULL) return emptySet()
        if (raw !is JSONArray) throw ManifestException("requires must be an array")
        val out = linkedSetOf<String>()
        for (i in 0 until raw.length()) {
            out += raw.getString(i).trim()
        }
        return out
    }

    private val SHA256_HEX = Regex("^[0-9a-f]{64}$")
}

class ManifestException(message: String) : Exception(message)
