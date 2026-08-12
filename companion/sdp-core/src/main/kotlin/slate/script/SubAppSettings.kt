package slate.script

import org.json.JSONObject

/**
 * User-editable settings a sub-app declares in its manifest.
 *
 * The sub-app supplies the schema; the companion generates the UI. No sub-app
 * ships settings screens of its own — a downloaded script drawing its own
 * phone-side UI is exactly the line this project does not cross, and it would
 * also mean every app inventing its own layout.
 *
 * Values reach the script through its normal storage: the host seeds the
 * declared keys into the sub-app's store before it runs, so a script reads a
 * setting with `slate.store.get('key')` and needs no new binding. That also
 * means a script can overwrite one, which is why `docs/subapp-rules.md` §5.2
 * requires scripts to validate defensively on read.
 */
data class SubAppSetting(
    val key: String,
    val label: String,
    val type: Type,
    val defaultValue: String,
    val min: Int? = null,
    val max: Int? = null,
    val unit: String = "",
    val help: String = "",
    val options: List<Option> = emptyList(),
) {
    enum class Type { INT, BOOL, CHOICE, STRING }

    data class Option(val value: String, val label: String)

    /** Clamp/normalise a user-entered value. Never throws. */
    fun sanitise(raw: String): String = when (type) {
        Type.INT -> {
            val n = raw.trim().toIntOrNull() ?: defaultValue.toIntOrNull() ?: 0
            val lo = min ?: Int.MIN_VALUE
            val hi = max ?: Int.MAX_VALUE
            n.coerceIn(lo, hi).toString()
        }
        Type.BOOL -> if (raw == "1" || raw.equals("true", true)) "1" else "0"
        Type.CHOICE ->
            if (options.any { it.value == raw }) raw else defaultValue
        Type.STRING -> {
            val trimmed = raw.trim()
            val hi = max ?: MAX_STRING_LEN
            trimmed.take(hi.coerceIn(1, MAX_STRING_LEN))
        }
    }

    companion object {
        const val MAX_STRING_LEN = 512

        /**
         * Parse the optional `settings` array from a manifest.
         *
         * Deliberately total: a malformed entry is skipped rather than throwing,
         * because this runs on manifests from third-party repositories and a bad
         * one must not stop the whole catalogue rendering.
         */
        fun parseAll(manifestJson: String): List<SubAppSetting> {
            val out = mutableListOf<SubAppSetting>()
            val arr = try {
                JSONObject(manifestJson).optJSONArray("settings") ?: return emptyList()
            } catch (_: Throwable) {
                return emptyList()
            }
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                val key = o.optString("key")
                if (key.isBlank()) continue
                val type = when (o.optString("type", "int").lowercase()) {
                    "bool", "boolean" -> Type.BOOL
                    "choice", "enum" -> Type.CHOICE
                    "string", "text", "url" -> Type.STRING
                    else -> Type.INT
                }
                val options = mutableListOf<Option>()
                o.optJSONArray("options")?.let { opts ->
                    for (j in 0 until opts.length()) {
                        val oo = opts.optJSONObject(j) ?: continue
                        val v = oo.opt("value")?.toString() ?: continue
                        options += Option(v, oo.optString("label", v))
                    }
                }
                if (type == Type.CHOICE && options.isEmpty()) continue
                val default = o.opt("default")?.toString()
                    ?: when (type) {
                        Type.BOOL -> "0"
                        Type.STRING -> ""
                        else -> ""
                    }
                out += SubAppSetting(
                    key = key,
                    label = o.optString("label", key),
                    type = type,
                    defaultValue = default,
                    min = if (o.has("min")) o.optInt("min") else null,
                    max = if (o.has("max")) o.optInt("max") else null,
                    unit = o.optString("unit", ""),
                    help = o.optString("help", ""),
                    options = options,
                )
            }
            return out
        }
    }
}
