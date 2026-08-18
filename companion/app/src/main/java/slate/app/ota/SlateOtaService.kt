package slate.app.ota

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import slate.app.link.LinkLog
import slate.app.link.SharedLink
import slate.frame.SdpFrame
import java.security.MessageDigest

data class SlateOtaState(
    val active: Boolean = false,
    val progress: Int = 0,
    val message: String = "Select a slate-dfu.zip to update running Slate firmware.",
    val error: String? = null,
)

/**
 * Foreground service (`connectedDevice`) for Slate→Slate SDP channel-5 OTA.
 *
 * Reuses the existing [SharedLink] GATT connection — does NOT disconnect the
 * link service or open a second GATT. Compositor/heartbeat/RTT traffic is
 * paused via [SharedLink.benchmarkPaused] during transfer.
 *
 * After a successful COMMIT, the watch reboots. The link service's reconnect
 * watchdog will reconnect through HELLO negotiation, causing the firmware to
 * write IMAGE_OK (boot::tick_confirm, after the link is held ~10 s).
 */
class SlateOtaService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private val watchMessages = Channel<ByteArray>(capacity = 64)
    private val otaListener: (ByteArray) -> Unit = { msg ->
        watchMessages.trySend(msg)
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
        try {
            ServiceCompat.startForeground(
                this,
                NOTIFICATION_ID,
                notification("Preparing OTA", 0),
                if (Build.VERSION.SDK_INT >= 34) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                } else {
                    0
                },
            )
        } catch (t: SecurityException) {
            fail("Bluetooth permission required for OTA")
            return
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> {
                job?.cancel()
                publish(SlateOtaState(message = "OTA cancelled"))
                stopSelf()
            }
            ACTION_START -> {
                if (job?.isActive == true) return START_NOT_STICKY
                val uri = intent.getStringExtra(EXTRA_URI)?.let(Uri::parse)
                if (uri == null) {
                    fail("Missing DFU package URI")
                    return START_NOT_STICKY
                }
                startOta(uri)
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        job?.cancel()
        val gatt = SharedLink.gatt(applicationContext)
        gatt.removeOtaListener(otaListener)
        SharedLink.benchmarkPaused = false
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startOta(uri: Uri) {
        job = scope.launch {
            val gatt = SharedLink.gatt(applicationContext)
            gatt.addOtaListener(otaListener)
            SharedLink.benchmarkPaused = true
            try {
                publish(SlateOtaState(active = true, message = "Validating package"))
                val pkg = NordicDfuPackageReader.read(contentResolver, uri)
                val image = pkg.firmware
                val stamp = NordicDfuPackageReader.faceStamp(image) ?: "unknown stamp"
                val mcuboot = NordicDfuPackageReader.mcubootVersion(image) ?: "?"
                val sha = NordicDfuPackageReader.sha12(image)

                publish(
                    SlateOtaState(
                        active = true,
                        progress = 0,
                        message = "Hashing $stamp MCUBoot $mcuboot (${image.size} B) sha $sha",
                    ),
                )
                val sha256 = MessageDigest.getInstance("SHA-256").digest(image)

                val transferId = (System.currentTimeMillis() and 0xFFFF).toInt()
                val state = slate.ota.OtaSenderState(id = transferId, imageSize = image.size)

                // Send BEGIN and wait for watch's CREDIT + ACK
                publish(SlateOtaState(active = true, progress = 0, message = "Sending BEGIN"))
                val beginMsg = slate.ota.encodeBegin(
                    id = transferId,
                    total = image.size,
                    sha256 = sha256,
                )
                gatt.sendMessage(SdpFrame.CHAN_OTA, beginMsg)

                // Wait for CREDIT and initial ACK from BEGIN response
                awaitBeginAck(state)

                // Transfer loop
                var resyncs = 0
                while (state.acknowledgedOffset < image.size) {
                    while (state.sendable > 0) {
                        val chunkSize = minOf(state.sendable, slate.ota.OTA_MAX_CHUNK_BYTES)
                        val chunk = slate.ota.encodeChunk(
                            id = transferId,
                            offset = state.sentOffset,
                            data = image,
                            fromIndex = state.sentOffset,
                            length = chunkSize,
                        )
                        // I-13: per-chunk log with offsets. When a transfer
                        // stalls on sealed hardware this is the only record of
                        // which offset it died at, to line up against the
                        // watch's OTA diag line (percent / last NAK).
                        LinkLog.i(
                            "OTA chunk off=${state.sentOffset} len=$chunkSize " +
                                "acked=${state.acknowledgedOffset} " +
                                "credit=${state.sendable} of ${image.size}",
                        )
                        gatt.sendMessage(SdpFrame.CHAN_OTA, chunk)
                        state.onChunkSent(chunkSize)
                        // Yield frequently so the write queue drains
                        delay(1)
                    }
                    // Wait for the watch to ACK / send more credit.
                    //
                    // A timeout here does not mean failure (N-19). The watch's
                    // link inbox holds one message, so chunks sent while the
                    // app task is busy are dropped outright — the watch has
                    // nothing to ACK and stays silent, while this side has
                    // already spent the credit for them. Re-sending BEGIN with
                    // the same id is the resume handshake: the watch answers
                    // with its true offset and its true credit, and the
                    // transfer continues from there.
                    val rawMsg = try {
                        withTimeout(CHUNK_TIMEOUT_MS) { watchMessages.receive() }
                    } catch (t: TimeoutCancellationException) {
                        if (++resyncs > MAX_RESYNCS) {
                            error("No response after $MAX_RESYNCS resyncs " +
                                "at ${state.acknowledgedOffset}/${image.size} B")
                        }
                        LinkLog.w(
                            "OTA timeout at acked=${state.acknowledgedOffset} " +
                                "sent=${state.sentOffset} — resync #$resyncs",
                        )
                        publish(
                            SlateOtaState(
                                active = true,
                                progress = (state.acknowledgedOffset * 100L / image.size).toInt(),
                                message = "Re-syncing at ${state.acknowledgedOffset} B",
                            ),
                        )
                        gatt.sendMessage(SdpFrame.CHAN_OTA, beginMsg)
                        continue
                    }
                    val decoded = slate.ota.decodeWatchMessage(rawMsg)
                        ?: continue
                    LinkLog.i("OTA watch msg=$decoded at acked=${state.acknowledgedOffset}")
                    when (val action = state.onWatchMessage(decoded)) {
                        is slate.ota.OtaSendAction.SendChunks -> {
                            val pct = (state.acknowledgedOffset * 100L / image.size).toInt()
                            publish(SlateOtaState(active = true, progress = pct,
                                message = "Transferred ${state.acknowledgedOffset}/${image.size} B"))
                        }
                        is slate.ota.OtaSendAction.Wait -> { /* credit already updated */ }
                        is slate.ota.OtaSendAction.SendCommit -> break
                        is slate.ota.OtaSendAction.Resend -> { /* loop handles it via sentOffset */ }
                        is slate.ota.OtaSendAction.Fail -> error(action.reason)
                    }
                }

                publish(SlateOtaState(active = true, progress = 99, message = "Committing — watch will reboot"))
                gatt.sendMessage(SdpFrame.CHAN_OTA, slate.ota.encodeCommit(transferId))

                // After COMMIT the watch reboots; wait briefly then let link reconnect
                delay(3_000)
                publish(SlateOtaState(
                    active = false,
                    progress = 100,
                    message = "OTA complete. Watch is rebooting; IMAGE_OK will be confirmed on reconnect.",
                ))
            } catch (t: CancellationException) {
                // Propagate normally
                throw t
            } catch (t: Throwable) {
                runCatching {
                    gatt.sendMessage(SdpFrame.CHAN_OTA, slate.ota.encodeAbort())
                }
                fail(t.message ?: t::class.java.simpleName)
            } finally {
                gatt.removeOtaListener(otaListener)
                SharedLink.benchmarkPaused = false
                stopSelf()
            }
        }
    }

    /**
     * Drain watch messages until we have received both a CREDIT and an ACK for
     * the BEGIN, then populate [state] initial credit/offset.
     */
    private suspend fun awaitBeginAck(state: slate.ota.OtaSenderState) {
        var seenCredit: Int? = null
        var seenAckOffset: Int? = null
        withTimeout(BEGIN_TIMEOUT_MS) {
            while (seenCredit == null || seenAckOffset == null) {
                val raw = watchMessages.receive()
                when (val m = slate.ota.decodeWatchMessage(raw)) {
                    is slate.ota.OtaWatchMessage.Credit -> seenCredit = m.credit.creditBytes
                    is slate.ota.OtaWatchMessage.Ack -> seenAckOffset = m.ack.received
                    is slate.ota.OtaWatchMessage.Nak ->
                        error("Watch rejected BEGIN: ${m.nak.reason}")
                    else -> {}
                }
            }
        }
        state.onBeginAcknowledged(
            creditFromWatch = seenCredit ?: slate.ota.OTA_WINDOW_BYTES,
            ackedOffset = seenAckOffset ?: 0,
        )
    }

    private fun fail(message: String) {
        publish(SlateOtaState(message = "OTA failed", error = message))
        stopSelf()
    }

    private fun publish(value: SlateOtaState) {
        stateMutable.value = value
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, notification(value.message, value.progress))
    }

    private fun createChannel() {
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                "Slate firmware update",
                NotificationManager.IMPORTANCE_LOW,
            ),
        )
    }

    private fun notification(message: String, progress: Int): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, SlateOtaActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val cancel = PendingIntent.getService(
            this,
            1,
            Intent(this, SlateOtaService::class.java).apply { action = ACTION_CANCEL },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Updating Slate firmware")
            .setContentText(message)
            .setContentIntent(open)
            .setOngoing(progress in 0..99)
            .setOnlyAlertOnce(true)
            .setProgress(100, progress.coerceIn(0, 100), false)
            .addAction(0, "Cancel", cancel)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "slate_ota"
        private const val NOTIFICATION_ID = 43
        private const val ACTION_START = "slate.app.ota.START"
        private const val ACTION_CANCEL = "slate.app.ota.CANCEL"
        private const val EXTRA_URI = "uri"

        /** Timeout waiting for watch to ACK BEGIN (erase can be slow). */
        private const val BEGIN_TIMEOUT_MS = 30_000L
        /** Timeout waiting for any ACK/CREDIT/NAK during chunk transfer. */
        // Lock-step now (N-19): a chunk is answered as soon as the app task
        // drains it, so a long wait means the chunk was lost, not slow. Keep
        // it well clear of the ~5 s BEGIN erase and a ~200 ms repaint, but
        // short enough that a lost chunk costs seconds rather than a minute.
        private const val CHUNK_TIMEOUT_MS = 5_000L

        /**
         * Resume handshakes tolerated before giving up (N-19). Each one
         * recovers from dropped chunks; a transfer that needs many is telling
         * you something else is wrong.
         */
        private const val MAX_RESYNCS = 20

        private val stateMutable = MutableStateFlow(SlateOtaState())
        val state = stateMutable.asStateFlow()

        fun start(context: Context, uri: Uri): Boolean {
            if (Build.VERSION.SDK_INT >= 31 &&
                ContextCompat.checkSelfPermission(
                    context,
                    Manifest.permission.BLUETOOTH_CONNECT,
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                stateMutable.value = SlateOtaState(
                    message = "OTA failed",
                    error = "Bluetooth permission is missing",
                )
                return false
            }
            if (!SharedLink.gatt(context).metrics.value.connected) {
                stateMutable.value = SlateOtaState(
                    message = "OTA failed",
                    error = "Watch is not connected — connect first",
                )
                return false
            }
            stateMutable.value = SlateOtaState(active = true, message = "Starting OTA")
            val intent = Intent(context, SlateOtaService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_URI, uri.toString())
            }
            return try {
                ContextCompat.startForegroundService(context, intent)
                true
            } catch (t: RuntimeException) {
                stateMutable.value = SlateOtaState(
                    message = "OTA failed",
                    error = "Android blocked the foreground service: ${t.message}",
                )
                false
            }
        }

        fun cancel(context: Context) {
            context.startService(
                Intent(context, SlateOtaService::class.java).apply { action = ACTION_CANCEL },
            )
        }
    }
}
