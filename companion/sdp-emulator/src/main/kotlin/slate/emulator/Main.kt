package slate.emulator

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.Button
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import slate.dsl.displayList
import slate.generated.SdpWire
import slate.hit.HitRect
import slate.hit.hitTest
import slate.host.HostInbound
import slate.host.HostOutbound
import slate.input.InputEventEncoder
import slate.interpreter.DisplayListInterpreter
import slate.script.JsSlateAppEndpoint
import slate.script.RhinoScriptEngine
import slate.script.ScriptConsole
import slate.script.ScriptPermission
import slate.script.ScriptResources
import slate.wire.Colors
import slate.wire.pal

/**
 * Desktop emulator with JS sub-app runtime (Rhino) — no watch, no phone.
 * See docs/script-runtime.md.
 */
fun main() = application {
    Window(onCloseRequest = ::exitApplication, title = "Slate Emulator — JS Timer") {
        val interpreter = remember { DisplayListInterpreter() }
        val blank = remember {
            displayList {
                palette(0, Colors.BLACK)
                clear(pal(0))
                commit()
            }
        }
        var framebuffer by remember { mutableStateOf(interpreter.render(blank).framebuffer) }
        var hitRects by remember { mutableStateOf<List<HitRect>>(emptyList()) }
        var lastEventHex by remember { mutableStateOf("—") }
        var consoleLine by remember { mutableStateOf("Tap Load to start the timer JS app.") }
        var endpoint by remember { mutableStateOf<JsSlateAppEndpoint?>(null) }
        val timerJobs = remember { HashMap<String, Job>() }
        val scope = rememberCoroutineScope()

        fun applyOutbound(out: List<HostOutbound>) {
            for (m in out) {
                when (m) {
                    is HostOutbound.PushDisplayList -> {
                        val rendered = interpreter.render(m.bytes)
                        framebuffer = rendered.framebuffer
                        hitRects = rendered.hitRects
                    }
                    is HostOutbound.Log ->
                        consoleLine = "${m.level}: ${m.message}"
                    else -> Unit
                }
            }
            val snap = ScriptConsole.snapshot().lastOrNull()
            if (snap != null) consoleLine = "${snap.kind} ${snap.message}"
        }

        fun boot() {
            scope.launch {
                endpoint?.close()
                timerJobs.values.forEach { it.cancel() }
                timerJobs.clear()
                val eng = RhinoScriptEngine()
                val ep = JsSlateAppEndpoint.loadTimer(
                    engine = eng,
                    hostHeld = setOf(ScriptPermission.Storage),
                    onTimerSet = { id, ms ->
                        timerJobs[id]?.cancel()
                        timerJobs[id] = scope.launch {
                            while (isActive) {
                                delay(ms)
                                endpoint?.let { e ->
                                    applyOutbound(
                                        e.dispatch(
                                            HostInbound.SystemEvent("timer", """{"id":"$id"}"""),
                                        ),
                                    )
                                }
                            }
                        }
                    },
                    onTimerClear = { id -> timerJobs.remove(id)?.cancel() },
                )
                ep.installRuntime(appJs = ScriptResources.read(ScriptResources.TIMER_MAIN))
                endpoint = ep
                applyOutbound(ep.dispatch(HostInbound.Focus))
                consoleLine = "Timer JS running (Rhino). Tap Start/Stop."
            }
        }

        MaterialTheme {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0xFF1A1A1A))
                    .padding(16.dp),
            ) {
                Text("JS sub-app (Rhino) — same slate.ui bytes as Android", color = Color.White)
                Text(consoleLine, color = Color.LightGray, modifier = Modifier.padding(top = 8.dp))
                Text("Last input: $lastEventHex", color = Color.Gray)
                Button(onClick = { boot() }, modifier = Modifier.padding(top = 8.dp)) {
                    Text("Load / reload timer")
                }
                Canvas(
                    modifier = Modifier
                        .padding(top = 16.dp)
                        .size(240.dp)
                        .pointerInput(hitRects, endpoint) {
                            detectTapGestures { offset ->
                                val ep = endpoint ?: return@detectTapGestures
                                val px = offset.x.toInt().coerceIn(0, SdpWire.DISPLAY_SIZE - 1)
                                val py = offset.y.toInt().coerceIn(0, SdpWire.DISPLAY_SIZE - 1)
                                val elem = hitTest(px, py, hitRects)
                                lastEventHex = InputEventEncoder.tap(elem, px, py)
                                    .joinToString(" ") { "%02X".format(it) }
                                scope.launch {
                                    applyOutbound(
                                        ep.dispatch(
                                            HostInbound.Input(
                                                op = SdpWire.InputOp.TAP,
                                                elemId = elem,
                                                x = px,
                                                y = py,
                                            ),
                                        ),
                                    )
                                }
                            }
                        },
                ) {
                    val fb = framebuffer
                    val scaleX = size.width / fb.width
                    val scaleY = size.height / fb.height
                    for (y in 0 until fb.height) {
                        for (x in 0 until fb.width) {
                            drawRect(
                                color = Color(fb.pixels[y * fb.width + x]),
                                topLeft = Offset(x * scaleX, y * scaleY),
                                size = Size(scaleX, scaleY),
                            )
                        }
                    }
                }
            }
        }
    }
}
