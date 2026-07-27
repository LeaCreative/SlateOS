package slate.golden

import slate.dsl.displayList
import slate.image.PngExport
import slate.interpreter.DisplayListInterpreter
import slate.parse.SdpStatus
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class GoldenRenderTests {
    private val interpreter = DisplayListInterpreter()

    private fun loadGoldenPng(name: String): ByteArray =
        javaClass.classLoader.getResourceAsStream("golden/$name.png")!!.readBytes()

    private fun loadGoldenBin(name: String): ByteArray =
        javaClass.classLoader.getResourceAsStream("golden/$name.bin")!!.readBytes()

    @Test
    fun clockFace_bytesMatchGolden() {
        val list = clockFaceList()
        assertContentEquals(loadGoldenBin("clock_face"), list)
    }

    @Test
    fun clockFace_pngMatchesGolden() {
        val out = interpreter.render(clockFaceList())
        assertEquals(SdpStatus.Ok, out.status)
        val png = PngExport.framebufferToPng(out.framebuffer)
        assertContentEquals(loadGoldenPng("clock_face"), png)
    }

    @Test
    fun clearBlack_pngMatchesGolden() {
        val list = displayList {
            clear(Colors.BLACK)
            commit()
        }
        val out = interpreter.render(list)
        val png = PngExport.framebufferToPng(out.framebuffer)
        assertContentEquals(loadGoldenPng("clear_black"), png)
    }

    @Test
    fun m4Example_dslBuildsAndRenders() {
        val list = displayList {
            palette(0, Colors.BLACK)
            palette(1, Colors.WHITE)
            clear(pal(0))
            text(font = Font.LARGE, x = 120, y = 100, align = Align.CENTER, color = pal(1), text = "12:34")
            element(id = 1, x = 0, y = 200, w = 240, h = 40) {
                rectRound(0, 200, 240, 40, r = 8, color = rgb(0x4208), style = Style.FILL)
            }
            commit()
        }
        val out = interpreter.render(list)
        assertEquals(SdpStatus.Ok, out.status)
        assertTrue(out.hitRects.any { it.id == 1 })
    }

  private fun clockFaceList(): ByteArray = displayList {
        palette(0, rgb(0x0000))
        palette(1, rgb(0xFFFF))
        clear(pal(0))
        text(font = 0, x = 88, y = 100, align = Align.LEFT, color = pal(1), text = "12:4")
        progressArc(cx = 120, cy = 120, r = 50, pct = 40, fg = pal(1), bg = rgb(0x0808), width = 3)
        commit()
    }
}
