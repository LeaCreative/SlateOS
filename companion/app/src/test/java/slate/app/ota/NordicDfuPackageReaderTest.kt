package slate.app.ota

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class NordicDfuPackageReaderTest {
    @Test
    fun faceStampReadsVersionAndDate() {
        val blob = ByteArray(64) { 0 }
        blob[0] = 0x3d.toByte()
        blob[1] = 0xb8.toByte()
        blob[2] = 0xf3.toByte()
        blob[3] = 0x96.toByte()
        blob[20] = 0
        blob[21] = 1
        blob[22] = 21
        blob[23] = 0
        val ver = "0.1.0-m21"
        val date = "Aug 18 2026"
        ver.encodeToByteArray().copyInto(blob, 32)
        date.encodeToByteArray().copyInto(blob, 32 + ver.length + 1)
        assertEquals("0.1.0-m21 Aug 18 2026", NordicDfuPackageReader.faceStamp(blob))
        assertEquals("0.1.21", NordicDfuPackageReader.mcubootVersion(blob))
        blob[22] = 0
        blob[23] = 0
        assertEquals("0.1.0", NordicDfuPackageReader.mcubootVersion(blob))
        assertEquals(12, NordicDfuPackageReader.sha12(blob).length)
    }

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
