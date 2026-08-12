package slate.repo

import slate.script.ScriptManifest
import slate.script.ScriptPermission

/**
 * §6.2 package manifest — validated at install time.
 * Converted to [ScriptManifest] for the M12 runtime.
 */
data class PackageManifest(
    val id: String,
    val name: String,
    val version: String,
    val author: String = "",
    val license: String = "",
    val description: String = "",
    val minProtocolVersion: Int,
    val minHostVersion: String,
    val entry: String = "main.js",
    val priority: String = "normal",
    val refreshPolicy: String = "on-change",
    val refreshIntervalMs: Long = 0L,
    val permissions: Set<ScriptPermission>,
    val allowedHosts: Set<String> = emptySet(),
    val requiredAssets: List<RequiredAsset> = emptyList(),
    /** Host capabilities the package needs; unknown entries → reject. */
    val requires: Set<String> = emptySet(),
    val icon: String = "icon.png",
    val storageQuotaBytes: Int = 256 * 1024,
) {
    fun toScriptManifest(): ScriptManifest = ScriptManifest(
        id = id,
        name = name,
        version = version,
        entry = entry,
        minProtocolVersion = minProtocolVersion,
        minHostVersion = minHostVersion,
        priority = priority,
        refreshPolicy = refreshPolicy,
        refreshIntervalMs = refreshIntervalMs,
        permissions = permissions,
        allowedHosts = allowedHosts,
        storageQuotaBytes = storageQuotaBytes,
    )
}

data class RequiredAsset(
    val atlas: String,
    val sha256: String,
)

/** Known host capability tokens for the optional `requires` array. */
object HostCapabilities {
    val ALL: Set<String> = setOf(
        "slate.ui",
        "slate.invalidate",
        "slate.store",
        "slate.http",
        "slate.notifications",
        "slate.media",
        "slate.location",
        "slate.health",
        "slate.timer",
        "slate.haptic",
        "slate.log",
        "slate.nav",
        "slate.camera",
        "slate.map",
        "slate.news",
        "slate.weather",
        /**
         * Buzzing the handset has been bindable since 6 Aug but had no token
         * here, so `examples/vibrate` could not declare the one capability it
         * cannot run without. An unknown entry in `requires` is a hard reject,
         * so the omission forced apps to under-declare.
         */
        "slate.phone",
    )
}
