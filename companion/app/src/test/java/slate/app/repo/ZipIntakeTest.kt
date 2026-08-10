package slate.app.repo

import kotlin.test.Test
import kotlin.test.assertEquals

class ZipIntakeTest {
    @Test
    fun dfuFromNestedManifest() {
        val json =
            """{"manifest":{"application":{"bin_file":"slate-mcuboot-image.bin","dat_file":"app.dat"}}}"""
        assertEquals(ZipKind.Dfu, ZipIntake.classifyManifest(json))
        assertEquals(
            ZipKind.Dfu,
            ZipIntake.classify(mapOf("manifest.json" to json.encodeToByteArray())),
        )
    }

    @Test
    fun dfuFromImageFilenameWithoutParsingManifest() {
        assertEquals(
            ZipKind.Dfu,
            ZipIntake.classify(
                mapOf(
                    "slate-mcuboot-image.bin" to byteArrayOf(0x3d, 0xb8.toByte()),
                    "manifest.json" to "{}".encodeToByteArray(),
                ),
            ),
        )
    }

    @Test
    fun subAppFromSlatePackageKeys() {
        val json =
            """{"id":"slate.timer","name":"Timer","version":"1.0","minProtocolVersion":1,"minHostVersion":"0.1","entry":"main.js"}"""
        assertEquals(ZipKind.SubApp, ZipIntake.classifyManifest(json))
        assertEquals(
            ZipKind.SubApp,
            ZipIntake.classify(
                mapOf(
                    "manifest.json" to json.encodeToByteArray(),
                    "main.js" to "export function render(){}".encodeToByteArray(),
                ),
            ),
        )
    }

    @Test
    fun unknownWhenNeitherShape() {
        assertEquals(ZipKind.Unknown, ZipIntake.classifyManifest("""{"foo":1}"""))
        assertEquals(ZipKind.Unknown, ZipIntake.classify(emptyMap()))
        assertEquals(
            ZipKind.Unknown,
            ZipIntake.classify(mapOf("readme.txt" to "hi".encodeToByteArray())),
        )
    }
}
