package slate.emulator

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.MaterialTheme
import androidx.compose.material.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import slate.dsl.displayList
import slate.generated.SdpWire
import slate.hit.hitTest
import slate.input.InputEventEncoder
import slate.interpreter.DisplayListInterpreter
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb

fun main() = application {
    val demoList = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        clear(pal(0))
        text(
            font = Font.LARGE,
            x = 120,
            y = 100,
            align = Align.CENTER,
            color = pal(1),
            text = "12:34",
        )
        element(id = 1, x = 0, y = 200, w = 240, h = 40) {
            rectRound(0, 200, 240, 40, r = 8, color = rgb(0x4208), style = Style.FILL)
        }
        commit()
    }

    val interpreter = DisplayListInterpreter()
    var output = interpreter.render(demoList)
    var lastEventHex by mutableStateOf("—")

    Window(onCloseRequest = ::exitApplication, title = "Slate Emulator (240×240)") {
        MaterialTheme {
            Column(
                modifier = Modifier
                    .fillMaxSize()
                    .background(Color(0xFF1A1A1A))
                    .padding(16.dp),
            ) {
                Text("Tap the watch face — emits SDP TAP input events (channel 2).", color = Color.White)
                Text("Last event: $lastEventHex", color = Color.LightGray, modifier = Modifier.padding(top = 8.dp))
                Canvas(
                    modifier = Modifier
                        .padding(top = 16.dp)
                        .size(240.dp)
                        .pointerInput(output.hitRects) {
                            detectTapGestures { offset ->
                                val px = offset.x.toInt().coerceIn(0, SdpWire.DISPLAY_SIZE - 1)
                                val py = offset.y.toInt().coerceIn(0, SdpWire.DISPLAY_SIZE - 1)
                                val elem = hitTest(px, py, output.hitRects)
                                val bytes = InputEventEncoder.tap(elem, px, py)
                                lastEventHex = bytes.joinToString(" ") { "%02X".format(it) }
                            }
                        },
                ) {
                    val fb = output.framebuffer
                    val scaleX = size.width / fb.width
                    val scaleY = size.height / fb.height
                    for (y in 0 until fb.height) {
                        for (x in 0 until fb.width) {
                            val argb = fb.pixels[y * fb.width + x]
                            drawRect(
                                color = Color(argb),
                                topLeft = Offset(x * scaleX, y * scaleY),
                                size = androidx.compose.ui.geometry.Size(scaleX, scaleY),
                            )
                        }
                    }
                }
            }
        }
    }
}
