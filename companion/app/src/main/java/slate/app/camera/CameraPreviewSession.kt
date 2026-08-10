package slate.app.camera

import android.content.Context
import android.util.Size
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import slate.camera.FrameGovernor
import slate.camera.Rgb332
import slate.dsl.displayList
import slate.generated.SdpWire
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * CameraX analysis → 60×60 RGB332 on the phone → host-built PATCH display lists.
 * Frames never enter the JS isolate.
 *
 * If CameraX bind fails (no permission / no camera), falls back to a synthetic
 * moving pattern so the PATCH path remains demonstrable.
 */
class CameraPreviewSession(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val onPatchList: (ByteArray) -> Unit,
    private val onStatus: (state: String, fpsHint: Double) -> Unit,
    private val onCaptured: () -> Unit,
) {
    private val executor = Executors.newSingleThreadExecutor()
    private val governor = FrameGovernor(budgetBytesPerSec = 36_000, maxInFlight = 1)
    private val running = AtomicBoolean(false)
    private var provider: ProcessCameraProvider? = null
    private var synthJob: java.util.concurrent.Future<*>? = null

    var slot: Int = 0
    var patchX: Int = 90
    var patchY: Int = 40
    var patchW: Int = 60
    var patchH: Int = 60

    fun start() {
        if (!running.compareAndSet(false, true)) return
        governor.reset()
        onStatus("starting", governor.measuredFpsHint(patchW * patchH))
        val future = ProcessCameraProvider.getInstance(context)
        future.addListener({
            try {
                val p = future.get()
                provider = p
                bind(p)
                onStatus("streaming", governor.measuredFpsHint(patchW * patchH))
            } catch (t: Throwable) {
                startSynthetic()
                onStatus("synthetic", governor.measuredFpsHint(patchW * patchH))
            }
        }, ContextCompat.getMainExecutor(context))
    }

    fun stop() {
        running.set(false)
        synthJob?.cancel(true)
        synthJob = null
        val p = provider
        provider = null
        if (p != null) {
            // CameraX bind/unbind must run on the main executor.
            ContextCompat.getMainExecutor(context).execute {
                runCatching { p.unbindAll() }
            }
        }
        onStatus("idle", 0.0)
    }

    fun captureStill() {
        onCaptured()
    }

    fun onWatchAck() {
        governor.onFrameAcked()
    }

    private fun bind(p: ProcessCameraProvider) {
        p.unbindAll()
        val analysis = ImageAnalysis.Builder()
            .setTargetResolution(Size(160, 160))
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
        analysis.setAnalyzer(executor) { image ->
            try {
                if (!running.get()) return@setAnalyzer
                processYuv(image)
            } finally {
                image.close()
            }
        }
        p.bindToLifecycle(lifecycleOwner, CameraSelector.DEFAULT_BACK_CAMERA, analysis)
    }

    private fun startSynthetic() {
        synthJob = executor.submit {
            var t = 0
            while (running.get() && !Thread.currentThread().isInterrupted) {
                val argb = IntArray(patchW * patchH)
                for (y in 0 until patchH) {
                    for (x in 0 until patchW) {
                        val v = ((x + t) xor (y + t / 2)) and 0xff
                        argb[y * patchW + x] = (0xff shl 24) or (v shl 16) or ((255 - v) shl 8) or 0x40
                    }
                }
                pushRgb(Rgb332.convertArgb8888(argb, patchW, patchH))
                t = (t + 3) and 0xff
                try {
                    Thread.sleep(100L)
                } catch (_: InterruptedException) {
                    break
                }
            }
        }
    }

    private fun processYuv(image: ImageProxy) {
        val w = image.width
        val h = image.height
        val yPlane = image.planes[0]
        val yBuf = yPlane.buffer
        val rowStride = yPlane.rowStride
        val argb = IntArray(w * h)
        val row = ByteArray(rowStride)
        var i = 0
        for (y in 0 until h) {
            yBuf.position(y * rowStride)
            val toRead = minOf(rowStride, yBuf.remaining())
            yBuf.get(row, 0, toRead)
            for (x in 0 until w) {
                val yy = row[x].toInt() and 0xff
                argb[i++] = (0xff shl 24) or (yy shl 16) or (yy shl 8) or yy
            }
        }
        val rgb = Rgb332.downscaleArgbToRgb332(argb, w, h, patchW, patchH)
        pushRgb(rgb)
    }

    private fun pushRgb(rgb: ByteArray) {
        val now = System.currentTimeMillis()
        if (!governor.tryAccept(rgb.size, now)) return
        // PATCH only — do not CLEAR; chrome stays from the script's last list.
        val dl = displayList {
            patch(
                slot = slot,
                x = patchX,
                y = patchY,
                w = patchW,
                h = patchH,
                format = SdpWire.PatchFormat.RGB332,
                encoding = SdpWire.PatchEncoding.RAW,
                data = rgb,
            )
            commit()
        }
        onPatchList(dl)
        governor.onFrameAcked()
    }

    fun close() {
        stop()
        executor.shutdownNow()
    }
}
