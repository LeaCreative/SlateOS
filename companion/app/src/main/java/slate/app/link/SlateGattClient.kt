package slate.app.link

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import androidx.core.content.ContextCompat
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import slate.diag.SdpDiag
import slate.frame.SdpFrame
import slate.frame.SdpReassembler
import slate.frame.SdpWriteQueue
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Raw [android.bluetooth.BluetoothGatt] client for Slate.
 * No third-party BLE wrappers — every call is visible here.
 */
@SuppressLint("MissingPermission")
class SlateGattClient(
    private val appContext: Context,
) {
    /**
     * Dedicated looper for the write pump and GATT follow-ups.
     *
     * The pump used to schedule on the main looper. With a 500 ms inter-message
     * gap and a notif flood, that kept Main busy enough that MainActivity could
     * not finish leaving the resumed state (operator saw a frozen UI and could
     * not usefully force-stop — NLS immediately respawns the process).
     * Writes and gaps stay on this thread; UI never waits on them.
     */
    private val bleThread = HandlerThread("slate-gatt").apply { start() }
    private val bleHandler = Handler(bleThread.looper)

    // Owns per-channel TX seq and enqueues each message's fragments atomically
    // (§4.2 single-in-flight — fragments must never interleave across channels).
    private val writeQueue = SdpWriteQueue()
    private val writing = AtomicBoolean(false)

    /** True from connect() until services are discovered or the attempt ends. */
    private var connecting = false

    /** True once discoverServices() has been issued for the current GATT. */
    private var discoveryStarted = false

    val isConnecting: Boolean get() = connecting

    private var pendingDevice: BluetoothDevice? = null
    private var usedAutoConnect = false
    private var leScanner: BluetoothLeScanner? = null
    private var leScanCallback: ScanCallback? = null

    private val scanGiveUp = Runnable {
        if (!connecting || _metrics.value.connected) return@Runnable
        val d = pendingDevice ?: return@Runnable
        LinkLog.w("LE scan timed out — falling back to autoConnect")
        stopScanLocked()
        openGattLocked(d, autoConnect = true)
    }

    private val connectTimeout = Runnable {
        if (!connecting || _metrics.value.connected) return@Runnable
        val d = pendingDevice
        if (!usedAutoConnect && d != null) {
            LinkLog.w("connectGatt(false) timed out — disconnect and autoConnect")
            abortGattLocked(clearConnecting = false)
            connecting = true
            bleHandler.postDelayed({
                if (connecting && !_metrics.value.connected) {
                    openGattLocked(d, autoConnect = true)
                }
            }, GATT_SETTLE_MS)
            return@Runnable
        }
        LinkLog.w("connectGatt timed out — releasing for retry")
        abortGattLocked(clearConnecting = true)
        update { copy(notes = "connect timed out", connected = false) }
    }

    /**
     * Android allows one GATT write at a time. TX/STATUS CCCD writes occupy
     * that slot during bring-up; a HELLO_ACCEPT in the same window returns
     * ERROR_GATT_WRITE_REQUEST_BUSY (201). Queue until both CCCDs land.
     */
    private var notifyReady = false

    /** Message-boundary state for the write pump (N-28). */
    private var lastPktEndedMessage = false
    private var lastPktChannel = -1
    private var lastWriteStartedMs = 0L
    /**
     * Earliest time the next SDP *message* may start.
     *
     * Critical: [sendMessage] also calls [pumpWrites]. Without this, the write
     * callback clears [writing] and a producer still enqueueing (notif flood)
     * starts the next write immediately — bypassing [INTER_MESSAGE_GAP_MS].
     * That is what the operator's log showed: ch=4 writes ~4 ms apart, then
     * launcher DISPLAY lost in the single-slot inbox.
     */
    private var gapUntilMs = 0L
    private var lastNotReadyLogMs = 0L

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var txChar: BluetoothGattCharacteristic? = null
    private var statusChar: BluetoothGattCharacteristic? = null

    private val _metrics = MutableStateFlow(LinkMetrics())
    val metrics: StateFlow<LinkMetrics> = _metrics.asStateFlow()

    private var pendingRttNs: AtomicLong = AtomicLong(-1L)
    private var expectedRttEcho: ByteArray? = null

    private val txReasm = SdpReassembler(diagAllowed = true)
    private val diagListeners = CopyOnWriteArrayList<(ByteArray) -> Unit>()
    private val controlListeners = CopyOnWriteArrayList<(ByteArray) -> Unit>()
    private val inputListeners = CopyOnWriteArrayList<(ByteArray) -> Unit>()
    private val otaListeners = CopyOnWriteArrayList<(ByteArray) -> Unit>()

    fun addDiagListener(listener: (ByteArray) -> Unit) {
        diagListeners += listener
    }

    fun removeDiagListener(listener: (ByteArray) -> Unit) {
        diagListeners -= listener
    }

    fun addControlListener(listener: (ByteArray) -> Unit) {
        controlListeners += listener
    }

    fun removeControlListener(listener: (ByteArray) -> Unit) {
        controlListeners -= listener
    }

    fun addInputListener(listener: (ByteArray) -> Unit) {
        inputListeners += listener
    }

    fun removeInputListener(listener: (ByteArray) -> Unit) {
        inputListeners -= listener
    }

    fun addOtaListener(listener: (ByteArray) -> Unit) {
        otaListeners += listener
    }

    fun removeOtaListener(listener: (ByteArray) -> Unit) {
        otaListeners -= listener
    }

    /**
     * Drop Android's cached GATT table for this device, then discover.
     *
     * The watch keeps one address (derived from FICR) across InfiniTime and
     * every Slate build, while the service tables differ completely, so a
     * cached table routinely describes firmware that is no longer installed —
     * which is how a correct build can look like it has no services at all.
     * `BluetoothGatt.refresh()` is a hidden API, so this is best-effort by
     * design: any failure is logged and discovery proceeds exactly as before.
     */
    private fun discoverServicesFresh(g: BluetoothGatt) {
        discoveryStarted = true
        val refreshed = try {
            val m = g.javaClass.getMethod("refresh")
            val r = m.invoke(g) as? Boolean ?: false
            LinkLog.i("gatt.refresh() = $r")
            r
        } catch (t: Throwable) {
            // Blocked or absent on this OS build — not an error, just means the
            // operator may need to clear the cache manually after a firmware
            // change that alters the service table.
            LinkLog.w("gatt.refresh() unavailable: ${t.javaClass.simpleName}")
            false
        }
        if (!g.discoverServices()) {
            LinkLog.w("discoverServices() returned false")
            update { copy(lastError = "discoverServices refused") }
        } else if (!refreshed) {
            LinkLog.i("discovering with a possibly cached table")
        }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            LinkLog.i("onConnectionStateChange status=$status newState=$newState")
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connecting = false
                update { copy(lastError = "conn status=$status", connected = false) }
                closeInternal()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    bleHandler.removeCallbacks(connectTimeout)
                    bleHandler.removeCallbacks(scanGiveUp)
                    stopScanLocked()
                    connecting = false
                    update {
                        copy(
                            connected = true,
                            deviceAddress = g.device.address,
                            lastError = "",
                            notes = "connected — discovering services",
                        )
                    }
                    // Request MTU before discover so subsequent transfers can use it.
                    val ok = g.requestMtu(247)
                    LinkLog.i("requestMtu(247) submitted=$ok")
                    if (!ok) {
                        discoverServicesFresh(g)
                    } else {
                        // Watchdog: discovery is chained off onMtuChanged, so a
                        // callback that never arrives leaves the link connected
                        // but unusable (N-24). Discover anyway after a grace
                        // period; a second discoverServices() is harmless.
                        bleHandler.postDelayed({
                            // Test whether discovery was *started*, not whether
                            // it finished: rxChar is still null while discovery
                            // is in flight, so the first version of this fired
                            // during a healthy discovery and ran a second one.
                            // That produced two onServicesDiscovered callbacks
                            // and a CCCD write rejected with rc=201.
                            if (gatt === g && !discoveryStarted) {
                                LinkLog.w("no onMtuChanged after ${MTU_GRACE_MS}ms — discovering anyway")
                                discoverServicesFresh(g)
                            }
                        }, MTU_GRACE_MS)
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    connecting = false
                    update { copy(connected = false, notes = "disconnected") }
                    closeInternal()
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            LinkLog.i("onMtuChanged mtu=$mtu status=$status (requested 247 — phone may refuse)")
            update {
                copy(
                    attMtu = if (status == BluetoothGatt.GATT_SUCCESS) mtu else attMtu,
                    notes = "MTU event: $mtu status=$status",
                )
            }
            discoverServicesFresh(g)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            LinkLog.i("onServicesDiscovered status=$status")
            connecting = false
            if (status != BluetoothGatt.GATT_SUCCESS) {
                update {
                    copy(
                        connected = false,
                        lastError = "discover failed status=$status",
                    )
                }
                closeInternal()
                return
            }
            val svc = g.getService(SlateGattIds.SERVICE)
            if (svc == null) {
                update {
                    copy(
                        connected = false,
                        lastError = "Slate service not found",
                    )
                }
                LinkLog.w("Slate service ${SlateGattIds.SERVICE} missing")
                closeInternal()
                return
            }
            rxChar = svc.getCharacteristic(SlateGattIds.RX)
            txChar = svc.getCharacteristic(SlateGattIds.TX)
            statusChar = svc.getCharacteristic(SlateGattIds.STATUS)
            if (rxChar == null || txChar == null) {
                update {
                    copy(
                        connected = false,
                        lastError = "RX/TX characteristics missing",
                    )
                }
                closeInternal()
                return
            }

            // Prefer 2M PHY (API 26+). Log what is actually granted in onPhyUpdate.
            if (Build.VERSION.SDK_INT >= 26) {
                val phyOk = g.setPreferredPhy(
                    BluetoothDevice.PHY_LE_2M_MASK,
                    BluetoothDevice.PHY_LE_2M_MASK,
                    BluetoothDevice.PHY_OPTION_NO_PREFERRED,
                )
                LinkLog.i("setPreferredPhy(2M) called (void); will log onPhyUpdate")
                // Also request connection priority for a shorter interval while active.
                val prio = g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
                LinkLog.i("requestConnectionPriority(HIGH)=$prio")
            }

            // DLE is negotiated by the stack when MTU>23; Android does not expose a
            // direct "request DLE" API on the GATT client. Log MTU as the practical proxy
            // and read PHY after a short delay.
            bleHandler.postDelayed({ readPhy(g) }, 500)

            enableNotify(g, txChar!!, "TX")
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            LinkLog.i("onDescriptorWrite uuid=${descriptor.uuid} status=$status")
            if (descriptor.characteristic.uuid == SlateGattIds.TX &&
                status == BluetoothGatt.GATT_SUCCESS &&
                statusChar != null
            ) {
                enableNotify(g, statusChar!!, "STATUS")
            } else if (descriptor.characteristic.uuid == SlateGattIds.STATUS ||
                (descriptor.characteristic.uuid == SlateGattIds.TX && statusChar == null)
            ) {
                notifyReady = true
                update { copy(notes = "subscribed TX(+STATUS); ready to push") }
                bleHandler.post { pumpWrites() }
            }
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            handleNotify(characteristic.uuid.toString(), value)
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: return
            handleNotify(characteristic.uuid.toString(), value)
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            bleHandler.post {
                if (status != BluetoothGatt.GATT_SUCCESS) {
                    LinkLog.w("onCharacteristicWrite status=$status")
                    update { copy(lastError = "write status=$status") }
                }
                writing.set(false)
                continuePump()
            }
        }

        override fun onPhyUpdate(g: BluetoothGatt, txPhy: Int, rxPhy: Int, status: Int) {
            LinkLog.i("onPhyUpdate tx=$txPhy rx=$rxPhy status=$status")
            update {
                copy(
                    phyTx = phyName(txPhy),
                    phyRx = phyName(rxPhy),
                    notes = "PHY update status=$status tx=$txPhy rx=$rxPhy",
                )
            }
        }

        override fun onPhyRead(g: BluetoothGatt, txPhy: Int, rxPhy: Int, status: Int) {
            LinkLog.i("onPhyRead tx=$txPhy rx=$rxPhy status=$status")
            if (status == BluetoothGatt.GATT_SUCCESS) {
                update { copy(phyTx = phyName(txPhy), phyRx = phyName(rxPhy)) }
            }
        }

        override fun onReadRemoteRssi(g: BluetoothGatt, rssi: Int, status: Int) {
            // unused — interval comes from connection params when available
        }
    }

    /**
     * Start (or continue) a GATT attempt.
     *
     * [force] aborts an in-flight connect so a user tap cannot no-op while
     * `connectGatt(false)` sits with no callback. A live connected session is
     * never torn down here — that is still [close].
     *
     * Direct `getRemoteDevice` + `autoConnect=false` often never callbacks if
     * the watch is not in the controller's recent scan cache. Foreground /
     * tap paths scan by MAC first; boot / watchdog use [autoConnect].
     */
    fun connect(
        device: BluetoothDevice,
        force: Boolean = false,
        autoConnect: Boolean = false,
    ) {
        bleHandler.post { connectLocked(device, force, autoConnect) }
    }

    private fun connectLocked(
        device: BluetoothDevice,
        force: Boolean,
        autoConnect: Boolean,
    ) {
        if (_metrics.value.connected) {
            LinkLog.w("connect(${device.address}) ignored — already connected")
            return
        }
        if (connecting && !force) {
            LinkLog.w("connect(${device.address}) ignored — already connecting")
            return
        }
        if (force && connecting) {
            LinkLog.i("connect force — aborting in-flight GATT for ${device.address}")
            abortGattLocked(clearConnecting = true)
            connecting = true
            pendingDevice = device
            usedAutoConnect = autoConnect
            writeQueue.reset()
            update {
                copy(
                    deviceAddress = device.address,
                    notes = "retrying…",
                    lastError = "",
                )
            }
            bleHandler.postDelayed({
                if (connecting && !_metrics.value.connected) {
                    continueConnectLocked(device, autoConnect)
                }
            }, GATT_SETTLE_MS)
            return
        }
        connecting = true
        pendingDevice = device
        usedAutoConnect = autoConnect
        writeQueue.reset()
        continueConnectLocked(device, autoConnect)
    }

    private fun continueConnectLocked(device: BluetoothDevice, autoConnect: Boolean) {
        update {
            copy(
                deviceAddress = device.address,
                notes = if (autoConnect) "waiting for watch…" else "scanning for watch…",
                lastError = "",
            )
        }
        logAdapterSnapshot(device)
        runCatching { bluetoothAdapter()?.cancelDiscovery() }
        val acl = adapterGattState(device)
        if (acl == BluetoothProfile.STATE_CONNECTED ||
            acl == BluetoothProfile.STATE_CONNECTING
        ) {
            LinkLog.i("ACL already gattState=$acl — connectGatt(false) without scan")
            openGattLocked(device, autoConnect = false)
            return
        }
        if (autoConnect) {
            openGattLocked(device, autoConnect = true)
        } else {
            startScanThenConnectLocked(device)
        }
    }

    private fun startScanThenConnectLocked(device: BluetoothDevice) {
        val scanner = bluetoothAdapter()?.bluetoothLeScanner
        if (scanner == null || !hasScanPermission()) {
            LinkLog.w("LE scan unavailable — connectGatt(false) via getRemoteDevice")
            openGattLocked(device, autoConnect = false)
            return
        }
        stopScanLocked()
        LinkLog.i("LE scan for ${device.address}")
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                bleHandler.post {
                    if (!connecting || _metrics.value.connected) return@post
                    if (!result.device.address.equals(device.address, ignoreCase = true)) {
                        return@post
                    }
                    LinkLog.i("scan hit ${result.device.address} rssi=${result.rssi}")
                    bleHandler.removeCallbacks(scanGiveUp)
                    stopScanLocked()
                    openGattLocked(result.device, autoConnect = false)
                }
            }

            override fun onScanFailed(errorCode: Int) {
                bleHandler.post {
                    LinkLog.w("LE scan failed error=$errorCode — autoConnect")
                    stopScanLocked()
                    if (connecting && !_metrics.value.connected) {
                        openGattLocked(device, autoConnect = true)
                    }
                }
            }
        }
        leScanCallback = cb
        leScanner = scanner
        val filter = ScanFilter.Builder().setDeviceAddress(device.address).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
            .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
            .build()
        try {
            scanner.startScan(listOf(filter), settings, cb)
        } catch (t: Throwable) {
            LinkLog.e("startScan failed", t)
            openGattLocked(device, autoConnect = false)
            return
        }
        bleHandler.removeCallbacks(scanGiveUp)
        bleHandler.postDelayed(scanGiveUp, SCAN_TIMEOUT_MS)
    }

    private fun openGattLocked(device: BluetoothDevice, autoConnect: Boolean) {
        val leftover = gatt
        if (leftover != null) {
            gatt = null
            runCatching { leftover.disconnect() }
            runCatching { leftover.close() }
        }
        usedAutoConnect = autoConnect
        pendingDevice = device
        update {
            copy(
                deviceAddress = device.address,
                notes = if (autoConnect) "waiting for watch (autoConnect)…" else "connecting…",
                lastError = "",
            )
        }
        val g = try {
            device.connectGatt(
                appContext,
                autoConnect,
                callback,
                BluetoothDevice.TRANSPORT_LE,
                BluetoothDevice.PHY_LE_1M_MASK,
                bleHandler,
            )
        } catch (t: Throwable) {
            LinkLog.w("connectGatt(handler) failed: ${t.message}; trying 4-arg")
            device.connectGatt(
                appContext,
                autoConnect,
                callback,
                BluetoothDevice.TRANSPORT_LE,
            )
        }
        gatt = g
        val acl = adapterGattState(device)
        LinkLog.i(
            "connectGatt autoConnect=$autoConnect TRANSPORT_LE " +
                "address=${device.address} returned=${g != null} " +
                "bond=${device.bondState} gattState=$acl",
        )
        if (g == null) {
            connecting = false
            update {
                copy(
                    notes = "connectGatt returned null",
                    lastError = "connectGatt null",
                    connected = false,
                )
            }
            return
        }
        bleHandler.removeCallbacks(connectTimeout)
        val timeoutMs = if (autoConnect) AUTO_CONNECT_TIMEOUT_MS else CONNECT_TIMEOUT_MS
        bleHandler.postDelayed(connectTimeout, timeoutMs)
    }

    private fun abortGattLocked(clearConnecting: Boolean) {
        bleHandler.removeCallbacks(connectTimeout)
        bleHandler.removeCallbacks(scanGiveUp)
        stopScanLocked()
        val old = gatt
        gatt = null
        if (old != null) {
            runCatching { old.disconnect() }
                .onFailure { LinkLog.w("gatt.disconnect: ${it.message}") }
            runCatching { old.close() }
                .onFailure { LinkLog.w("gatt.close: ${it.message}") }
        }
        discoveryStarted = false
        notifyReady = false
        writing.set(false)
        gapUntilMs = 0L
        rxChar = null
        txChar = null
        statusChar = null
        if (clearConnecting) connecting = false
    }

    private fun stopScanLocked() {
        val scanner = leScanner
        val cb = leScanCallback
        leScanner = null
        leScanCallback = null
        if (scanner != null && cb != null) {
            runCatching { scanner.stopScan(cb) }
        }
    }

    private fun bluetoothAdapter() =
        appContext.getSystemService(BluetoothManager::class.java)?.adapter

    private fun adapterGattState(device: BluetoothDevice): Int =
        try {
            appContext.getSystemService(BluetoothManager::class.java)
                ?.getConnectionState(device, BluetoothProfile.GATT)
                ?: -1
        } catch (_: Throwable) {
            -1
        }

    private fun hasScanPermission(): Boolean =
        Build.VERSION.SDK_INT < 31 ||
            ContextCompat.checkSelfPermission(
                appContext,
                Manifest.permission.BLUETOOTH_SCAN,
            ) == PackageManager.PERMISSION_GRANTED

    private fun logAdapterSnapshot(device: BluetoothDevice) {
        val adapter = bluetoothAdapter()
        LinkLog.i(
            "adapter enabled=${adapter?.isEnabled} state=${adapter?.state} " +
                "bond=${device.bondState} gattState=${adapterGattState(device)} " +
                "address=${device.address}",
        )
    }

    fun close() {
        bleHandler.post {
            closeInternal()
            update { LinkMetrics(notes = "idle") }
        }
    }

    private fun closeInternal() {
        abortGattLocked(clearConnecting = true)
        writeQueue.reset()
        pendingDevice = null
    }

    /** Push a complete SDP message on [channel] (DISPLAY=1, DIAG=7, …). */
    fun sendMessage(channel: Int, message: ByteArray): Boolean {
        val g = gatt
        val rx = rxChar
        if (g == null || rx == null || !_metrics.value.connected) {
            // Rate-limited: the compositor and heartbeat retry constantly, and
            // an unready link used to bury everything else in the log.
            val now = System.currentTimeMillis()
            if (now - lastNotReadyLogMs > NOT_READY_LOG_INTERVAL_MS) {
                lastNotReadyLogMs = now
                LinkLog.w(
                    "sendMessage ch=$channel: not ready " +
                        "(gatt=${g != null} rx=${rx != null} " +
                        "connected=${_metrics.value.connected})",
                )
            }
            return false
        }
        val mtu = _metrics.value.attMtu.coerceAtLeast(23)
        val payloadMax = (mtu - 3).coerceAtMost(SdpFrame.ATT_PAYLOAD_MAX)
        if (payloadMax < 20) {
            LinkLog.w("ATT payload too small: $payloadMax")
        }
        val fragments = writeQueue.enqueueMessage(channel, message)
        LinkLog.i("sendMessage ch=$channel bytes=${message.size} fragments=$fragments")
        // Never pump on the caller thread — compositor / NLS / binder must
        // not share a looper with the UI.
        bleHandler.post { pumpWrites() }
        return true
    }

    /** DIAG RTT ping (debug firmware). Prefer [BenchmarkRunner] for gate B. */
    fun pingRtt() {
        val token = SdpDiag.u64Le(System.nanoTime())
        expectedRttEcho = token
        pendingRttNs.set(System.nanoTime())
        sendMessage(SdpFrame.CHAN_DIAG, SdpDiag.rttReq(token))
    }

    fun pushDisplayList(bytes: ByteArray): Boolean {
        val ok = sendMessage(SdpFrame.CHAN_DISPLAY, bytes)
        if (ok) {
            update { copy(lastPushAt = System.currentTimeMillis()) }
        }
        return ok
    }

    /**
     * Recover a stalled pump.
     *
     * `writing` is cleared by onCharacteristicWrite. If that callback never
     * arrives — which it demonstrably doesn't always, for writes without
     * response — the queue stops dead and every later message is silently
     * stranded. That is what left a 53-byte display list enqueued with no
     * corresponding writeCharacteristic.
     */
    private fun scheduleStallCheck() {
        bleHandler.postDelayed({
            if (!writeQueue.isEmpty() && writing.get() &&
                System.currentTimeMillis() - lastWriteStartedMs >= WRITE_STALL_MS
            ) {
                LinkLog.w("write pump stalled — forcing resume")
                writing.set(false)
                // Honour the inter-message gap: a forced resume must not dump
                // the rest of the queue back-to-back into the single-slot inbox.
                continuePump()
            }
        }, WRITE_STALL_MS)
    }

    /** Must run on [bleHandler]. */
    private fun pumpWrites() {
        val now = System.currentTimeMillis()
        if (now < gapUntilMs) {
            bleHandler.postDelayed({ pumpWrites() }, gapUntilMs - now)
            return
        }
        if (!notifyReady) {
            return
        }
        if (!writing.compareAndSet(false, true)) {
            scheduleStallCheck()
            return
        }
        val pkt = writeQueue.poll()
        if (pkt == null) {
            writing.set(false)
            return
        }
        val g = gatt
        val rx = rxChar
        if (g == null || rx == null) {
            writing.set(false)
            return
        }
        lastPktEndedMessage = pkt.endsMessage
        lastPktChannel = pkt.channel
        lastWriteStartedMs = System.currentTimeMillis()
        scheduleStallCheck()
        val ok = writeNoResponse(g, rx, pkt.bytes)
        if (!ok) {
            // Packet was already polled. ERROR_GATT_WRITE_REQUEST_BUSY (201)
            // during CCCD bring-up used to drop HELLO_ACCEPT permanently.
            writeQueue.requeueFront(pkt)
            writing.set(false)
            LinkLog.w(
                "write failed — requeued ch=${pkt.channel} len=${pkt.bytes.size}; " +
                    "backoff ${WRITE_FAIL_BACKOFF_MS}ms",
            )
            gapUntilMs = System.currentTimeMillis() + WRITE_FAIL_BACKOFF_MS
            bleHandler.postDelayed({ pumpWrites() }, WRITE_FAIL_BACKOFF_MS)
        } else if (_metrics.value.lastError.isNotBlank()) {
            update { copy(lastError = "") }
        }
    }

    /**
     * Continue the pump after a completed write, leaving a gap at message
     * boundaries (N-28).
     *
     * The watch's AppInbox holds exactly one message and gates ingest while
     * busy; the app task drains it every 20 ms. Two messages written
     * back-to-back mean the second is dropped, silently, because these are
     * writes without response. `pushToWatch` did exactly that on every screen
     * push — a CONTROL message immediately followed by the display list — so
     * no display list ever reached the renderer.
     *
     * Channel 5 (OTA) is exempt: it has real credit-based flow control, never
     * has more than one message outstanding, and pacing it would cut transfer
     * throughput by more than half.
     *
     * [gapUntilMs] is what stops [sendMessage] → [pumpWrites] from racing past
     * the delayed resume while a producer is still enqueueing.
     */
    private fun continuePump() {
        val gap = lastPktEndedMessage &&
            lastPktChannel != SdpFrame.CHAN_OTA &&
            !writeQueue.containsChannel(SdpFrame.CHAN_OTA) &&
            !writeQueue.isEmpty()
        if (gap) {
            gapUntilMs = System.currentTimeMillis() + INTER_MESSAGE_GAP_MS
            bleHandler.postDelayed({ pumpWrites() }, INTER_MESSAGE_GAP_MS)
        } else {
            pumpWrites()
        }
    }

    private fun writeNoResponse(
        g: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
    ): Boolean {
        return if (Build.VERSION.SDK_INT >= 33) {
            val rc = g.writeCharacteristic(
                characteristic,
                value,
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
            )
            LinkLog.i("writeCharacteristic(NO_RSP) len=${value.size} rc=$rc")
            rc == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION")
            characteristic.value = value
            @Suppress("DEPRECATION")
            val ok = g.writeCharacteristic(characteristic)
            LinkLog.i("writeCharacteristic(NO_RSP legacy) len=${value.size} ok=$ok")
            ok
        }
    }

    private fun enableNotify(g: BluetoothGatt, ch: BluetoothGattCharacteristic, label: String) {
        val set = g.setCharacteristicNotification(ch, true)
        LinkLog.i("setCharacteristicNotification($label)=$set")
        val cccd = ch.getDescriptor(SlateGattIds.CCCD)
        if (cccd == null) {
            LinkLog.w("CCCD missing on $label")
            notifyReady = true
            bleHandler.post { pumpWrites() }
            return
        }
        val enable = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        if (Build.VERSION.SDK_INT >= 33) {
            val rc = g.writeDescriptor(cccd, enable)
            LinkLog.i("writeDescriptor($label CCCD) rc=$rc")
        } else {
            @Suppress("DEPRECATION")
            cccd.value = enable
            @Suppress("DEPRECATION")
            val ok = g.writeDescriptor(cccd)
            LinkLog.i("writeDescriptor($label CCCD legacy)=$ok")
        }
    }

    private fun readPhy(g: BluetoothGatt) {
        if (Build.VERSION.SDK_INT >= 26) {
            g.readPhy()
            LinkLog.i("readPhy() requested")
        }
    }

    private fun handleNotify(uuid: String, value: ByteArray) {
        LinkLog.i("notify $uuid len=${value.size}")
        // STATUS characteristic (time_mid 0003).
        if (uuid.contains("0003", ignoreCase = true) && value.size >= 12) {
            val mtu = (value[0].toInt() and 0xFF) or ((value[1].toInt() and 0xFF) shl 8)
            val iv = (value[8].toInt() and 0xFF) or ((value[9].toInt() and 0xFF) shl 8)
            val intervalMs = if (iv > 0) iv * 1.25 else null
            update {
                copy(
                    attMtu = mtu.takeIf { it > 0 } ?: attMtu,
                    intervalMs = intervalMs,
                    notes = "STATUS mtu=$mtu interval_units=$iv",
                )
            }
            return
        }

        // TX characteristic — framed SDP packets.
        when (txReasm.ingest(value)) {
            SdpReassembler.Status.NeedMore -> return
            SdpReassembler.Status.Dropped,
            SdpReassembler.Status.ChannelReject,
            -> {
                LinkLog.w("TX reassembly drop")
                return
            }
            SdpReassembler.Status.Ok -> {
                val msg = txReasm.message()
                val ch = txReasm.messageChannel()
                when (ch) {
                    SdpFrame.CHAN_CONTROL -> dispatchControl(msg)
                    SdpFrame.CHAN_INPUT -> dispatchInput(msg)
                    SdpFrame.CHAN_DIAG -> dispatchDiag(msg)
                    SdpFrame.CHAN_OTA -> dispatchOta(msg)
                    else -> LinkLog.i("TX ch=$ch len=${msg.size} (no listener)")
                }
            }
        }
    }

    private fun dispatchControl(msg: ByteArray) {
        for (l in controlListeners) {
            try {
                l(msg)
            } catch (t: Throwable) {
                LinkLog.e("control listener", t)
            }
        }
    }

    private fun dispatchInput(msg: ByteArray) {
        for (l in inputListeners) {
            try {
                l(msg)
            } catch (t: Throwable) {
                LinkLog.e("input listener", t)
            }
        }
    }

    private fun dispatchOta(msg: ByteArray) {
        for (l in otaListeners) {
            try {
                l(msg)
            } catch (t: Throwable) {
                LinkLog.e("ota listener", t)
            }
        }
    }

    private fun dispatchDiag(msg: ByteArray) {
        val op = msg.firstOrNull()?.toInt()?.and(0xFF)
        // Legacy / RTT metrics for the main screen single-ping button.
        val echo = expectedRttEcho
        val started = pendingRttNs.get()
        if (echo != null && started > 0 &&
            (op == SdpDiag.OP_RTT_RSP || op == SdpDiag.OP_PING_RSP)
        ) {
            val token = if (msg.size > 1 + echo.size) {
                msg.copyOfRange(1, 1 + echo.size)
            } else if (msg.size >= 1 + echo.size) {
                msg.copyOfRange(1, 1 + echo.size)
            } else {
                ByteArray(0)
            }
            if (token.contentEquals(echo)) {
                val ms = (System.nanoTime() - started) / 1_000_000.0
                pendingRttNs.set(-1L)
                expectedRttEcho = null
                update { copy(rttMs = ms, notes = "RTT ${"%.1f".format(ms)} ms (DIAG)") }
            }
        }
        for (l in diagListeners) {
            try {
                l(msg)
            } catch (t: Throwable) {
                LinkLog.e("diag listener", t)
            }
        }
    }

    private fun phyName(phy: Int): String = when (phy) {
        BluetoothDevice.PHY_LE_1M -> "1M"
        BluetoothDevice.PHY_LE_2M -> "2M"
        BluetoothDevice.PHY_LE_CODED -> "CODED"
        else -> "phy=$phy"
    }

    private fun update(block: LinkMetrics.() -> LinkMetrics) {
        _metrics.value = _metrics.value.block()
    }

    companion object {
        /** How long to wait for onMtuChanged before discovering anyway. */
        private const val MTU_GRACE_MS = 2_000L
        private const val CONNECT_TIMEOUT_MS = 20_000L
        private const val AUTO_CONNECT_TIMEOUT_MS = 90_000L
        private const val SCAN_TIMEOUT_MS = 8_000L
        private const val GATT_SETTLE_MS = 400L

        /** Minimum gap between "not ready" log lines. */
        private const val NOT_READY_LOG_INTERVAL_MS = 5_000L

        /**
         * Gap between consecutive SDP messages, covering the watch's 20 ms
         * app-task drain interval with margin. See [continuePump].
         */
        /**
         * Gap between consecutive SDP messages.
         *
         * 30 ms covered the watch's nominal 20 ms drain, but not reality: a
         * face repaint takes ~230 ms and the worst measured app-loop stall is
         * 929 ms. During those the single-slot inbox stays occupied, so the
         * CONTROL immediately preceding a display list held the slot and the
         * list itself — sent 41 ms later — was discarded. 110 inbox drops
         * against 5 applied lists.
         *
         * 250 ms clears a typical repaint. When we still must send CONTROL
         * before DISPLAY (stack depth > 1), 500 ms covers the measured stalls
         * far more often. The common case now skips that CONTROL entirely
         * (see CompositorHost.sendListNow).
         */
        private const val INTER_MESSAGE_GAP_MS = 500L

        /**
         * After a rejected writeCharacteristic (e.g. CONNECTION_CONGESTED=201
         * while CCCD writes are still in flight), wait before retrying the
         * same packet. Do not skip to the next message — HELLO_ACCEPT must
         * not be discarded.
         */
        private const val WRITE_FAIL_BACKOFF_MS = 150L

        /**
         * Must be longer than [INTER_MESSAGE_GAP_MS]. A shorter stall would
         * fire during an intentional gap and force a back-to-back write.
         */
        private const val WRITE_STALL_MS = 1_500L
    }
}
