package slate.app.ota

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
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout
import slate.app.link.LinkLog
import java.util.UUID

/**
 * The link dropped mid-operation. Before the image is fully written this is a
 * real failure; once it is written, the target resetting itself looks exactly
 * like this from the phone (GATT status 8, link supervision timeout).
 */
private class LinkLostException(status: Int) :
    IllegalStateException("Watch disconnected during DFU (status=$status)")

/**
 * Raw Android implementation of Nordic SDK 7.x in-app legacy DFU used by
 * InfiniTime/recovery. It accepts application-only adafruit-nrfutil packages.
 */
@SuppressLint("MissingPermission")
class NordicLegacyDfuClient(private val context: Context) {
    private sealed interface Event {
        data class Connection(val status: Int, val state: Int) : Event
        data class Mtu(val mtu: Int, val status: Int) : Event
        data class Services(val status: Int) : Event
        data class DescriptorWrite(val uuid: UUID, val status: Int) : Event
        data class CharacteristicWrite(val uuid: UUID, val status: Int) : Event
        data class CharacteristicRead(
            val uuid: UUID,
            val status: Int,
            val value: ByteArray,
        ) : Event
        data class Notification(val uuid: UUID, val value: ByteArray) : Event
    }

    private val events = Channel<Event>(Channel.UNLIMITED)
    private val pending = ArrayList<Event>()
    private var gatt: BluetoothGatt? = null

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            events.trySend(Event.Connection(status, newState))
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            events.trySend(Event.Mtu(mtu, status))
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            events.trySend(Event.Services(status))
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            events.trySend(Event.DescriptorWrite(descriptor.uuid, status))
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            events.trySend(Event.CharacteristicWrite(characteristic.uuid, status))
        }

        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
            status: Int,
        ) {
            events.trySend(Event.CharacteristicRead(characteristic.uuid, status, value.copyOf()))
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicRead(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: ByteArray(0)
            events.trySend(Event.CharacteristicRead(characteristic.uuid, status, value.copyOf()))
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            events.trySend(Event.Notification(characteristic.uuid, value.copyOf()))
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            val value = characteristic.value ?: return
            events.trySend(Event.Notification(characteristic.uuid, value.copyOf()))
        }
    }

    suspend fun flash(
        device: BluetoothDevice,
        pkg: NordicDfuPackage,
        progress: (percent: Int, message: String) -> Unit,
    ) {
        require(pkg.firmware.size <= MAX_APPLICATION_SIZE)
        progress(0, "Connecting to ${device.address}")

        val localGatt = device.connectGatt(
            context,
            false,
            callback,
            BluetoothDevice.TRANSPORT_LE,
        )
        gatt = localGatt
        var controlForAbort: BluetoothGattCharacteristic? = null
        try {
            val connected = await<Event.Connection>(CONNECT_TIMEOUT_MS) {
                it.state == BluetoothProfile.STATE_CONNECTED ||
                    it.status != BluetoothGatt.GATT_SUCCESS
            }
            check(connected.status == BluetoothGatt.GATT_SUCCESS &&
                connected.state == BluetoothProfile.STATE_CONNECTED
            ) {
                "BLE connection failed (status=${connected.status}, state=${connected.state})"
            }
            localGatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)

            progress(1, "Negotiating BLE")
            if (localGatt.requestMtu(247)) {
                try {
                    await<Event.Mtu>(OP_TIMEOUT_MS) { true }
                } catch (_: TimeoutCancellationException) {
                    // MTU negotiation is optional; legacy DFU packets stay 20 bytes.
                }
            }
            check(localGatt.discoverServices()) { "Could not start service discovery" }
            val services = await<Event.Services>(OP_TIMEOUT_MS) { true }
            check(services.status == BluetoothGatt.GATT_SUCCESS) {
                "Service discovery failed (${services.status})"
            }

            val service = localGatt.getService(DFU_SERVICE)
                ?: error("Nordic legacy DFU service not found")
            val control = service.getCharacteristic(DFU_CONTROL)
                ?: error("DFU control characteristic missing")
            controlForAbort = control
            val packet = service.getCharacteristic(DFU_PACKET)
                ?: error("DFU packet characteristic missing")

            checkBatteryIfAvailable(localGatt)
            enableControlNotifications(localGatt, control)
            progress(2, "Starting application DFU")

            // START_DFU(application), then 3 little-endian image sizes.
            writeCharacteristic(localGatt, control, byteArrayOf(OP_START, MODE_APPLICATION), false)
            writeCharacteristic(
                localGatt,
                packet,
                le32(0) + le32(0) + le32(pkg.firmware.size),
                true,
            )
            awaitResponse(OP_START, ERASE_TIMEOUT_MS)

            // INITIALIZE_DFU(start), init packet, INITIALIZE_DFU(complete).
            progress(3, "Sending init packet")
            writeCharacteristic(localGatt, control, byteArrayOf(OP_INITIALIZE, 0), false)
            pkg.initPacket.asListChunks(DATA_PACKET_SIZE).forEach {
                writeCharacteristic(localGatt, packet, it, true)
            }
            writeCharacteristic(localGatt, control, byteArrayOf(OP_INITIALIZE, 1), false)
            awaitResponse(OP_INITIALIZE, OP_TIMEOUT_MS)

            // Request packet-receipt notification every 10 packets.
            writeCharacteristic(
                localGatt,
                control,
                byteArrayOf(OP_PACKET_RECEIPT, PRN_INTERVAL.toByte(), 0),
                false,
            )
            writeCharacteristic(localGatt, control, byteArrayOf(OP_RECEIVE), false)

            val packetCount =
                (pkg.firmware.size + DATA_PACKET_SIZE - 1) / DATA_PACKET_SIZE
            var lastProgress = -1
            for (index in 0 until packetCount) {
                val offset = index * DATA_PACKET_SIZE
                val bytes = pkg.firmware.copyOfRange(
                    offset,
                    minOf(offset + DATA_PACKET_SIZE, pkg.firmware.size),
                )
                writeCharacteristic(localGatt, packet, bytes, true)
                // InfiniTime sends the final RECEIVE response instead of a PRN
                // when the image ends exactly on a PRN boundary.
                if ((index + 1) % PRN_INTERVAL == 0 && index + 1 < packetCount) {
                    awaitPacketReceipt(
                        expectedBytes = (index + 1) * DATA_PACKET_SIZE,
                        timeoutMs = OP_TIMEOUT_MS,
                    )
                }
                val percent = 5 + (((index + 1L) * 90L) / packetCount).toInt()
                if (percent != lastProgress) {
                    lastProgress = percent
                    progress(percent, "Uploading ${index + 1}/$packetCount packets")
                }
            }
            awaitResponse(OP_RECEIVE, OP_TIMEOUT_MS)

            // The whole image is on the watch and PRN-verified from here, so the
            // target is entitled to reset at any moment — and a reset is only
            // visible to the phone as a dropped link. Treat that as the success
            // path, but keep reporting an explicit DFU error response as failure.
            var resetDuringActivate = false
            progress(96, "Validating image")
            try {
                writeCharacteristic(localGatt, control, byteArrayOf(OP_VALIDATE), false)
                awaitResponse(OP_VALIDATE, OP_TIMEOUT_MS)

                progress(99, "Activating image and rebooting watch")
                writeCharacteristic(localGatt, control, byteArrayOf(OP_ACTIVATE_RESET), false)
                await<Event.Connection>(ACTIVATE_TIMEOUT_MS) {
                    it.state == BluetoothProfile.STATE_DISCONNECTED
                }
            } catch (t: LinkLostException) {
                resetDuringActivate = true
                LinkLog.i("watch reset while activating: ${t.message}")
            } catch (_: TimeoutCancellationException) {
                // Activate is fire-and-forget; the write callback already succeeded.
            }
            progress(
                100,
                if (resetDuringActivate) {
                    "Image sent; watch reset itself. Check it booted Slate."
                } else {
                    "DFU complete; watch is rebooting"
                },
            )
        } catch (t: Throwable) {
            val control = controlForAbort
            if (control != null) {
                try {
                    writeCharacteristic(localGatt, control, byteArrayOf(OP_ABORT), false)
                } catch (_: Throwable) {
                    // Link loss or targets without Abort support are expected here.
                }
            }
            throw t
        } finally {
            runCatching { localGatt.disconnect() }
            localGatt.close()
            if (gatt === localGatt) gatt = null
        }
    }

    fun cancel() {
        val local = gatt ?: return
        runCatching { local.disconnect() }
        local.close()
        gatt = null
    }

    private suspend fun checkBatteryIfAvailable(g: BluetoothGatt) {
        val battery = g.getService(BATTERY_SERVICE)
            ?.getCharacteristic(BATTERY_LEVEL)
            ?: return
        if (!g.readCharacteristic(battery)) return
        val event = await<Event.CharacteristicRead>(OP_TIMEOUT_MS) {
            it.uuid == BATTERY_LEVEL
        }
        if (event.status == BluetoothGatt.GATT_SUCCESS && event.value.isNotEmpty()) {
            val percent = event.value[0].toInt() and 0xff
            require(percent in 0..100) {
                "Watch battery unknown ($percent) — wait for a sample, or charge, then retry DFU"
            }
            require(percent >= MIN_BATTERY_PERCENT) {
                "Watch battery is $percent%; charge to at least $MIN_BATTERY_PERCENT% before DFU"
            }
        }
    }

    private suspend fun enableControlNotifications(
        g: BluetoothGatt,
        control: BluetoothGattCharacteristic,
    ) {
        check(g.setCharacteristicNotification(control, true)) {
            "Could not enable DFU control notifications"
        }
        val cccd = control.getDescriptor(CCCD) ?: error("DFU control CCCD missing")
        val submitted = if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            g.writeDescriptor(cccd)
        }
        check(submitted) { "Could not write DFU control CCCD" }
        val event = await<Event.DescriptorWrite>(OP_TIMEOUT_MS) { it.uuid == CCCD }
        check(event.status == BluetoothGatt.GATT_SUCCESS) {
            "DFU notification setup failed (${event.status})"
        }
    }

    private suspend fun writeCharacteristic(
        g: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic,
        value: ByteArray,
        noResponse: Boolean,
    ) {
        val writeType = if (noResponse) {
            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
        } else {
            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        }
        val submitted = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(characteristic, value, writeType) ==
                BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = writeType
            @Suppress("DEPRECATION")
            characteristic.value = value
            @Suppress("DEPRECATION")
            g.writeCharacteristic(characteristic)
        }
        check(submitted) { "BLE write was rejected (${characteristic.uuid})" }
        val event = await<Event.CharacteristicWrite>(OP_TIMEOUT_MS) {
            it.uuid == characteristic.uuid
        }
        check(event.status == BluetoothGatt.GATT_SUCCESS) {
            "BLE write failed (${event.status}, ${characteristic.uuid})"
        }
    }

    private suspend fun awaitResponse(requestOpcode: Byte, timeoutMs: Long) {
        val event = await<Event.Notification>(timeoutMs) {
            it.uuid == DFU_CONTROL &&
                it.value.size >= 3 &&
                it.value[0] == OP_RESPONSE &&
                it.value[1] == requestOpcode
        }
        val status = event.value[2].toInt() and 0xff
        check(status == STATUS_SUCCESS) {
            "DFU operation ${requestOpcode.toInt() and 0xff} failed: ${statusName(status)}"
        }
    }

    private suspend fun awaitPacketReceipt(expectedBytes: Int, timeoutMs: Long) {
        val event = await<Event.Notification>(timeoutMs) {
            it.uuid == DFU_CONTROL &&
                it.value.size >= 5 &&
                it.value[0] == OP_PACKET_RECEIPT_NOTIFICATION
        }
        val received = (event.value[1].toInt() and 0xff) or
            ((event.value[2].toInt() and 0xff) shl 8) or
            ((event.value[3].toInt() and 0xff) shl 16) or
            ((event.value[4].toInt() and 0xff) shl 24)
        check(received == expectedBytes) {
            "DFU packet receipt mismatch (watch=$received, sent=$expectedBytes)"
        }
    }

    private suspend inline fun <reified T : Event> await(
        timeoutMs: Long,
        crossinline predicate: (T) -> Boolean,
    ): T = withTimeout(timeoutMs) {
        while (true) {
            val pendingIndex = pending.indexOfFirst { it is T && predicate(it) }
            if (pendingIndex >= 0) {
                @Suppress("UNCHECKED_CAST")
                return@withTimeout pending.removeAt(pendingIndex) as T
            }
            val event = events.receive()
            if (event is T && predicate(event)) {
                return@withTimeout event
            }
            if (event is Event.Connection &&
                (event.status != BluetoothGatt.GATT_SUCCESS ||
                    event.state == BluetoothProfile.STATE_DISCONNECTED)
            ) {
                throw LinkLostException(event.status)
            }
            pending += event
        }
        error("unreachable")
    }

    private fun ByteArray.asListChunks(size: Int): List<ByteArray> {
        if (isEmpty()) return emptyList()
        return (indices step size).map { offset ->
            copyOfRange(offset, minOf(offset + size, this.size))
        }
    }

    private fun le32(value: Int): ByteArray = byteArrayOf(
        value.toByte(),
        (value ushr 8).toByte(),
        (value ushr 16).toByte(),
        (value ushr 24).toByte(),
    )

    private fun statusName(status: Int): String = when (status) {
        2 -> "invalid state"
        3 -> "not supported"
        4 -> "data size exceeds limit"
        5 -> "CRC error"
        6 -> "operation failed"
        else -> "status $status"
    }

    companion object {
        val DFU_SERVICE: UUID = UUID.fromString("00001530-1212-efde-1523-785feabcd123")
        val DFU_CONTROL: UUID = UUID.fromString("00001531-1212-efde-1523-785feabcd123")
        val DFU_PACKET: UUID = UUID.fromString("00001532-1212-efde-1523-785feabcd123")
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        val BATTERY_SERVICE: UUID = UUID.fromString("0000180f-0000-1000-8000-00805f9b34fb")
        val BATTERY_LEVEL: UUID = UUID.fromString("00002a19-0000-1000-8000-00805f9b34fb")

        private const val OP_START: Byte = 1
        private const val OP_INITIALIZE: Byte = 2
        private const val OP_RECEIVE: Byte = 3
        private const val OP_VALIDATE: Byte = 4
        private const val OP_ACTIVATE_RESET: Byte = 5
        private const val OP_ABORT: Byte = 6
        private const val OP_PACKET_RECEIPT: Byte = 8
        private const val OP_RESPONSE: Byte = 16
        private const val OP_PACKET_RECEIPT_NOTIFICATION: Byte = 17
        private const val MODE_APPLICATION: Byte = 4
        private const val STATUS_SUCCESS = 1
        private const val DATA_PACKET_SIZE = 20
        private const val PRN_INTERVAL = 10
        private const val MAX_APPLICATION_SIZE = 475_136
        private const val MIN_BATTERY_PERCENT = 30
        private const val CONNECT_TIMEOUT_MS = 20_000L
        private const val OP_TIMEOUT_MS = 30_000L
        private const val ERASE_TIMEOUT_MS = 120_000L
        private const val ACTIVATE_TIMEOUT_MS = 10_000L
    }
}
