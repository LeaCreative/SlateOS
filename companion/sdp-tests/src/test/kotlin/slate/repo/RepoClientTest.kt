package slate.repo

import java.io.ByteArrayOutputStream
import java.security.KeyPairGenerator
import java.util.Base64
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import slate.script.ScriptPermission

class ManifestParserTest {
    @Test
    fun acceptsValidManifest() {
        val m = ManifestParser.parse(
            """
            {
              "id": "com.example.transit",
              "name": "Transit",
              "version": "1.2.0",
              "minProtocolVersion": 2,
              "minHostVersion": "1.3",
              "permissions": ["http", "storage"],
              "http": { "allowedHosts": ["api.example"] },
              "requires": ["slate.ui", "slate.timer"]
            }
            """.trimIndent(),
        )
        assertEquals("com.example.transit", m.id)
        assertTrue(ScriptPermission.Http in m.permissions)
    }

    @Test
    fun rejectsMissingRequired() {
        assertFailsWith<ManifestException> {
            ManifestParser.parse("""{"id":"x","name":"n","version":"1"}""")
        }
    }

    @Test
    fun rejectsUnknownPermission() {
        assertFailsWith<ManifestException> {
            ManifestParser.parse(
                """
                {
                  "id": "a", "name": "A", "version": "1",
                  "minProtocolVersion": 1, "minHostVersion": "0.1",
                  "permissions": ["root"]
                }
                """.trimIndent(),
            )
        }
    }

    @Test
    fun rejectsUnknownRequiredCapability() {
        val ex = assertFailsWith<ManifestException> {
            ManifestParser.parse(
                """
                {
                  "id": "a", "name": "A", "version": "1",
                  "minProtocolVersion": 1, "minHostVersion": "0.1",
                  "requires": ["slate.telepathy"]
                }
                """.trimIndent(),
            )
        }
        assertTrue(ex.message!!.contains("unknown-but-required"))
    }

    @Test
    fun rejectsHttpWithoutHosts() {
        assertFailsWith<ManifestException> {
            ManifestParser.parse(
                """
                {
                  "id": "a", "name": "A", "version": "1",
                  "minProtocolVersion": 1, "minHostVersion": "0.1",
                  "permissions": ["http"]
                }
                """.trimIndent(),
            )
        }
    }
}

class IndexAndSigningTest {
    @Test
    fun ed25519RoundTripAndSha256Package() {
        val kpg = KeyPairGenerator.getInstance("Ed25519")
        val kp = kpg.generateKeyPair()
        val pub = Base64.getEncoder().encodeToString(kp.public.encoded)
        val priv = Base64.getEncoder().encodeToString(kp.private.encoded)

        val indexJson = """
            {
              "schema": 1,
              "updated": "2026-07-27T00:00:00Z",
              "apps": [{
                "id": "com.example.demo",
                "version": "1.0.0",
                "name": "Demo",
                "minProtocolVersion": 1,
                "minHostVersion": "0.1",
                "permissions": ["storage"],
                "size": 1,
                "sha256": "${"ab".repeat(32)}",
                "url": "https://apps.example.org/demo.slate",
                "screenshots": ["https://apps.example.org/s.png"]
              }]
            }
        """.trimIndent()
        val bytes = indexJson.toByteArray(Charsets.UTF_8)
        val sig = Ed25519IndexVerifier.sign(bytes, priv)
        assertTrue(Ed25519IndexVerifier.verify(bytes, sig, pub))
        assertFalse(Ed25519IndexVerifier.verify(bytes, sig, pub.replace('A', 'B')))

        val index = IndexParser.parse(indexJson)
        assertEquals(1, index.apps.size)
        assertEquals("com.example.demo", index.apps[0].id)
    }

    @Test
    fun packageZipRoundTrip() {
        val manifest = """
            {
              "id": "slate.demo",
              "name": "Demo",
              "version": "1.0.0",
              "minProtocolVersion": 1,
              "minHostVersion": "0.1",
              "entry": "main.js",
              "permissions": ["storage"]
            }
        """.trimIndent()
        val zip = zipOf(
            "manifest.json" to manifest.toByteArray(),
            "main.js" to "function render(){return [];}".toByteArray(),
        )
        val sha = Digests.sha256Hex(zip)
        val pkg = SlatePackageReader.open(zip, sha)
        assertEquals("slate.demo", pkg.manifest.id)
        assertFailsWith<PackageException> {
            SlatePackageReader.open(zip, "00".repeat(32))
        }
    }
}

class PolicyTest {
    @Test
    fun thirdPartyNeverShadowsOfficial() {
        val official = CatalogEntry(
            app = IndexApp(
                id = "slate.timer", version = "1.0.0", name = "Timer",
                sha256 = "aa".repeat(32), url = "https://a.example/t.slate",
            ),
            repoId = "official", repoName = "Official",
            trust = RepoTrust.Official, indexUrl = "https://a.example/index.json",
        )
        val evil = CatalogEntry(
            app = IndexApp(
                id = "slate.timer", version = "9.0.0", name = "Evil Timer",
                sha256 = "bb".repeat(32), url = "https://evil.example/t.slate",
            ),
            repoId = "tp", repoName = "Evil",
            trust = RepoTrust.ThirdParty, indexUrl = "https://evil.example/index.json",
        )
        val merged = CatalogMerge.merge(listOf(official, evil))
        assertEquals(1, merged.kept.size)
        assertEquals(RepoTrust.Official, merged.kept[0].trust)
        assertEquals(1, merged.shadowed.size)
    }

    @Test
    fun thirdPartyGetsDeclaredPermissions() {
        val declared = setOf(
            ScriptPermission.Storage,
            ScriptPermission.Http,
            ScriptPermission.Location,
            ScriptPermission.News,
        )
        val eff = PermissionPolicy.effective(declared, RepoTrust.ThirdParty)
        assertEquals(declared, eff)
        assertTrue(PermissionPolicy.blockedByDefault(declared, RepoTrust.ThirdParty).isEmpty())
        assertEquals(
            declared,
            PermissionPolicy.effective(declared, RepoTrust.Official),
        )
    }

    @Test
    fun updateConsentOnPermissionIncrease() {
        val old = setOf(ScriptPermission.Storage)
        val neu = setOf(ScriptPermission.Storage, ScriptPermission.Http)
        assertFalse(UpdatePolicy.mayAutoInstall(old, neu))
        assertTrue(UpdatePolicy.mayAutoInstall(neu, old))
        assertTrue(UpdatePolicy.mayAutoInstall(old, old))
    }

    @Test
    fun availabilityReasonsVisible() {
        val proto = AppAvailability.check(2, "0.1", watchProtocolVersion = 1, hostVersion = "1.0")
        assertTrue(proto is Availability.Unavailable)
        assertTrue((proto as Availability.Unavailable).reason.contains("watch protocol"))

        val host = AppAvailability.check(1, "2.0", watchProtocolVersion = 1, hostVersion = "1.0")
        assertTrue(host is Availability.Unavailable)
        assertTrue((host as Availability.Unavailable).reason.contains("companion"))
    }
}

private fun zipOf(vararg files: Pair<String, ByteArray>): ByteArray {
    val out = ByteArrayOutputStream()
    ZipOutputStream(out).use { zos ->
        for ((name, data) in files) {
            zos.putNextEntry(ZipEntry(name))
            zos.write(data)
            zos.closeEntry()
        }
    }
    return out.toByteArray()
}
