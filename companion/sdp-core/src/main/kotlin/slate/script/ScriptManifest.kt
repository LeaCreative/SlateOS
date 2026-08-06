package slate.script

/**
 * Permission names from roadmap §6.3. Bindings are denied unless granted
 * in the sub-app manifest AND held by the host app.
 */
enum class ScriptPermission(val id: String) {
    Storage("storage"),
    Http("http"),
    Notifications("notifications"),
    Media("media"),
    Location("location"),
    HealthRead("health.read"),
    /** Turn-by-turn nav adapter (OsmAnd / generic). */
    Navigation("navigation"),
    /**
     * Camera preview/capture. Host-side CameraX only — frames do not enter
     * the script isolate (see docs/flagship-apps.md).
     */
    Camera("camera"),
    /** Vibrate the phone. Distinct from slate.haptic(), which is the watch. */
    Vibrate("vibrate"),
    ;

    companion object {
        fun parse(raw: String): ScriptPermission? =
            entries.firstOrNull { it.id.equals(raw, ignoreCase = true) }
    }
}

data class ScriptManifest(
    val id: String,
    val name: String,
    val version: String = "1.0.0",
    val entry: String = "main.js",
    val minProtocolVersion: Int = 1,
    val minHostVersion: String = "0.1",
    val priority: String = "normal",
    val refreshPolicy: String = "on-change",
    val refreshIntervalMs: Long = 0L,
    val permissions: Set<ScriptPermission> = emptySet(),
    val allowedHosts: Set<String> = emptySet(),
    /** Max storage bytes — governor also enforces 256 KB hard cap. */
    val storageQuotaBytes: Int = 256 * 1024,
) {
    fun allows(p: ScriptPermission): Boolean = p in permissions
}
