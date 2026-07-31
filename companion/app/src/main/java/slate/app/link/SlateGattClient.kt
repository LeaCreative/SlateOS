package slate.app.link

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
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
    private val mainHandler = Handler(Looper.getMainLooper())

    // Owns per-channel TX seq and enqueues each message's fragments atomically
    // (§4.2 single-in-flight — fragments must never interleave across channels).
    private val writeQueue = SdpWriteQueue()
    private val writing = AtomicBoolean(false)

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

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            LinkLog.i("onConnectionStateChange status=$status newState=$newState")
            if (status != BluetoothGatt.GATT_SUCCESS) {
                update { copy(lastError = "conn status=$status", connected = false) }
                closeInternal()
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
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
                        g.discoverServices()
                    }
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
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
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            LinkLog.i("onServicesDiscovered status=$status")
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
            mainHandler.postDelayed({ readPhy(g) }, 500)

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
            } else if (descriptor.characteristic.uuid == SlateGattIds.STATUS) {
                update { copy(notes = "subscribed TX(+STATUS); ready to push") }
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
            if (status != BluetoothGatt.GATT_SUCCESS) {
                LinkLog.w("onCharacteristicWrite status=$status")
                update { copy(lastError = "write status=$status") }
            }
            writing.set(false)
            pumpWrites()
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

    fun connect(device: BluetoothDevice) {
        close()
        update {
            copy(
                deviceAddress = device.address,
                notes = "connecting…",
                lastError = "",
            )
        }
        LinkLog.i("connectGatt autoConnect=false TRANSPORT_LE address=${device.address}")
        gatt = device.connectGatt(
            appContext,
            /* autoConnect = */ false,
            callback,
            BluetoothDevice.TRANSPORT_LE,
        )
    }

    fun close() {
        closeInternal()
        update { LinkMetrics(notes = "idle") }
    }

    private fun closeInternal() {
        writeQueue.clear()
        writing.set(false)
        rxChar = null
        txChar = null
        statusChar = null
        gatt?.close()
        gatt = null
    }

    /** Push a complete SDP message on [channel] (DISPLAY=1, DIAG=7, …). */
    fun sendMessage(channel: Int, message: ByteArray): Boolean {
        val g = gatt
        val rx = rxChar
        if (g == null || rx == null || !_metrics.value.connected) {
            LinkLog.w("sendMessage: not ready")
            return false
        }
        val mtu = _metrics.value.attMtu.coerceAtLeast(23)
        val payloadMax = (mtu - 3).coerceAtMost(SdpFrame.ATT_PAYLOAD_MAX)
        if (payloadMax < 20) {
            LinkLog.w("ATT payload too small: $payloadMax")
        }
        val fragments = writeQueue.enqueueMessage(channel, message)
        LinkLog.i("sendMessage ch=$channel bytes=${message.size} fragments=$fragments")
        pumpWrites()
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

    private fun pumpWrites() {
        if (!writing.compareAndSet(false, true)) return
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
        val ok = writeNoResponse(g, rx, pkt)
        if (!ok) {
            writing.set(false)
            update { copy(lastError = "writeNoResponse failed") }
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
}
