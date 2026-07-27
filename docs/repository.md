# Sub-app repository client (M13)

Static, signed JSON index over HTTPS — no server-side component (Bangle.js-style).

## Package format (§6.2)

`.slate` = zip:

```
manifest.json
main.js
icon.png          (optional)
assets/…          (optional)
README.md         (optional)
```

Parsed by `ManifestParser` / `SlatePackageReader` in `sdp-core`. Required fields,
known permissions only, and `requires` capabilities must be in the host’s known set
(unknown-but-required → reject). `http` requires `allowedHosts`.

## Index (§6.6)

`index.json` (+ detached `index.json.sig`). Fetched, Ed25519-verified, cached under
`cacheDir/repo-index/`. Package bytes verified with the index’s SHA-256 before install.

## Signing (Ed25519 ≠ firmware)

| Domain | Algorithm | Purpose |
|---|---|---|
| Repository index | **Ed25519** | Trust the catalog |
| Package blob | **SHA-256** (in index) | Integrity of `.slate` |
| Firmware / MCUBoot | **ECDSA-P256** | Watch image — **separate keys and code** |

### Key rotation
1. Generate a new Ed25519 keypair offline; private key stays offline / HSM.
2. Ship the new public key in a companion release (optionally dual-verify old+new for one release).
3. Re-sign and publish `index.json` + `index.json.sig`.
4. Drop the old public key after the dual-key window.

### Compromise
Remove the compromised public key in a companion update; stop publishing under that key;
block new installs/updates from indexes that only verify with it. **Already-installed**
packages keep running from the local cache (offline). Firmware signing is unaffected.

Replace `OfficialRepoTrust.PUBLIC_KEY_SPKI_BASE64_PLACEHOLDER` before any public launch.

## Multi-repo

- Official source is built-in.
- Users may add HTTPS third-party indexes (with their Ed25519 public key).
- Provenance shown on list/detail (`Official` vs repo name).
- Third-party → reduced permissions (no `http` / `location` / `health.read`) unless
  the user grants per-app on the install disclosure.
- Third-party **never shadows** an official app ID (`CatalogMerge`).

## Updates

- `RepoUpdateScheduler` (≈12 h) from the link FGS.
- Skips metered networks unless “Allow updates on metered” is on.
- **Never** auto-installs a version that adds permissions — UI requires consent.

## Offline

Installed trees live under `filesDir/repo/subapps/{id}/`. Install needs network;
**running does not**.

## Availability

Apps the watch/host cannot run stay **visible** with an explicit reason
(`AppAvailability`: protocol vs companion version).

## UI

MainActivity → **Sub-app repository** → Compose browse / detail / install disclosure /
installed / sources.

## Tests

```powershell
cd companion
.\gradlew.bat :sdp-tests:test --tests "slate.repo.*"
```

## Content policy

See [official-repo-content-policy.md](official-repo-content-policy.md).
