package slate.session

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNotNull

/**
 * N-6 golden: byte-exact HELLO_OFFER as emitted by the firmware encoder
 * (`Manager::encode_hello_offer`, pinned by tests/host/test_session.cpp
 * "hello_offer golden"). If either side changes the layout or the profile
 * catalog, both goldens must be regenerated together.
 */
class HelloOfferGoldenTest {
    // fw 0.11.0, proto 1, 240×240, base opcode bitmap, free_dl 4096,
    // profiles {ambient 0/400, active 55/24, streaming 70/12}, diag=1.
    private val goldenHex =
        "01000B000100F000F000FE070F00030003000100030003000000000000000000" +
            "000000000000000003000010030007616D6269656E74009001010661637469" +
            "7665371800020973747265616D696E67460C000001"

    private fun golden(): ByteArray =
        goldenHex.chunked(2).map { it.toInt(16).toByte() }.toByteArray()

    @Test
    fun fixtureEncoderMatchesFirmwareGolden() {
        assertContentEquals(golden(), SessionTestFixtures.encodeHelloOffer())
    }

    @Test
    fun sessionClientDecodesFirmwareGolden() {
        val client = SessionClient(ByteArray(SessionClient.PHONE_ID_BYTES), "1.0.0")
        val offer = client.parseHelloOffer(golden())
        assertNotNull(offer)
        assertEquals(0, offer.fwMajor)
        assertEquals(11, offer.fwMinor)
        assertEquals(0, offer.fwPatch)
        assertEquals(1, offer.protocolVersion)
        assertEquals(240, offer.width)
        assertEquals(240, offer.height)
        assertEquals(4096, offer.freeDlBytes)
        assertEquals(1, offer.flags)
        assertEquals(3, offer.profiles.size)
        assertEquals(
            SessionClient.ProfileDesc(0, "ambient", 0, 400),
            offer.profiles[0],
        )
        assertEquals(
            SessionClient.ProfileDesc(1, "active", 55, 24),
            offer.profiles[1],
        )
        assertEquals(
            SessionClient.ProfileDesc(2, "streaming", 70, 12),
            offer.profiles[2],
        )
    }
}
