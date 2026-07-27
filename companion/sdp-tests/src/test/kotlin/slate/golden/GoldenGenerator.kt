package slate.golden

import slate.dsl.displayList
import slate.image.PngExport
import slate.interpreter.DisplayListInterpreter
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb
import java.io.File

/** Refresh golden PNG/BIN files under sdp-tests/src/test/resources/golden */
fun main() {
    val interpreter = DisplayListInterpreter()
    val dir = File("sdp-tests/src/test/resources/golden")
    dir.mkdirs()

    fun save(name: String, bytes: ByteArray) {
        File(dir, "$name.bin").writeBytes(bytes)
        val out = interpreter.render(bytes)
        PngExport.writePng(out.framebuffer, File(dir, "$name.png"))
        println("wrote golden $name")
    }

    save("clear_black", displayList { clear(Colors.BLACK); commit() })

    save("clock_face", displayList {
        palette(0, rgb(0x0000))
        palette(1, rgb(0xFFFF))
        clear(pal(0))
        text(font = 0, x = 88, y = 100, align = Align.LEFT, color = pal(1), text = "12:4")
        progressArc(cx = 120, cy = 120, r = 50, pct = 40, fg = pal(1), bg = rgb(0x0808), width = 3)
        commit()
    })

    save("m4_example", displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        clear(pal(0))
        text(font = Font.LARGE, x = 120, y = 100, align = Align.CENTER, color = pal(1), text = "12:34")
        element(id = 1, x = 0, y = 200, w = 240, h = 40) {
            rectRound(0, 200, 240, 40, r = 8, color = rgb(0x4208), style = Style.FILL)
        }
        commit()
    })
}
