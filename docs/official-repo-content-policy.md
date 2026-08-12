# Official Slate repository — content policy

**Status:** Draft for launch-closed → invitation → open submissions (roadmap §6.1).  
**Owner:** Slate companion maintainers  
**Applies to:** Packages listed in the **official** signed index only.

We are responsible under Google Play policy for what the companion can be made to do
via interpreted sub-apps. This document is the operational commitment behind that.

## What we allow

- Sub-apps that are **JavaScript + assets only** (`.slate` zip). No dex, JAR, `.so`, or
  native code. No attempts to escape the sandbox or obtain undeclared permissions.
- Clear, accurate manifests: identity, permissions, `allowedHosts`, version constraints.
- Utility, health-adjacent (within `health.read` and Host permissions), media, transit,
  notes, timers, and similar watch-facing tools that respect user attention and battery.
- Open-source or proprietary packages whose authors accept this policy and takedown process.

## What we reject / remove

- Anything that violates Play policies (including Device & Network Abuse, deceptive
  behavior, malware, malware, unauthorized data collection, or child-endangerment rules).
- Phishing, impersonation of system UI, or misleading permission / provenance claims.
- Undeclared network hosts, permission escalation between versions without disclosure.
- Harassment, illegal content, or content we cannot legally distribute in supported regions.
- Packages that attempt sandbox escape, reflection into Android APIs, or filesystem access
  outside the binding surface.
- Spam, clone flooding, or indexes designed to confuse provenance.

## Review criteria (before listing or updating)

1. **Manifest validation** passes (`ManifestParser`) — required fields, known permissions,
   `requires` ⊆ known capabilities, `http` ⇒ `allowedHosts`.
2. **Permission diff** vs previous version: any increase is called out in the review notes
   and will force in-app consent (never silent auto-update).
3. **Static checks:** no obvious `eval` of remote strings beyond the host bridge; no
   attempts to reach undeclared hosts; entry script present.
4. **Behavioral smoke:** load in the desktop emulator / script runtime; confirm render and
   input paths; governor budgets not trivially abused.
5. **Provenance:** author contact, license, and changelog for the version.
6. **Screenshots / description** match actual behavior (no bait-and-switch).

Launch-closed: only maintainer-authored apps. Invitation: PR review against the public
index git repo. Open submissions: all of the above plus capacity for ongoing moderation.

## Takedown process

1. **Report:** email `security@slate.example` (replace with real address) or open a
   GitHub Security Advisory / issue labeled `repo-takedown` on the index repository.
   Include app id, version, SHA-256, and evidence.
2. **Triage:** within **2 business days** acknowledge; within **5 business days** initial
   severity (critical / high / normal).
3. **Critical** (malware, active phishing, clear Play violation): remove from index and
   re-sign **same day** once confirmed; notify users in-app on next index refresh that
   the app was delisted (installed copies remain until the user removes them — we do not
   remotely wipe without a separate signed kill-switch design).
4. **High / normal:** author contacted with a fix deadline (typically 7–14 days). Failure
   to remediate → delist and re-sign.
5. **Appeal:** one written appeal within 14 days of delist; decision logged in the index
   repo’s `TAKEDOWN.md` log (date, id, version, reason code, public summary).
6. **Law enforcement / legal orders:** comply as required; document the legal basis in the
   private ops log; public summary when lawful.

## Author responsibilities

- Keep contact information current.
- Do not ship permission increases without calling them out in the changelog.
- Do not impersonate Official provenance (unsigned indexes, forged signatures).
- Report vulnerabilities in the runtime/bindings through the security contact, not via
  silent exploitation.

## Relationship to third-party repositories

Third-party indexes are **user-added** and clearly labeled. They receive the **same**
binding permissions as official packages for everything they declare (the host
whitelist still applies; there is no reduced third-party set). They must not shadow
official app IDs. We do not endorse third-party content; Play responsibility for
official listings remains ours. Users who add third-party repos accept additional
risk after an explicit trust prompt in the companion UI.

## Change log

| Date | Change |
|---|---|
| 2026-08-11 | Third-party / sideload: same declared binding ceiling as Official (no reduced set) |
| 2026-07-27 | Initial draft for M13 |
