# Compositor & app host (M8)

## Process-boundary-safe contract

Apps talk to the host only through [slate.host.SlateAppEndpoint]:

- **Inbound** (`HostInbound`): Create/Start/Focus/Blur/Stop/Destroy, Render, Input, SystemEvent
- **Outbound** (`HostOutbound`): PushDisplayList(bytes), Invalidate, RequestFocus, RelinquishFocus, InputHandled/Unhandled, Log

Rules:

- Display lists are **opaque byte arrays** (SDP channel-1 payloads)
- No Android `Context` / `View` / `BluetoothGatt` across the boundary
- No shared mutable objects — app state lives inside the endpoint (or JS isolate)
- Lifecycle is explicit; everything is `suspend` / async

### Kotlin reference

`KotlinSlateApp` adapts hooks → messages. `ClockApp` / `TestApp` /
`NotificationsApp` subclass it. See [notifications.md](notifications.md) for M9.

### JS (M12)

See [script-runtime.md](script-runtime.md). `JsSlateAppEndpoint` evaluates
`__slate_dispatch(...)` inside an isolate (Android) or Rhino (desktop/tests) and
decodes outbound JSON — the [Compositor] never knows which runtime answered.
Display lists are base64 of the same SDP bytes the Kotlin DSL emits (golden-tested).

## Focus-stealing rules

Priority: **CRITICAL > INTERRUPT > NORMAL > AMBIENT**

| Situation | Result |
|---|---|
| Requester rank **>** focused | **Steal** — previous gets Blur, stays under on Push |
| Rank **equal** | Steal only for `UserNavigation` or same app; system raises denied |
| Rank **<** focused | **Deny** |
| `minProtocolVersion` > watch | **Deny** + push “Update watch” screen |

Displaced app: **Blur**, not Destroy. Pop / RelinquishFocus restores the previous
entry with **Focus** again. Ambient is the singleton base under higher screens.

## Quotas & credit

| Path | Cap |
|---|---|
| Focused non-ambient | 10 pushes / s |
| Ambient | 1 push / min |
| Watch CREDIT | never push if `list.size > freeBytes` |

Refresh policies (`on-change` / `periodic` / `manual`) only mark dirty; the compositor
coalesces on `tick()` and still applies quota + credit.

## Repository (M13)

See [repository.md](repository.md) and [official-repo-content-policy.md](official-repo-content-policy.md).
Sub-apps arrive as signed-index `.slate` packages; the compositor still only sees
`SlateAppEndpoint` once the script runtime loads an installed package.

## Tests

```powershell
cd companion
.\gradlew.bat :sdp-tests:test --tests "slate.compositor.*"
```
