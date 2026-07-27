/**
 * Example JS sub-app contract (M12).
 *
 * Real sample: `companion/examples/timer/`
 * Runtime docs: `docs/script-runtime.md`
 *
 * Host posts JSON matching slate.host.HostInbound; isolate returns a JSON array
 * of HostOutbound. Display lists are base64 of raw SDP bytes (byte-identical to
 * Kotlin DisplayListBuilder — see JsUiGoldenTest).
 *
 * Prefer the §6.3 exports (`onFocus` / `render` / `onInput` / `onEvent` / `onBlur`)
 * wired by `shared-js/slate_host.js`. A raw `dispatch(msg)` export also works.
 */
