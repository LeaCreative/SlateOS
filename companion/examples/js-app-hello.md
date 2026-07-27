/**
 * Example JS sub-app (future androidx.javascriptengine isolate).
 *
 * The host posts JSON messages matching slate.host.HostInbound and expects
 * JSON arrays of HostOutbound. Display lists are base64 of raw SDP bytes
 * (same bytes Kotlin's DisplayListBuilder emits).
 *
 * This file is documentation / a contract sample — not executed by the app yet.
 */

/*
// --- main.js (conceptual) ---

export const manifest = {
  id: "com.example.hello",
  name: "Hello",
  version: "1.0.0",
  minProtocolVersion: 1,
  minHostVersion: "0.1",
  priority: "normal",
  refresh: { policy: "on-change" }
};

export function dispatch(msg) {
  switch (msg.type) {
    case "focus":
    case "render":
      return [{
        type: "pushDisplayList",
        // base64 of SDP bytes from slate.ui.displayList(...) in the real sandbox
        displayListBase64: buildHelloListBase64()
      }];
    case "input":
      if (msg.op === 0x06) { // BACK
        return [{ type: "relinquishFocus" }, { type: "inputHandled" }];
      }
      return [{ type: "inputUnhandled" }];
    default:
      return [];
  }
}

// Host bridge (Kotlin) sketch:
//
// class JsSlateAppEndpoint(isolate: JavaScriptIsolate, manifest: AppManifest)
//   : SlateAppEndpoint {
//   override suspend fun dispatch(msg: HostInbound): List<HostOutbound> {
//     val json = HostJson.encodeInbound(msg)
//     val result = isolate.evaluateJavaScriptAsync("dispatch($json)")
//     return HostJson.decodeOutboundList(result)
//   }
// }
//
// KotlinSlateApp and JsSlateAppEndpoint both satisfy SlateAppEndpoint —
// the Compositor never knows which runtime produced the messages.
*/
