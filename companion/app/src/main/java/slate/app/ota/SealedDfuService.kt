package slate.app.ota

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import slate.app.link.AssociationHelper
import slate.app.link.BtAddress
import slate.app.link.LinkForegroundService

data class SealedDfuState(
    val active: Boolean = false,
    val progress: Int = 0,
    val message: String = "Select an InfiniTime/recovery watch and DFU zip.",
    val error: String? = null,
)

/**
 * Foreground owner for the sealed first-hop transfer. A DFU must not depend on
 * an Activity remaining visible while Android/OEM power management is active.
 */
class SealedDfuService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var client: NordicLegacyDfuClient? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
        try {
            ServiceCompat.startForeground(
                this,
                NOTIFICATION_ID,
                notification("Preparing DFU", 0),
                if (Build.VERSION.SDK_INT >= 34) {
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
                } else {
                    0
                },
            )
        } catch (t: SecurityException) {
            fail("Bluetooth permission is required for firmware installation")
            return
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> {
                client?.cancel()
                job?.cancel()
                publish(SealedDfuState(message = "DFU cancelled"))
                stopSelf()
            }
            ACTION_START -> {
                if (job?.isActive == true) return START_NOT_STICKY
                val address = intent.getStringExtra(EXTRA_ADDRESS)
                val uri = intent.getStringExtra(EXTRA_URI)?.let(Uri::parse)
                if (address == null || uri == null) {
                    fail("Missing watch address or DFU zip")
                    return START_NOT_STICKY
                }
                startTransfer(address, uri)
            }
        }
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        client?.cancel()
        client = null
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startTransfer(address: String, uri: Uri) {
        job = scope.launch {
            try {
                publish(SealedDfuState(active = true, message = "Validating DFU package"))
                val pkg = NordicDfuPackageReader.read(contentResolver, uri)
                val adapter = getSystemService(BluetoothManager::class.java)?.adapter
                    ?: error("Bluetooth adapter unavailable")
                check(adapter.isEnabled) { "Bluetooth is disabled" }
                val normalized = BtAddress.normalize(address)
                    ?: error("Malformed watch address '$address'")
                val device = adapter.getRemoteDevice(normalized)
                val dfu = NordicLegacyDfuClient(applicationContext)
                client = dfu
                dfu.flash(device, pkg) { percent, message ->
                    publish(
                        SealedDfuState(
                            active = true,
                            progress = percent,
                            message = message,
                        ),
                    )
                }
                publish(
                    SealedDfuState(
                        active = false,
                        progress = 100,
                        message = "DFU complete. Wait for Slate to boot; link presence is enabled.",
                    ),
                )
                AssociationHelper(applicationContext).startObservingPresence(address)
            } catch (t: Throwable) {
                fail(t.message ?: t::class.java.simpleName)
            } finally {
                client = null
                stopSelf()
            }
        }
    }

    private fun fail(message: String) {
        publish(SealedDfuState(message = "DFU failed", error = message))
        stopSelf()
    }

    private fun publish(value: SealedDfuState) {
        stateMutable.value = value
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, notification(value.message, value.progress))
    }

    private fun createChannel() {
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(
                CHANNEL_ID,
                "Slate firmware installation",
                NotificationManager.IMPORTANCE_LOW,
            ),
        )
    }

    private fun notification(message: String, progress: Int): Notification {
        val open = PendingIntent.getActivity(
            this,
            0,
            Intent(this, SealedDfuProbeActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val cancel = PendingIntent.getService(
            this,
            1,
            Intent(this, SealedDfuService::class.java).apply { action = ACTION_CANCEL },
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("Installing Slate firmware")
            .setContentText(message)
            .setContentIntent(open)
            .setOngoing(progress in 0..99)
            .setOnlyAlertOnce(true)
            .setProgress(100, progress.coerceIn(0, 100), false)
            .addAction(0, "Cancel", cancel)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "slate_sealed_dfu"
        private const val NOTIFICATION_ID = 42
        private const val ACTION_START = "slate.app.dfu.START"
        private const val ACTION_CANCEL = "slate.app.dfu.CANCEL"
        private const val EXTRA_ADDRESS = "address"
        private const val EXTRA_URI = "uri"

        private val stateMutable = MutableStateFlow(SealedDfuState())
        val state = stateMutable.asStateFlow()

        fun start(context: Context, address: String, uri: Uri): Boolean {
            if (Build.VERSION.SDK_INT >= 31 &&
                ContextCompat.checkSelfPermission(
                    context,
                    Manifest.permission.BLUETOOTH_CONNECT,
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                stateMutable.value = SealedDfuState(
                    message = "DFU failed",
                    error = "Bluetooth permission is missing",
                )
                return false
            }
            stateMutable.value = SealedDfuState(active = true, message = "Starting DFU")
            if (LinkForegroundService.instance != null) {
                context.startService(
                    Intent(context, LinkForegroundService::class.java).apply {
                        action = LinkForegroundService.ACTION_DISCONNECT
                    },
                )
            }
            val intent = Intent(context, SealedDfuService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_ADDRESS, address)
                putExtra(EXTRA_URI, uri.toString())
            }
            return try {
                ContextCompat.startForegroundService(context, intent)
                true
            } catch (t: RuntimeException) {
                stateMutable.value = SealedDfuState(
                    message = "DFU failed",
                    error = "Android blocked the foreground service: ${t.message}",
                )
                false
            }
        }

        fun cancel(context: Context) {
            context.startService(
                Intent(context, SealedDfuService::class.java).apply {
                    action = ACTION_CANCEL
                },
            )
        }
    }
}
