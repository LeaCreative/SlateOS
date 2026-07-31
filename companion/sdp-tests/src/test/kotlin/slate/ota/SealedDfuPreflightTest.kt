package slate.ota

import kotlin.test.Test
import kotlin.test.assertEquals

class SealedDfuPreflightTest {
    @Test
    fun infinitimeByName() {
        val v = SealedDfuPreflight.classify(
            SealedDfuProbe(advertisedName = "InfiniTime"),
        )
        assertEquals(SealedDfuVerdict.AllowInfiniTimeDfu, v)
    }

    @Test
    fun pinedfuBlocked() {
        val v = SealedDfuPreflight.classify(
            SealedDfuProbe(advertisedName = "PineDFU"),
        )
        assertEquals(SealedDfuVerdict.BlockSoftDevice, v)
    }

    @Test
    fun slateUsesOta() {
        val v = SealedDfuPreflight.classify(
            SealedDfuProbe(
                advertisedName = "Slate",
                serviceUuids = setOf(SealedDfuPreflight.SLATE_SERVICE),
            ),
        )
        assertEquals(SealedDfuVerdict.UseSlateOta, v)
    }

    @Test
    fun nordicDfuPlusVersion() {
        val v = SealedDfuPreflight.classify(
            SealedDfuProbe(
                firmwareRevision = "1.14.0",
                serviceUuids = setOf(SealedDfuPreflight.NORDIC_DFU),
            ),
        )
        assertEquals(SealedDfuVerdict.AllowInfiniTimeDfu, v)
    }
}
