package slate.app.link

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
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
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import slate.app.MainActivity
import slate.app.host.CompositorHost
import slate.app.repo.RepoUpdateScheduler
import slate.frame.SdpFrame
import slate.session.ConfirmStatus

/**
 * Foreground service (`connectedDevice`) that owns the GATT connection and
 * drives the M8 compositor (ambient clock + app host).
 */
class LinkForegroundService : Service() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var rttJob: Job? = null
    private var heartbeatJob: Job? = null
    private var reconnectJob: Job? = null

    private lateinit var client: SlateGattClient
    private var compositorHost: CompositorHost? = null
    private var repoScheduler: RepoUpdateScheduler? = null

    override fun onCreate() {
        super.onCreate()
        instance = this
        client = SharedLink.gatt(applicationContext)
        compositorHost = CompositorHost(
            applicationContext,
            client,
            scope,
            slate.app.notif.NotifPrefs(applicationContext),
        ).also { it.start() }
        repoScheduler = RepoUpdateScheduler(applicationContext, scope).also { it.start() }
        createChannel()
        val notification = buildNotification("Slate link starting…")
        try {
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
        } catch (t: SecurityException) {
            LinkLog.e("Missing Bluetooth permission for connectedDevice FGS", t)
            stopSelf()
            return
        }
        LinkLog.i("LinkForegroundService onCreate (compositor host)")
        scope.launch {
            client.metrics.collect { m ->
                val text = if (m.connected) {
                    "Connected ${m.deviceAddress} MTU=${m.attMtu} PHY=${m.phyTx}"
                } else {
                    "Idle — ${m.notes.ifBlank { "not connected" }}"
                }
                getSystemService(NotificationManager::class.java)
                    .notify(NOTIF_ID, buildNotification(text))
                if (m.connected) {
                    startRtt()
                    startHeartbeat()
                } else {
                    stopRtt()
                    stopHeartbeat()
                }
            }
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) {
            AssociationHelper(applicationContext).lastAssociatedAddress()?.let {
                LinkLog.i("sticky service restart — reconnect $it")
                connectAddress(it)
            }
            return START_STICKY
        }
        when (intent?.action) {
            ACTION_CONNECT -> {
                val addr = intent.getStringExtra(EXTRA_ADDRESS)
                if (addr != null) connectAddress(addr)
            }
            ACTION_DISCONNECT -> {
                compositorHost?.sendGoodbye()
                stopReconnect()
                client.close()
                stopSelf()
            }
            ACTION_PRESENCE_APPEARED -> {
                val addr = intent.getStringExtra(EXTRA_ADDRESS) ?: SharedLink.associatedAddress
                if (addr != null) {
                    val preferred =
                        AssociationHelper(applicationContext).lastAssociatedAddress()
                    if (preferred == null || preferred.equals(addr, ignoreCase = true)) {
                        LinkLog.i("presence appeared — reconnect $addr")
                        connectAddress(addr)
                    } else {
                        LinkLog.i("ignoring presence for non-selected association $addr")
                    }
                }
            }
            ACTION_PRESENCE_DISAPPEARED -> {
                val gone = intent.getStringExtra(EXTRA_ADDRESS)
                val active = SharedLink.associatedAddress
                if (gone != null && active != null &&
                    !gone.equals(active, ignoreCase = true)
                ) {
                    LinkLog.i("ignoring disappearance for non-selected association $gone")
                    return START_STICKY
                }
                LinkLog.i("presence disappeared ${gone ?: ""}")
                stopReconnect()
                client.close()
                stopSelf()
            }
            ACTION_OPEN_TEST_APP -> {
                scope.launch {
                    compositorHost?.openTestApp()
                }
            }
            ACTION_OPEN_NOTIFICATIONS -> {
                scope.launch {
                    compositorHost?.openNotifications()
                }
            }
            ACTION_OPEN_TIMER -> {
                scope.launch {
                    compositorHost?.openTimer()
                }
            }
            ACTION_OPEN_NAVIGATION -> {
                scope.launch {
                    compositorHost?.openNavigation()
                }
            }
            ACTION_OPEN_CAMERA -> {
                scope.launch {
                    compositorHost?.openCamera()
                }
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopRtt()
        stopHeartbeat()
        stopReconnect()
        repoScheduler?.stop()
        repoScheduler = null
        compositorHost?.sendGoodbye()
        compositorHost?.stop()
        client.close()
        scope.cancel()
        instance = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    fun watchProtocolVersion(): Int =
        compositorHost?.compositor?.watchProtocolVersion ?: 1

    private fun connectAddress(rawAddress: String) {
        // Presence callbacks and intent extras can carry CDM's lowercase form,
        // which getRemoteDevice() rejects outright.
        val address = BtAddress.normalize(rawAddress)
        if (address == null) {
            LinkLog.w("refusing to connect to malformed address '$rawAddress'")
            return
        }
        val held = LinkContention.remediationIfHeldByOther(
            applicationContext,
            address,
            weAreConnected = client.metrics.value.connected,
        )
        if (held != null) {
            LinkLog.w(held)
            SharedLink.lastContentionMessage = held
        }
        AssociationHelper(applicationContext).rememberAddress(address)
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
            var delayMs = 5_000L
            while (isActive) {
                delay(delayMs)
                if (!client.metrics.value.connected) {
                    LinkLog.i("reconnect watchdog — retry $address after ${delayMs}ms")
                    val bonded = LinkContention.isBonded(applicationContext, address)
                    // After the first failed retry, treat bonded+no-link as a
                    // foreign-central hint (I-16); first pass stays GATT-only.
                    val verdict = LinkContention.checkWithSignals(
                        applicationContext,
                        address,
                        weAreConnected = false,
                        advertisingSeen = null,
                        connectFailedWhileBonded = bonded && delayMs > 5_000L,
                    )
                    if (verdict.blocked) {
                        LinkLog.w(LinkContention.formatMessage(applicationContext, verdict))
                    }
                    val adapter = getSystemService(BluetoothManager::class.java)?.adapter
                    val device = adapter?.getRemoteDevice(address) ?: continue
                    client.connect(device)
                    delayMs = (delayMs * 2).coerceAtMost(60_000L)
                } else {
                    SharedLink.lastContentionMessage = null
                    delayMs = 5_000L
                }
            }
        }
    }

    private fun stopReconnect() {
        reconnectJob?.cancel()
        reconnectJob = null
    }

    private fun startRtt() {
        // Disabled by default. Production firmware builds with
        // SLATE_BLE_DIAG=0, so every one of these was rejected by the watch's
        // reassembler and counted as a frame drop — ~750 pings produced the
        // 538 "frame drops" that looked like a transport fault and were
        // nothing of the kind. Use the "Ping RTT" button for a one-off probe.
        if (!RTT_PING_ENABLED) return
        if (rttJob?.isActive == true) return
        rttJob = scope.launch {
            while (isActive) {
                delay(5_000)
                if (client.metrics.value.connected && !SharedLink.benchmarkPaused) {
                    client.pingRtt()
                }
            }
        }
    }

    private fun stopRtt() {
        rttJob?.cancel()
        rttJob = null
    }

    private fun startHeartbeat() {
        if (heartbeatJob?.isActive == true) return
        heartbeatJob = scope.launch {
            while (isActive) {
                delay(HEARTBEAT_MS)
                if (!client.metrics.value.connected || SharedLink.benchmarkPaused) continue
                val hb = compositorHost?.sessionHeartbeat() ?: continue
                client.sendMessage(SdpFrame.CHAN_CONTROL, hb)
            }
        }
    }

    private fun stopHeartbeat() {
        heartbeatJob?.cancel()
        heartbeatJob = null
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
        /** Periodic DIAG ping. Off: the watch rejects DIAG unless built with
         *  SLATE_BLE_DIAG=1, and each rejection counts as a frame drop. */
        private const val RTT_PING_ENABLED = false

        const val CHANNEL_ID = "slate_link"
        const val NOTIF_ID = 41
        const val ACTION_CONNECT = "slate.app.CONNECT"
        const val ACTION_DISCONNECT = "slate.app.DISCONNECT"
        const val ACTION_PRESENCE_APPEARED = "slate.app.PRESENCE_APPEARED"
        const val ACTION_PRESENCE_DISAPPEARED = "slate.app.PRESENCE_DISAPPEARED"
        const val ACTION_OPEN_TEST_APP = "slate.app.OPEN_TEST_APP"
        const val ACTION_OPEN_NOTIFICATIONS = "slate.app.OPEN_NOTIFICATIONS"
        const val ACTION_OPEN_TIMER = "slate.app.OPEN_TIMER"
        const val ACTION_OPEN_NAVIGATION = "slate.app.OPEN_NAVIGATION"
        const val ACTION_OPEN_CAMERA = "slate.app.OPEN_CAMERA"
        const val EXTRA_ADDRESS = "address"

        @Volatile
        var instance: LinkForegroundService? = null
            private set

        private const val HEARTBEAT_MS = 2_000L

        fun start(context: Context, address: String? = null): Boolean {
            if (!hasBluetoothPermission(context)) {
                LinkLog.w("Not starting link service: Bluetooth permission missing")
                return false
            }
            val i = Intent(context, LinkForegroundService::class.java)
            if (address != null) {
                i.action = ACTION_CONNECT
                i.putExtra(EXTRA_ADDRESS, address)
            }
            return try {
                context.startForegroundService(i)
                true
            } catch (t: RuntimeException) {
                LinkLog.e("Unable to start link foreground service", t)
                false
            }
        }

        private fun hasBluetoothPermission(context: Context): Boolean =
            Build.VERSION.SDK_INT < 31 ||
                ContextCompat.checkSelfPermission(
                    context,
                    Manifest.permission.BLUETOOTH_CONNECT,
                ) == PackageManager.PERMISSION_GRANTED

        fun openTestApp(context: Context) {
            context.startService(
                Intent(context, LinkForegroundService::class.java).apply {
                    action = ACTION_OPEN_TEST_APP
                },
            )
        }

        fun openNotifications(context: Context) {
            context.startService(
                Intent(context, LinkForegroundService::class.java).apply {
                    action = ACTION_OPEN_NOTIFICATIONS
                },
            )
        }

        fun openTimer(context: Context) {
            context.startService(
                Intent(context, LinkForegroundService::class.java).apply {
                    action = ACTION_OPEN_TIMER
                },
            )
        }

        fun openNavigation(context: Context) {
            context.startService(
                Intent(context, LinkForegroundService::class.java).apply {
                    action = ACTION_OPEN_NAVIGATION
                },
            )
        }

        fun openCamera(context: Context) {
            context.startService(
                Intent(context, LinkForegroundService::class.java).apply {
                    action = ACTION_OPEN_CAMERA
                },
            )
        }
    }
}

/** Process-wide GATT client + last associated MAC. */
object SharedLink {
    @Volatile
    var associatedAddress: String? = null

    /** When true, compositor / RTT skip radio traffic. */
    @Volatile
    var benchmarkPaused: Boolean = false

    /** Last CONTROL 0xE1 snapshot from the watch (trial / IMAGE_OK). */
    @Volatile
    var lastConfirmStatus: ConfirmStatus.Snapshot? = null

    /** Set when another app appears to own the GATT slot. */
    @Volatile
    var lastContentionMessage: String? = null

    private val _confirmUi = MutableStateFlow<CompositorHost.ConfirmUi>(CompositorHost.ConfirmUi.Idle)
    val confirmUi: StateFlow<CompositorHost.ConfirmUi> = _confirmUi.asStateFlow()

    fun publishConfirmUi(ui: CompositorHost.ConfirmUi) {
        _confirmUi.value = ui
    }

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
