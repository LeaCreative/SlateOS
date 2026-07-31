package slate.app.ota

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertFailsWith

class NordicDfuPackageReaderTest {
    @Test
    fun readsApplicationOnlyPackage() {
        val firmware = byteArrayOf(0x3d, 0xb8.toByte(), 0xf3.toByte(), 0x96.toByte())
        val init = initPacket(firmware)
        val pkg = NordicDfuPackageReader.read(
            zip(
                mapOf(
                    "manifest.json" to manifest().encodeToByteArray(),
                    "slate-mcuboot-image.bin" to firmware,
                    "app.dat" to init,
                ),
            ),
        )

        assertContentEquals(firmware, pkg.firmware)
        assertContentEquals(init, pkg.initPacket)
    }

    @Test
    fun rejectsPackageContainingBootloader() {
        assertFailsWith<IllegalArgumentException> {
            NordicDfuPackageReader.read(
                zip(
                    mapOf(
                        "manifest.json" to manifest(
                            ""","bootloader":{"bin_file":"bl.bin","dat_file":"bl.dat"}""",
                        ).encodeToByteArray(),
                        "slate-mcuboot-image.bin" to
                            byteArrayOf(0x3d, 0xb8.toByte(), 0xf3.toByte(), 0x96.toByte()),
                        "app.dat" to initPacket(
                            byteArrayOf(
                                0x3d,
                                0xb8.toByte(),
                                0xf3.toByte(),
                                0x96.toByte(),
                            ),
                        ),
                    ),
                ),
            )
        }
    }

    @Test
    fun rejectsCorruptFirmwareBeforeBleTransfer() {
        val firmware = byteArrayOf(
            0x3d,
            0xb8.toByte(),
            0xf3.toByte(),
            0x96.toByte(),
            7,
        )
        val init = initPacket(firmware)
        firmware[4] = 8

        assertFailsWith<IllegalArgumentException> {
            NordicDfuPackageReader.read(
                zip(
                    mapOf(
                        "manifest.json" to manifest().encodeToByteArray(),
                        "slate-mcuboot-image.bin" to firmware,
                        "app.dat" to init,
                    ),
                ),
            )
        }
    }

    private fun manifest(extra: String = ""): String =
        """{"manifest":{"application":{"bin_file":"slate-mcuboot-image.bin","dat_file":"app.dat","init_packet_data":{"device_type":82}}$extra}}"""

    private fun initPacket(firmware: ByteArray): ByteArray {
        var crc = 0xffff
        for (byte in firmware) {
            crc = ((crc ushr 8) and 0xff) or ((crc shl 8) and 0xffff)
            crc = crc xor (byte.toInt() and 0xff)
            crc = crc xor ((crc and 0xff) ushr 4)
            crc = crc xor ((crc shl 12) and 0xffff)
            crc = crc xor (((crc and 0xff) shl 5) and 0xffff)
            crc = crc and 0xffff
        }
        return byteArrayOf(
            0x52, 0,
            0xff.toByte(), 0xff.toByte(),
            0xff.toByte(), 0xff.toByte(), 0xff.toByte(), 0xff.toByte(),
            1, 0,
            0xfe.toByte(), 0xff.toByte(),
            crc.toByte(), (crc ushr 8).toByte(),
        )
    }

    private fun zip(entries: Map<String, ByteArray>): ByteArrayInputStream {
        val out = ByteArrayOutputStream()
        ZipOutputStream(out).use { zip ->
            entries.forEach { (name, bytes) ->
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
        return ByteArrayInputStream(out.toByteArray())
    }
}
