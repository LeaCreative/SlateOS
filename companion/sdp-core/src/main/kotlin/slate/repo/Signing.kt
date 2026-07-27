package slate.repo

import java.security.KeyFactory
import java.security.Signature
import java.security.spec.PKCS8EncodedKeySpec
import java.security.spec.X509EncodedKeySpec
import java.util.Base64

/**
 * SHA-256 helpers for package integrity (§6.6).
 * Not shared with MCUBoot ECDSA-P256 firmware signing.
 */
object Digests {
    fun sha256Hex(bytes: ByteArray): String {
        val dig = java.security.MessageDigest.getInstance("SHA-256").digest(bytes)
        return dig.joinToString("") { "%02x".format(it) }
    }

    fun matches(bytes: ByteArray, expectedHex: String): Boolean {
        val exp = expectedHex.trim().lowercase()
        return exp.length == 64 && sha256Hex(bytes) == exp
    }
}

/**
 * Ed25519 verification for the **repository index** only.
 *
 * Trust domain is separate from firmware (MCUBoot ECDSA-P256). Do not reuse
 * keys, certs, or verification code paths across the two.
 *
 * Signature is over the raw UTF-8 bytes of `index.json` as served.
 * Detached signature is base64 in `index.json.sig`.
 */
object Ed25519IndexVerifier {
    fun verify(
        indexBytes: ByteArray,
        signatureBase64: String,
        publicKeySpkiBase64: String,
    ): Boolean {
        return try {
            val sigBytes = Base64.getDecoder().decode(signatureBase64.trim())
            val pubBytes = Base64.getDecoder().decode(publicKeySpkiBase64.trim())
            val kf = KeyFactory.getInstance("Ed25519")
            val pub = kf.generatePublic(X509EncodedKeySpec(pubBytes))
            val sig = Signature.getInstance("Ed25519")
            sig.initVerify(pub)
            sig.update(indexBytes)
            sig.verify(sigBytes)
        } catch (_: Throwable) {
            false
        }
    }

    /** Test / tooling helper — signs index bytes with a PKCS#8 Ed25519 private key. */
    fun sign(indexBytes: ByteArray, privateKeyPkcs8Base64: String): String {
        val kf = KeyFactory.getInstance("Ed25519")
        val priv = kf.generatePrivate(
            PKCS8EncodedKeySpec(Base64.getDecoder().decode(privateKeyPkcs8Base64.trim())),
        )
        val sig = Signature.getInstance("Ed25519")
        sig.initSign(priv)
        sig.update(indexBytes)
        return Base64.getEncoder().encodeToString(sig.sign())
    }
}

/**
 * Official repository trust material.
 *
 * ## Key rotation
 * 1. Generate a new Ed25519 keypair offline; keep the private key offline / HSM.
 * 2. Ship the **new** public key in a companion update (optionally accept both
 *    old+new for one release — dual-verify).
 * 3. Re-sign `index.json` with the new key; publish `index.json.sig`.
 * 4. After the dual-key window, drop the old public key from the app.
 *
 * ## Compromise
 * If the private key leaks: revoke by shipping a companion update that removes the
 * compromised public key, stop publishing indexes signed by it, and treat any
 * index still verifying under that key as untrusted. Installed packages remain
 * runnable offline (local cache) but **no new installs/updates** from that repo
 * until a new key is live. Notify users in-app. Firmware keys are unaffected.
 */
object OfficialRepoTrust {
    const val PUBLIC_KEY_SPKI_BASE64_PLACEHOLDER =
        "MCowBQYDK2VwAyEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="

    const val DEFAULT_INDEX_URL =
        "https://apps.slate.example/official/index.json"
}
