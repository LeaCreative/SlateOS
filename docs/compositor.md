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

`KotlinSlateApp` adapts hooks → messages. `ClockApp` / `TestApp` subclass it.

### JS (future)

`companion/examples/js-app-hello.md` shows the same contract as JSON + base64 lists.
A `JsSlateAppEndpoint` will `evaluateJavaScriptAsync("dispatch(...)")` and decode
outbound messages — the [Compositor] never knows which runtime answered.

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

## Tests

```powershell
cd companion
.\gradlew.bat :sdp-tests:test --tests "slate.compositor.*"
```
