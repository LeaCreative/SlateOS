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
 * §6.1 / §6.6 third-party reduced permission set.
 * No http, health, or location unless the user grants per-app.
 */
object PermissionPolicy {
    val THIRD_PARTY_BLOCKED: Set<ScriptPermission> = setOf(
        ScriptPermission.Http,
        ScriptPermission.HealthRead,
        ScriptPermission.Location,
    )

    fun effective(
        declared: Set<ScriptPermission>,
        trust: RepoTrust,
        userGrantedSensitive: Set<ScriptPermission> = emptySet(),
    ): Set<ScriptPermission> {
        if (trust == RepoTrust.Official) return declared
        return declared.filter { p ->
            p !in THIRD_PARTY_BLOCKED || p in userGrantedSensitive
        }.toSet()
    }

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
