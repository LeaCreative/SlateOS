package slate.app.repo

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject
import slate.repo.OfficialRepoTrust
import slate.repo.RepoSource
import slate.repo.RepoTrust
import slate.script.ScriptPermission

/** User-configurable repositories + update / trust prefs. */
class RepoPrefs(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("slate_repo", Context.MODE_PRIVATE)

    var allowMeteredUpdates: Boolean
        get() = prefs.getBoolean("allow_metered", false)
        set(v) = prefs.edit().putBoolean("allow_metered", v).apply()

    var autoUpdateEnabled: Boolean
        get() = prefs.getBoolean("auto_update", true)
        set(v) = prefs.edit().putBoolean("auto_update", v).apply()

    /** Official Ed25519 SPKI — override for staging; production ships baked-in key. */
    var officialPublicKeySpki: String
        get() = prefs.getString("official_pubkey", OfficialRepoTrust.PUBLIC_KEY_SPKI_BASE64_PLACEHOLDER)
            ?: OfficialRepoTrust.PUBLIC_KEY_SPKI_BASE64_PLACEHOLDER
        set(v) = prefs.edit().putString("official_pubkey", v).apply()

    var officialIndexUrl: String
        get() = prefs.getString("official_url", OfficialRepoTrust.DEFAULT_INDEX_URL)
            ?: OfficialRepoTrust.DEFAULT_INDEX_URL
        set(v) = prefs.edit().putString("official_url", v).apply()

    fun sources(): List<RepoSource> {
        val list = mutableListOf(
            RepoSource(
                id = "official",
                name = "Official",
                indexUrl = officialIndexUrl,
                trust = RepoTrust.Official,
                publicKeySpkiBase64 = officialPublicKeySpki,
            ),
        )
        val raw = prefs.getString("third_party", "[]") ?: "[]"
        val arr = JSONArray(raw)
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            list += RepoSource(
                id = o.getString("id"),
                name = o.getString("name"),
                indexUrl = o.getString("indexUrl"),
                trust = RepoTrust.ThirdParty,
                publicKeySpkiBase64 = if (o.has("publicKey") && !o.isNull("publicKey")) {
                    o.getString("publicKey")
                } else {
                    null
                },
            )
        }
        return list
    }

    fun addThirdParty(name: String, indexUrl: String, publicKeySpki: String?): Boolean {
        if (!indexUrl.startsWith("https://")) return false
        val id = "tp-" + indexUrl.hashCode().toUInt().toString(16)
        val arr = JSONArray(prefs.getString("third_party", "[]"))
        for (i in 0 until arr.length()) {
            if (arr.getJSONObject(i).getString("indexUrl") == indexUrl) return false
        }
        arr.put(
            JSONObject()
                .put("id", id)
                .put("name", name)
                .put("indexUrl", indexUrl)
                .put("publicKey", publicKeySpki),
        )
        prefs.edit().putString("third_party", arr.toString()).apply()
        return true
    }

    fun removeThirdParty(id: String) {
        val arr = JSONArray(prefs.getString("third_party", "[]"))
        val next = JSONArray()
        for (i in 0 until arr.length()) {
            val o = arr.getJSONObject(i)
            if (o.getString("id") != id) next.put(o)
        }
        prefs.edit().putString("third_party", next.toString()).apply()
    }

    fun userGrantedSensitive(appId: String): Set<ScriptPermission> {
        val raw = prefs.getString("grant:$appId", "") ?: ""
        if (raw.isBlank()) return emptySet()
        return raw.split(',').mapNotNull { ScriptPermission.parse(it.trim()) }.toSet()
    }

    fun setUserGrantedSensitive(appId: String, perms: Set<ScriptPermission>) {
        prefs.edit()
            .putString("grant:$appId", perms.joinToString(",") { it.id })
            .apply()
    }
}
