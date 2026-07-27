package slate.image

import slate.render.Framebuffer
import java.awt.image.BufferedImage
import java.io.ByteArrayOutputStream
import javax.imageio.ImageIO

object PngExport {
    fun framebufferToPng(fb: Framebuffer): ByteArray {
        val img = BufferedImage(fb.width, fb.height, BufferedImage.TYPE_INT_ARGB)
        for (y in 0 until fb.height) {
            for (x in 0 until fb.width) {
                img.setRGB(x, y, fb.pixels[y * fb.width + x])
            }
        }
        val out = ByteArrayOutputStream()
        ImageIO.write(img, "png", out)
        return out.toByteArray()
    }

    fun writePng(fb: Framebuffer, path: java.io.File) {
        path.parentFile?.mkdirs()
        path.writeBytes(framebufferToPng(fb))
    }
}
