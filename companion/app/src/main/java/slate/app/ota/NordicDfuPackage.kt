package slate.app.ota

import android.content.ContentResolver
import android.net.Uri
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.InputStream
import java.util.zip.ZipInputStream

data class NordicDfuPackage(
    val firmware: ByteArray,
    val initPacket: ByteArray,
)

object NordicDfuPackageReader {
    private const val MAX_FIRMWARE_BYTES = 475_136
    // InfiniTime's legacy DfuService parses the init packet from one ATT write.
    private const val MAX_INIT_BYTES = 20
    private const val MAX_MANIFEST_BYTES = 64 * 1024

    fun read(resolver: ContentResolver, uri: Uri): NordicDfuPackage {
        return resolver.openInputStream(uri)?.use(::read)
            ?: error("Unable to open selected DFU zip")
    }

    internal fun read(input: InputStream): NordicDfuPackage {
        val entries = HashMap<String, ByteArray>()
        ZipInputStream(input.buffered()).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                if (entry.isDirectory) continue
                val name = entry.name.substringAfterLast('/')
                val limit = when {
                    name == "manifest.json" -> MAX_MANIFEST_BYTES
                    name.endsWith(".bin", ignoreCase = true) -> MAX_FIRMWARE_BYTES
                    name.endsWith(".dat", ignoreCase = true) -> MAX_INIT_BYTES
                    else -> {
                        zip.closeEntry()
                        continue
                    }
                }
                require(name !in entries) { "DFU zip contains duplicate entry $name" }
                entries[name] = readBounded(zip, limit)
                zip.closeEntry()
            }
        }

        val manifestBytes = entries["manifest.json"] ?: error("DFU zip has no manifest.json")
        val manifest = JSONObject(manifestBytes.toString(Charsets.UTF_8))
            .getJSONObject("manifest")
        require(!manifest.has("softdevice") && !manifest.has("bootloader") &&
            !manifest.has("softdevice_bootloader")
        ) {
            "Only application-only InfiniTime MCUBoot packages are allowed"
        }
        val app = manifest.optJSONObject("application")
            ?: error("DFU manifest has no application image")
        val binName = app.getString("bin_file").substringAfterLast('/')
        val datName = app.getString("dat_file").substringAfterLast('/')
        require(binName == "slate-mcuboot-image.bin") {
            "Not a Slate sealed-install package ($binName)"
        }
        val deviceType = app.optJSONObject("init_packet_data")?.optInt("device_type", -1) ?: -1
        require(deviceType == 0x52) {
            "DFU package device type is not PineTime (0x0052)"
        }
        val firmware = entries[binName] ?: error("DFU firmware $binName is missing")
        val initPacket = entries[datName] ?: error("DFU init packet $datName is missing")

        require(firmware.isNotEmpty() && firmware.size <= MAX_FIRMWARE_BYTES) {
            "Application image size ${firmware.size} is invalid"
        }
        require(initPacket.isNotEmpty() && initPacket.size <= MAX_INIT_BYTES) {
            "Init packet size ${initPacket.size} is invalid"
        }
        require(firmware.size >= 4 &&
            firmware[0] == 0x3d.toByte() &&
            firmware[1] == 0xb8.toByte() &&
            firmware[2] == 0xf3.toByte() &&
            firmware[3] == 0x96.toByte()
        ) {
            "Application image has no MCUBoot header"
        }
        validateInitPacket(initPacket, firmware)
        return NordicDfuPackage(firmware, initPacket)
    }

    private fun validateInitPacket(init: ByteArray, firmware: ByteArray) {
        require(init.size >= 12) { "Legacy DFU init packet is truncated" }
        fun u16(offset: Int): Int =
            (init[offset].toInt() and 0xff) or
                ((init[offset + 1].toInt() and 0xff) shl 8)

        require(u16(0) == 0x52) { "Init packet device type is not PineTime" }
        val softDeviceCount = u16(8)
        val expectedSize = 12 + softDeviceCount * 2
        require(init.size == expectedSize) {
            "Legacy DFU init packet length ${init.size} does not match $softDeviceCount SoftDevice IDs"
        }
        val expectedCrc = u16(init.size - 2)
        val actualCrc = crc16(firmware)
        require(actualCrc == expectedCrc) {
            "DFU package CRC mismatch (init=$expectedCrc, image=$actualCrc)"
        }
    }

    private fun crc16(data: ByteArray): Int {
        var crc = 0xffff
        for (byte in data) {
            crc = ((crc ushr 8) and 0xff) or ((crc shl 8) and 0xffff)
            crc = crc xor (byte.toInt() and 0xff)
            crc = crc xor ((crc and 0xff) ushr 4)
            crc = crc xor ((crc shl 12) and 0xffff)
            crc = crc xor (((crc and 0xff) shl 5) and 0xffff)
            crc = crc and 0xffff
        }
        return crc
    }

    private fun readBounded(zip: ZipInputStream, maxBytes: Int): ByteArray {
        val out = ByteArrayOutputStream()
        val buffer = ByteArray(8 * 1024)
        var total = 0
        while (true) {
            val n = zip.read(buffer)
            if (n < 0) break
            total += n
            require(total <= maxBytes) { "DFU zip entry exceeds $maxBytes bytes" }
            out.write(buffer, 0, n)
        }
        return out.toByteArray()
    }
}
