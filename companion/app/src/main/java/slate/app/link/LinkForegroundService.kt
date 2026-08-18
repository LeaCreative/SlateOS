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
import kotlinx.coroutines.CoroutineExceptionHandler
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

    /**
     * Last line of defence for the BLE link.
     *
     * `SupervisorJob` stops one failed child cancelling its siblings, but it
     * does nothing about an **uncaught** exception: that goes to the thread's
     * default handler, which on Android means the process dies. Every sub-app
     * launch, adapter callback and compositor push runs in this scope, so
     * without a handler here any one of them can take the link down — and one
     * did, repeatedly (the JS sandbox rebind, 6-7 Aug).
     *
     * Individual call sites still catch what they can act on. This exists so
     * that the ones nobody anticipated cost a log line instead of the link.
     */
    private val crashGuard = CoroutineExceptionHandler { _, t ->
        LinkLog.e("uncaught in link service scope — link kept alive", t)
    }

    /**
     * Link/compositor work must not share the UI looper.
     *
     * Historically this was [Dispatchers.Main.immediate], so every heartbeat,
     * notif upsert, display push and JS seed ran on Main — which is what made
     * MainActivity appear frozen under link load and blocked lifecycle
     * timeouts. A single worker thread keeps session/compositor ordering
     * while leaving the UI free.
     */
    private val scope =
        CoroutineScope(
            SupervisorJob() +
                Dispatchers.Default.limitedParallelism(1) +
                crashGuard,
        )
    private var rttJob: Job? = null
    private var heartbeatJob: Job? = null
    private var reconnectJob: Job? = null

    private lateinit var client: SlateGattClient
    private var compositorHost: CompositorHost? = null
    private var repoScheduler: RepoUpdateScheduler? = null

    /** Kept so [refreshForegroundType] can re-post the notification unchanged. */
    private var lastNotificationText: String = "Slate link starting…"

    override fun onCreate() {
        super.onCreate()
        instance = this
        client = SharedLink.gatt(applicationContext)
        compositorHost = CompositorHost(
            applicationContext,
            client,
            scope,
        ).also { it.start() }
        repoScheduler = RepoUpdateScheduler(applicationContext, scope).also { it.start() }
        createChannel()
        val notification = buildNotification("Slate link starting…")
        try {
            enterForeground(notification)
        } catch (t: SecurityException) {
            LinkLog.e("Unable to enter connectedDevice foreground", t)
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
                lastNotificationText = text
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

    /**
     * Foreground-service types this service may claim **right now**.
     *
     * `location` is added only when it is legal **in this process state**.
     * FINE/COARSE are foreground-only; claiming the location FGS type from a
     * boot / package-replace receiver throws and used to `stopSelf()` the
     * whole link. BLE uses `connectedDevice` alone until we are eligible.
     */
    private fun foregroundTypes(): Int {
        var types = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        if (mayClaimLocationType()) {
            types = types or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION
        }
        return types
    }

    private fun mayClaimLocationType(): Boolean {
        if (!hasLocationPermission(this)) return false
        if (hasBackgroundLocation(this)) return true
        return isProcessForeground()
    }

    private fun isProcessForeground(): Boolean {
        val am = getSystemService(android.app.ActivityManager::class.java) ?: return false
        val pkg = packageName
        return am.runningAppProcesses.orEmpty().any {
            it.processName == pkg &&
                it.importance <= android.app.ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND
        }
    }

    private fun enterForeground(notification: Notification) {
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            startForeground(NOTIF_ID, notification)
            return
        }
        try {
            ServiceCompat.startForeground(this, NOTIF_ID, notification, foregroundTypes())
        } catch (t: SecurityException) {
            LinkLog.w(
                "FGS types refused (${t.message}); retrying connectedDevice-only",
            )
            ServiceCompat.startForeground(
                this,
                NOTIF_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        }
    }

    /**
     * Re-assert the foreground type after a permission change.
     *
     * Safe to call at any time: on failure the service keeps whatever type it
     * already had rather than stopping, because losing the link to gain a
     * location type would be a bad trade.
     */
    private fun refreshForegroundType() {
        if (Build.VERSION.SDK_INT < 34) return
        val types = foregroundTypes()
        try {
            ServiceCompat.startForeground(
                this,
                NOTIF_ID,
                buildNotification(lastNotificationText),
                types,
            )
            LinkLog.i("FGS type refreshed: location=${hasLocationPermission(this)}")
        } catch (t: SecurityException) {
            LinkLog.e("FGS type refresh refused; keeping previous type", t)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) {
            AssociationHelper(applicationContext).lastAssociatedAddress()?.let {
                LinkLog.i("sticky service restart — reconnect $it")
                connectAddress(it, force = false, autoConnect = true)
            }
            return START_STICKY
        }
        when (intent?.action) {
            ACTION_CONNECT -> {
                val addr = intent.getStringExtra(EXTRA_ADDRESS)
                val force = intent.getBooleanExtra(EXTRA_FORCE, false)
                val autoConnect = intent.getBooleanExtra(EXTRA_AUTO_CONNECT, false)
                if (addr != null) connectAddress(addr, force, autoConnect)
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
                        connectAddress(addr, force = true, autoConnect = false)
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
                // CDM "disappeared" means the watch is not in CDM's scan, not
                // that GATT dropped. A connected peripheral often stops
                // advertising, so this fires in the middle of a live session —
                // SDP OTA included. Closing here is what made OTA crawl: drop,
                // full rediscovery, 32 notif upserts, repeat.
                if (client.metrics.value.connected) {
                    LinkLog.i(
                        "presence disappeared ${gone ?: ""} while GATT up — keeping link",
                    )
                    return START_STICKY
                }
                // Watch is out of CDM's scan and GATT is down. Keep the FGS so
                // the reconnect watchdog can connectGatt when it is back —
                // stopSelf here is why the operator had to tap Reconnect.
                val addr = active
                    ?: AssociationHelper(applicationContext).lastAssociatedAddress()
                    ?: gone
                LinkLog.i(
                    "presence disappeared ${gone ?: ""} — keeping service to reconnect",
                )
                if (addr != null) scheduleReconnectWatchdog(addr)
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
            ACTION_REFRESH_FGS_TYPE -> refreshForegroundType()
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

    private fun connectAddress(
        rawAddress: String,
        force: Boolean = false,
        autoConnect: Boolean = false,
    ) {
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
        client.connect(device, force = force, autoConnect = autoConnect)
        scheduleReconnectWatchdog(address)
    }

    private fun scheduleReconnectWatchdog(address: String) {
        stopReconnect()
        reconnectJob = scope.launch {
            var delayMs = 5_000L
            while (isActive) {
                delay(delayMs)
                if (!client.metrics.value.connected) {
                    if (client.isConnecting) {
                        LinkLog.i("reconnect watchdog — connect still in flight, not aborting")
                        continue
                    }
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
                    client.connect(device, force = false, autoConnect = true)
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
        // nothing of the kind. Use Debug → Benchmarks (gate B) for RTT.
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
        const val ACTION_REFRESH_FGS_TYPE = "slate.app.REFRESH_FGS_TYPE"
        const val EXTRA_ADDRESS = "address"
        const val EXTRA_FORCE = "force"
        const val EXTRA_AUTO_CONNECT = "autoConnect"

        @Volatile
        var instance: LinkForegroundService? = null
            private set

        private const val HEARTBEAT_MS = 2_000L

        /**
         * Either location grant counts. The user can allow approximate while
         * refusing precise, and a watch face showing a town name is perfectly
         * served by the coarse one — treating that as "denied" would refuse a
         * capability the user actually granted.
         */
        fun hasLocationPermission(context: Context): Boolean =
            ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.ACCESS_COARSE_LOCATION,
            ) == PackageManager.PERMISSION_GRANTED ||
                ContextCompat.checkSelfPermission(
                    context,
                    Manifest.permission.ACCESS_FINE_LOCATION,
                ) == PackageManager.PERMISSION_GRANTED

        fun hasBackgroundLocation(context: Context): Boolean =
            Build.VERSION.SDK_INT < 29 ||
                ContextCompat.checkSelfPermission(
                    context,
                    Manifest.permission.ACCESS_BACKGROUND_LOCATION,
                ) == PackageManager.PERMISSION_GRANTED

        /**
         * Tell a running service to re-evaluate its foreground type.
         *
         * Call after the user grants or revokes location. Does nothing when the
         * service is not running — it will compute the right type on its next
         * start anyway.
         */
        fun refreshForegroundServiceType(context: Context) {
            if (instance == null) return
            val i = Intent(context, LinkForegroundService::class.java)
            i.action = ACTION_REFRESH_FGS_TYPE
            runCatching { context.startForegroundService(i) }
                .onFailure { LinkLog.e("Unable to refresh FGS type", it) }
        }

        fun start(
            context: Context,
            address: String? = null,
            force: Boolean = false,
            autoConnect: Boolean = false,
        ): Boolean {
            if (!hasBluetoothPermission(context)) {
                LinkLog.w("Not starting link service: Bluetooth permission missing")
                return false
            }
            val i = Intent(context, LinkForegroundService::class.java)
            if (address != null) {
                i.action = ACTION_CONNECT
                i.putExtra(EXTRA_ADDRESS, address)
                i.putExtra(EXTRA_FORCE, force)
                i.putExtra(EXTRA_AUTO_CONNECT, autoConnect)
            }
            return try {
                context.startForegroundService(i)
                true
            } catch (t: RuntimeException) {
                LinkLog.e("Unable to start link foreground service", t)
                false
            }
        }

        /**
         * Start (or keep) the link FGS for the last associated watch.
         * Used from Main, boot, and package-replace so reconnect does not
         * depend on tapping the button or on CDM firing appeared.
         */
        fun startForRememberedWatch(
            context: Context,
            force: Boolean = false,
            autoConnect: Boolean = false,
        ): Boolean {
            val app = context.applicationContext
            val association = AssociationHelper(app)
            val addr = association.lastAssociatedAddress()
                ?: association.associatedAddresses().firstOrNull()
                ?: return false
            association.startObservingPresence(addr)
            return start(app, addr, force = force, autoConnect = autoConnect)
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
