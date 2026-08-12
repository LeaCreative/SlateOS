package slate.script

import org.json.JSONObject
import java.io.InputStreamReader
import java.nio.charset.StandardCharsets

/** Classpath loaders for shared JS + packaged sub-apps. */
object ScriptResources {
    const val UI_JS = "/slate/script/slate_ui.js"
    const val HOST_JS = "/slate/script/slate_host.js"
    const val TIMER_MAIN = "/slate/subapps/timer/main.js"
    const val TIMER_MANIFEST = "/slate/subapps/timer/manifest.json"
    const val NAV_MAIN = "/slate/subapps/navigation/main.js"
    const val NAV_MANIFEST = "/slate/subapps/navigation/manifest.json"
    const val CAMERA_MAIN = "/slate/subapps/camera/main.js"
    const val CAMERA_MANIFEST = "/slate/subapps/camera/manifest.json"
    const val VIBRATE_MAIN = "/slate/subapps/vibrate/main.js"
    const val VIBRATE_MANIFEST = "/slate/subapps/vibrate/manifest.json"
    const val LOCATION_MAIN = "/slate/subapps/location/main.js"
    const val LOCATION_MANIFEST = "/slate/subapps/location/manifest.json"
    const val MAP_MAIN = "/slate/subapps/map/main.js"
    const val MAP_MANIFEST = "/slate/subapps/map/manifest.json"
    const val NEWS_MAIN = "/slate/subapps/news/main.js"
    const val NEWS_MANIFEST = "/slate/subapps/news/manifest.json"
    const val MEDIA_MAIN = "/slate/subapps/media/main.js"
    const val MEDIA_MANIFEST = "/slate/subapps/media/manifest.json"
    const val WEATHER_MAIN = "/slate/subapps/weather/main.js"
    const val WEATHER_MANIFEST = "/slate/subapps/weather/manifest.json"
    const val HTTPDEMO_MAIN = "/slate/subapps/httpdemo/main.js"
    const val HTTPDEMO_MANIFEST = "/slate/subapps/httpdemo/manifest.json"

    fun read(path: String): String {
        val stream = ScriptResources::class.java.getResourceAsStream(path)
            ?: error("Missing resource $path — ensure shared-js is copied into sdp-core resources")
        return stream.use { InputStreamReader(it, StandardCharsets.UTF_8).readText() }
    }

    fun parseManifest(json: String): ScriptManifest {
        val o = JSONObject(json)
        val perms = mutableSetOf<ScriptPermission>()
        val arr = o.optJSONArray("permissions")
        if (arr != null) {
            for (i in 0 until arr.length()) {
                ScriptPermission.parse(arr.getString(i))?.let { perms += it }
            }
        }
        val hosts = mutableSetOf<String>()
        val http = o.optJSONObject("http")
        val hostArr = http?.optJSONArray("allowedHosts") ?: o.optJSONArray("allowedHosts")
        if (hostArr != null) {
            for (i in 0 until hostArr.length()) hosts += hostArr.getString(i)
        }
        return ScriptManifest(
            id = o.getString("id"),
            name = o.getString("name"),
            version = o.optString("version", "1.0.0"),
            entry = o.optString("entry", "main.js"),
            minProtocolVersion = o.optInt("minProtocolVersion", 1),
            minHostVersion = o.optString("minHostVersion", "0.1"),
            priority = o.optString("priority", "normal"),
            refreshPolicy = o.optString("refreshPolicy", o.optJSONObject("refresh")?.optString("policy") ?: "on-change"),
            refreshIntervalMs = o.optLong(
                "refreshIntervalMs",
                o.optJSONObject("refresh")?.optLong("intervalMs") ?: 0L,
            ),
            permissions = perms,
            allowedHosts = hosts,
            storageQuotaBytes = o.optInt("storageQuotaBytes", 256 * 1024),
        )
    }
}
