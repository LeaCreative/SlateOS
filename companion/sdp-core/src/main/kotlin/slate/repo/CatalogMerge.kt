package slate.repo

/**
 * Catalog merge rules for multiple repositories (§6.6).
 * Official app IDs always win; third-party must never shadow them.
 */
data class CatalogEntry(
    val app: IndexApp,
    val repoId: String,
    val repoName: String,
    val trust: RepoTrust,
    val indexUrl: String,
)

data class RepoSource(
    val id: String,
    val name: String,
    val indexUrl: String,
    val trust: RepoTrust,
    /** Ed25519 SPKI base64; official uses [OfficialRepoTrust], third-party may omit (unsigned → reject). */
    val publicKeySpkiBase64: String?,
)

object CatalogMerge {
    fun merge(entries: List<CatalogEntry>): MergeResult {
        val officialIds = entries.filter { it.trust == RepoTrust.Official }.map { it.app.id }.toSet()
        val kept = ArrayList<CatalogEntry>()
        val shadowed = ArrayList<CatalogEntry>()
        val byId = LinkedHashMap<String, CatalogEntry>()
        // Official first
        for (e in entries.filter { it.trust == RepoTrust.Official }) {
            val prev = byId[e.app.id]
            if (prev == null || HostVersion.compare(e.app.version, prev.app.version) > 0) {
                byId[e.app.id] = e
            }
        }
        for (e in entries.filter { it.trust == RepoTrust.ThirdParty }) {
            if (e.app.id in officialIds || byId.containsKey(e.app.id) && byId[e.app.id]!!.trust == RepoTrust.Official) {
                shadowed += e
                continue
            }
            val prev = byId[e.app.id]
            if (prev == null) {
                byId[e.app.id] = e
            } else if (prev.trust == RepoTrust.ThirdParty &&
                HostVersion.compare(e.app.version, prev.app.version) > 0
            ) {
                byId[e.app.id] = e
            }
        }
        kept += byId.values
        return MergeResult(kept = kept.sortedBy { it.app.name.lowercase() }, shadowed = shadowed)
    }

    data class MergeResult(
        val kept: List<CatalogEntry>,
        val shadowed: List<CatalogEntry>,
    )
}
