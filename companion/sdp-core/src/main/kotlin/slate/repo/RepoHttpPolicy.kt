package slate.repo

import java.net.URL

/**
 * HTTPS / redirect-downgrade policy for repository fetches.
 *
 * Companion [slate.app.repo.RepoHttp] uses HttpURLConnection with
 * `instanceFollowRedirects = true`. The platform refuses cross-protocol
 * redirects; we still assert the final URL is https so a stack swap cannot
 * drop the guard. Prefer same-host CDN redirects; hop budget is the platform
 * default (~[MAX_REDIRECTS_DOCUMENTED]).
 */
object RepoHttpPolicy {
    const val MAX_REDIRECTS_DOCUMENTED = 20

    fun requireHttps(protocol: String, where: String = "after redirects") {
        if (!protocol.equals("https", ignoreCase = true)) {
            throw RepoHttpPolicyException(
                when (where) {
                    "initial URL" -> "HTTPS required for repository downloads"
                    else -> "HTTPS downgrade redirect blocked"
                },
            )
        }
    }

    /** Resolve Location against [from]; rejects non-https targets. */
    fun resolveRedirect(from: URL, location: String): URL {
        val next = URL(from, location)
        requireHttps(next.protocol, "after redirects")
        return next
    }
}

class RepoHttpPolicyException(message: String) : Exception(message)
