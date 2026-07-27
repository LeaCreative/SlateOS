package slate.app.link

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import slate.app.MainActivity
import slate.dsl.displayList
import slate.wire.Align
import slate.wire.Colors
import slate.wire.Font
import slate.wire.Style
import slate.wire.pal
import slate.wire.rgb
import java.time.LocalTime
import java.time.format.DateTimeFormatter

/**
 * Foreground service (`connectedDevice`) that owns the GATT connection and
 * pushes a clock display list once per second while connected.
 */
class LinkForegroundService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var clockJob: Job? = null
    private var rttJob: Job? = null
    private var reconnectJob: Job? = null

    private lateinit var client: SlateGattClient

    override fun onCreate() {
        super.onCreate()
        instance = this
        client = SharedLink.gatt(applicationContext)
        createChannel()
        val notification = buildNotification("Slate link starting…")
        if (Build.VERSION.SDK_INT >= 34) {
            ServiceCompat.startForeground(
                this,
                NOTIF_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        } else {
            @Suppress("DEPRECATION")
            startForeground(NOTIF_ID, notification)
        }
        LinkLog.i("LinkForegroundService onCreate")
        scope.launch {
            client.metrics.collect { m ->
                val text = if (m.connected) {
                    "Connected ${m.deviceAddress} MTU=${m.attMtu} PHY=${m.phyTx}"
                } else {
                    "Idle — ${m.notes.ifBlank { "not connected" }}"
                }
                getSystemService(NotificationManager::class.java)
                    .notify(NOTIF_ID, buildNotification(text))
                if (m.connected) startClock() else stopClock()
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                val addr = intent.getStringExtra(EXTRA_ADDRESS)
                if (addr != null) connectAddress(addr)
            }
            ACTION_DISCONNECT -> {
                client.close()
                stopReconnect()
            }
            ACTION_PRESENCE_APPEARED -> {
                val addr = intent.getStringExtra(EXTRA_ADDRESS) ?: SharedLink.associatedAddress
                if (addr != null) {
                    LinkLog.i("presence appeared — reconnect $addr")
                    connectAddress(addr)
                }
            }
            ACTION_PRESENCE_DISAPPEARED -> {
                LinkLog.i("presence disappeared")
                client.close()
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopClock()
        stopReconnect()
        client.close()
        scope.cancel()
        instance = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun connectAddress(address: String) {
        SharedLink.associatedAddress = address
        val adapter = getSystemService(BluetoothManager::class.java)?.adapter
        if (adapter == null || !adapter.isEnabled) {
            LinkLog.w("Bluetooth adapter unavailable")
            return
        }
        val device: BluetoothDevice = try {
            adapter.getRemoteDevice(address)
        } catch (t: Throwable) {
            LinkLog.e("getRemoteDevice failed", t)
            return
        }
        client.connect(device)
        scheduleReconnectWatchdog(address)
    }

    private fun scheduleReconnectWatchdog(address: String) {
        stopReconnect()
        reconnectJob = scope.launch {
            while (isActive) {
                delay(5_000)
                if (!client.metrics.value.connected) {
                    LinkLog.i("reconnect watchdog — retry $address")
                    val adapter = getSystemService(BluetoothManager::class.java)?.adapter
                    val device = adapter?.getRemoteDevice(address) ?: continue
                    client.connect(device)
                }
            }
        }
    }

    private fun stopReconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
    }

    private fun startClock() {
        if (clockJob?.isActive == true) return
        clockJob = scope.launch {
            val fmt = DateTimeFormatter.ofPattern("HH:mm")
            while (isActive) {
                if (client.metrics.value.connected) {
                    val list = buildClockList(LocalTime.now().format(fmt))
                    client.pushDisplayList(list)
                }
                delay(1_000)
            }
        }
        rttJob = scope.launch {
            while (isActive) {
                delay(5_000)
                if (client.metrics.value.connected) {
                    client.pingRtt()
                }
            }
        }
    }

    private fun stopClock() {
        clockJob?.cancel()
        clockJob = null
        rttJob?.cancel()
        rttJob = null
    }

    private fun buildClockList(timeText: String): ByteArray = displayList {
        palette(0, Colors.BLACK)
        palette(1, Colors.WHITE)
        clear(pal(0))
        text(
            font = Font.LARGE,
            x = 120,
            y = 100,
            align = Align.CENTER,
            color = pal(1),
            text = timeText.take(5),
        )
        element(id = 1, x = 20, y = 180, w = 200, h = 40) {
            rectRound(20, 180, 200, 40, r = 8, color = rgb(0x4208), style = Style.FILL)
            text(
                font = Font.LARGE,
                x = 120,
                y = 194,
                align = Align.CENTER,
                color = pal(1),
                text = "OK",
            )
        }
        commit()
    }

    private fun createChannel() {
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Slate link", NotificationManager.IMPORTANCE_LOW),
        )
    }

    private fun buildNotification(content: String): Notification {
        val pi = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Slate")
            .setContentText(content)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    companion object {
        const val CHANNEL_ID = "slate_link"
        const val NOTIF_ID = 41
        const val ACTION_CONNECT = "slate.app.CONNECT"
        const val ACTION_DISCONNECT = "slate.app.DISCONNECT"
        const val ACTION_PRESENCE_APPEARED = "slate.app.PRESENCE_APPEARED"
        const val ACTION_PRESENCE_DISAPPEARED = "slate.app.PRESENCE_DISAPPEARED"
        const val EXTRA_ADDRESS = "address"

        @Volatile
        var instance: LinkForegroundService? = null
            private set

        fun start(context: Context, address: String? = null) {
            val i = Intent(context, LinkForegroundService::class.java)
            if (address != null) {
                i.action = ACTION_CONNECT
                i.putExtra(EXTRA_ADDRESS, address)
            }
            context.startForegroundService(i)
        }
    }
}

/** Process-wide GATT client + last associated MAC. */
object SharedLink {
    @Volatile
    var associatedAddress: String? = null

    @Volatile
    private var clientInstance: SlateGattClient? = null

    fun gatt(context: Context): SlateGattClient {
        val existing = clientInstance
        if (existing != null) return existing
        return synchronized(this) {
            clientInstance ?: SlateGattClient(context.applicationContext).also {
                clientInstance = it
            }
        }
    }
}
