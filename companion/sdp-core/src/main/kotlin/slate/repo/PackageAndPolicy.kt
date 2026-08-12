package slate.repo

import slate.script.ScriptPermission
import java.io.ByteArrayInputStream
import java.util.zip.ZipInputStream

/**
 * `.slate` package = zip (§6.2). Parsed in memory; no Android types.
 */
data class SlatePackage(
    val bytes: ByteArray,
    val sha256: String,
    val manifest: PackageManifest,
    val files: Map<String, ByteArray>,
) {
    fun entryJs(): ByteArray =
        files[manifest.entry]
            ?: files["main.js"]
            ?: throw ManifestException("entry ${manifest.entry} missing from package")

    override fun equals(other: Any?): Boolean =
        other is SlatePackage && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = bytes.contentHashCode()
}

object SlatePackageReader {
    fun open(bytes: ByteArray, expectedSha256: String? = null): SlatePackage {
        val sha = Digests.sha256Hex(bytes)
        if (expectedSha256 != null && !Digests.matches(bytes, expectedSha256)) {
            throw PackageException(
                "package SHA-256 mismatch (expected $expectedSha256 got $sha)",
            )
        }
        val files = LinkedHashMap<String, ByteArray>()
        ZipInputStream(ByteArrayInputStream(bytes)).use { zis ->
            while (true) {
                val entry = zis.nextEntry ?: break
                if (entry.isDirectory) continue
                val name = entry.name.removePrefix("./").trimStart('/')
                if (name.contains("..")) {
                    throw PackageException("illegal path in package: $name")
                }
                files[name] = zis.readBytes()
            }
        }
        val manifestBytes = files["manifest.json"]
            ?: throw PackageException("manifest.json missing from .slate package")
        val manifest = ManifestParser.parse(manifestBytes.toString(Charsets.UTF_8))
        if (manifest.entry !in files && "main.js" !in files) {
            throw PackageException("entry script missing: ${manifest.entry}")
        }
        return SlatePackage(bytes = bytes, sha256 = sha, manifest = manifest, files = files)
    }
}

class PackageException(message: String) : Exception(message)

/**
 * §6.1 / §6.6 permission ceilings.
 *
 * Effective bindable set = declared ∩ [sourceCeiling] ∩ [HOST_HELD].
 * Store install already applies [effective]; runtime re-applies the same so a
 * future [PRIVILEGED_INTERNAL] permission cannot silently land on store apps.
 *
 * Third-party / sideloaded packages are **not** permission-restricted relative
 * to Official: if the host can wire a binding, a declared third-party app may
 * use it. Users still see provenance (Official vs repo name) and must consent
 * when an update *adds* permissions ([UpdatePolicy]).
 */
object PermissionPolicy {
    /**
     * What the companion host can technically wire (whitelisted bindings only —
     * no reflection, filesystem, or raw Android APIs; see CLAUDE.md).
     */
    val HOST_HELD: Set<ScriptPermission> = ScriptPermission.entries.toSet()

    /**
     * Host-only capabilities: grantable solely under Official/bundled trust.
     * Keep empty unless a binding must never ship to community packages.
     */
    val PRIVILEGED_INTERNAL: Set<ScriptPermission> = emptySet()

    /**
     * Historical “reduced third-party set”. Intentionally empty — open-source
     * packages declare what they need; the BindingSurface still gates on the
     * manifest. Kept as a named set so older UI/tests compile.
     */
    val THIRD_PARTY_BLOCKED: Set<ScriptPermission> = emptySet()

    /** Max permissions a package from [trust] may ever hold. */
    fun sourceCeiling(trust: RepoTrust): Set<ScriptPermission> = when (trust) {
        RepoTrust.Official -> HOST_HELD
        RepoTrust.ThirdParty -> HOST_HELD - PRIVILEGED_INTERNAL
    }

    fun effective(
        declared: Set<ScriptPermission>,
        trust: RepoTrust,
        @Suppress("UNUSED_PARAMETER")
        userGrantedSensitive: Set<ScriptPermission> = emptySet(),
    ): Set<ScriptPermission> =
        declared.intersect(sourceCeiling(trust)).intersect(HOST_HELD)

    /**
     * Runtime bindable set for [JsSlateAppEndpoint] / [BindingSurface].
     * Recomputes from declared permissions + trust.
     */
    fun bindable(
        declared: Set<ScriptPermission>,
        trust: RepoTrust,
        userGrantedSensitive: Set<ScriptPermission> = emptySet(),
    ): Set<ScriptPermission> = effective(declared, trust, userGrantedSensitive)

    fun blockedByDefault(declared: Set<ScriptPermission>, trust: RepoTrust): Set<ScriptPermission> {
        if (trust == RepoTrust.Official) return emptySet()
        return declared.intersect(THIRD_PARTY_BLOCKED)
    }
}

enum class RepoTrust {
    Official,
    ThirdParty,
}

/** Semver-ish compare for minHostVersion (1.2.0 style; non-numeric tails ignored). */
object HostVersion {
    fun compare(a: String, b: String): Int {
        val pa = parse(a)
        val pb = parse(b)
        val n = maxOf(pa.size, pb.size)
        for (i in 0 until n) {
            val x = pa.getOrElse(i) { 0 }
            val y = pb.getOrElse(i) { 0 }
            if (x != y) return x.compareTo(y)
        }
        return 0
    }

    /** True if [host] satisfies requirement [required] (host >= required). */
    fun satisfies(host: String, required: String): Boolean =
        compare(host, required) >= 0

    private fun parse(v: String): List<Int> =
        v.trim().split('.', '-', '+').mapNotNull { part ->
            part.takeWhile { it.isDigit() }.toIntOrNull()
        }
}

sealed class Availability {
    data object Available : Availability()
    data class Unavailable(val reason: String) : Availability()
}

object AppAvailability {
    fun check(
        minProtocolVersion: Int,
        minHostVersion: String,
        watchProtocolVersion: Int,
        hostVersion: String,
    ): Availability {
        if (minProtocolVersion > watchProtocolVersion) {
            return Availability.Unavailable(
                "Needs watch protocol v$minProtocolVersion " +
                    "(connected watch is v$watchProtocolVersion). Update firmware.",
            )
        }
        if (!HostVersion.satisfies(hostVersion, minHostVersion)) {
            return Availability.Unavailable(
                "Needs companion $minHostVersion+ (this app is $hostVersion). Update Slate.",
            )
        }
        return Availability.Available
    }
}

/**
 * Update policy: never auto-install a version that expands the permission set.
 */
object UpdatePolicy {
    fun permissionIncrease(
        installed: Set<ScriptPermission>,
        candidate: Set<ScriptPermission>,
    ): Set<ScriptPermission> = candidate - installed

    fun mayAutoInstall(
        installedPermissions: Set<ScriptPermission>,
        candidatePermissions: Set<ScriptPermission>,
    ): Boolean = permissionIncrease(installedPermissions, candidatePermissions).isEmpty()
}
