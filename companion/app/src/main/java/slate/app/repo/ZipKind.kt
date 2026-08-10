package slate.app.repo

import org.json.JSONObject

/**
 * What an opened `.zip` is for — JS sub-app sideload vs InfiniTime/Slate DFU.
 *
 * Both formats ship a top-level `manifest.json`; the shapes differ enough that
 * a cheap peek (no image CRC) is enough to route "Open with" correctly.
 */
enum class ZipKind {
    SubApp,
    Dfu,
    Unknown,
}

object ZipIntake {
    private const val DFU_IMAGE = "slate-mcuboot-image.bin"

    /**
     * Classify from already-read zip entries (basename or relative path keys).
     *
     * DFU wins when either the Nordic nested manifest is present or the known
     * Slate image filename appears — those signals never appear in a sub-app.
     */
    fun classify(files: Map<String, ByteArray>): ZipKind {
        val basenames = files.keys.map { it.substringAfterLast('/') }.toSet()
        if (DFU_IMAGE in basenames) {
            return ZipKind.Dfu
        }

        val manifestBytes = files["manifest.json"]
            ?: files.entries.firstOrNull { it.key.substringAfterLast('/') == "manifest.json" }?.value
            ?: return ZipKind.Unknown

        return classifyManifest(String(manifestBytes, Charsets.UTF_8))
    }

    fun classifyManifest(json: String): ZipKind {
        val root = runCatching { JSONObject(json) }.getOrNull() ?: return ZipKind.Unknown
        val nested = root.optJSONObject("manifest")
        if (nested != null && nested.has("application")) {
            return ZipKind.Dfu
        }
        if (root.has("id") && root.has("minProtocolVersion")) {
            return ZipKind.SubApp
        }
        return ZipKind.Unknown
    }
}
