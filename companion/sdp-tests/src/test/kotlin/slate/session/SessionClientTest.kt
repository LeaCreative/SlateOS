package slate.session

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue
import slate.generated.SdpWire

class SessionClientTest {
    private val phoneId = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8)
    private val hostVersion = "1.0.0"

    @Test
    fun helloOfferRoundTripMatchesFirmwareLayout() {
        val offer = SessionTestFixtures.encodeHelloOffer()
        val client = SessionClient(phoneId, hostVersion)
        client.onLinkUp()
        assertEquals(SessionClient.State.Connected, client.state)

        val result = client.onControlMessage(offer)
        // Stay Connected until post-accept CREDIT — offer alone must not Ready.
        assertEquals(SessionClient.State.Connected, client.state)
        assertEquals(1, result.outbound.size)

        val accept = result.outbound[0]
        assertEquals(SdpWire.ControlOp.HELLO_ACCEPT, accept[0].toInt() and 0xFF)
        assertEquals(SessionClient.PROFILE_ID_ACTIVE, accept[1].toInt() and 0xFF)
        assertContentEquals(phoneId, accept.copyOfRange(2, 10))
        assertEquals(hostVersion.length, accept[10].toInt() and 0xFF)
        assertEquals(
            hostVersion,
            accept.copyOfRange(11, accept.size).decodeToString(),
        )

        val parsed = client.helloOffer!!
        assertEquals(SdpWire.PROTOCOL_VERSION, parsed.protocolVersion)
        assertEquals(SdpWire.DISPLAY_SIZE, parsed.width)
        assertEquals(SdpWire.MAX_LIST_BYTES, parsed.freeDlBytes)
        assertEquals(3, parsed.profiles.size)
        assertEquals("active", parsed.profiles[1].name)
        assertEquals(4096, client.freeCreditBytes)

        client.onControlMessage(
            byteArrayOf(SdpWire.ControlOp.CREDIT.toByte(), 0x00, 0x10.toByte()),
        )
        assertEquals(SessionClient.State.Ready, client.state)
    }

    @Test
    fun creditParseUpdatesFreeBytes() {
        val client = SessionClient(phoneId, hostVersion)
        client.onLinkUp()
        val credit = byteArrayOf(
            SdpWire.ControlOp.CREDIT.toByte(),
            0x00,
            0x10.toByte(),
        )
        client.onControlMessage(credit)
        assertEquals(4096, client.freeCreditBytes)
        // CREDIT without a prior HELLO_OFFER must not Ready the session.
        assertEquals(SessionClient.State.Connected, client.state)
    }

    @Test
    fun heartbeatOpcode() {
        val client = SessionClient(phoneId, hostVersion)
        val hb = client.encodeHeartbeat()
        assertContentEquals(byteArrayOf(SdpWire.ControlOp.HEARTBEAT.toByte()), hb)
    }

    @Test
    fun preDisplayControlAlternatesPushAndReplace() {
        val client = SessionClient(phoneId, hostVersion)
        client.scheduleDisplay(SessionClient.PendingDisplay.Push)
        assertEquals(SdpWire.ControlOp.SCREEN_PUSH, client.takePreDisplayControl()[0].toInt() and 0xFF)
        assertEquals(SdpWire.ControlOp.SCREEN_REPLACE, client.takePreDisplayControl()[0].toInt() and 0xFF)
    }

    @Test
    fun linkLossClearsReady() {
        val client = readyClient()
        client.onLinkDown()
        assertEquals(SessionClient.State.Disconnected, client.state)
        assertNull(client.helloOffer)
        assertEquals(0, client.freeCreditBytes)
    }

    @Test
    fun reconnectLifecycleReadyAgain() {
        val client = readyClient()
        client.onLinkDown()
        client.onLinkUp()
        assertEquals(SessionClient.State.Connected, client.state)
        val result = client.onControlMessage(SessionTestFixtures.encodeHelloOffer())
        assertEquals(SessionClient.State.Connected, client.state)
        assertEquals(1, result.outbound.size)
        advanceToReady(client)
        assertEquals(SessionClient.State.Ready, client.state)
    }

    @Test
    fun inboundGoodbyeDisconnects() {
        val client = readyClient()
        client.onControlMessage(byteArrayOf(SdpWire.ControlOp.GOODBYE.toByte()))
        assertEquals(SessionClient.State.Disconnected, client.state)
        assertNull(client.helloOffer)
    }

    @Test
    fun encodeGoodbyeWire() {
        val client = SessionClient(phoneId, hostVersion)
        assertContentEquals(
            byteArrayOf(SdpWire.ControlOp.GOODBYE.toByte()),
            client.encodeGoodbye(),
        )
    }

    @Test
    fun encodeHelloRejectWire() {
        val client = SessionClient(phoneId, hostVersion)
        assertContentEquals(
            byteArrayOf(SdpWire.ControlOp.HELLO_REJECT.toByte(), 7),
            client.encodeHelloReject(7),
        )
    }

    @Test
    fun malformedHelloStaysConnectedNoAccept() {
        val client = SessionClient(phoneId, hostVersion)
        client.onLinkUp()
        val result = client.onControlMessage(byteArrayOf(SdpWire.ControlOp.HELLO_OFFER.toByte(), 1, 2))
        assertEquals(SessionClient.State.Connected, client.state)
        assertTrue(result.outbound.isEmpty())
        assertNull(client.helloOffer)
    }

    @Test
    fun profilePickFallsBackToFirstWhenDefaultMissing() {
        val profiles = listOf(
            SessionTestFixtures.Profile(0, "ambient", 0, 400),
            SessionTestFixtures.Profile(2, "streaming", 70, 12),
        )
        val client = SessionClient(phoneId, hostVersion, defaultProfileId = 1)
        client.onLinkUp()
        client.onControlMessage(SessionTestFixtures.encodeHelloOffer(profiles = profiles))
        assertEquals(0, client.selectedProfileId)
    }

    @Test
    fun profilePickKeepsDefaultWhenProfilesEmpty() {
        val client = SessionClient(phoneId, hostVersion, defaultProfileId = 1)
        client.onLinkUp()
        client.onControlMessage(SessionTestFixtures.encodeHelloOffer(profiles = emptyList()))
        assertEquals(1, client.selectedProfileId)
        assertEquals(SessionClient.State.Connected, client.state)
        advanceToReady(client)
        assertEquals(SessionClient.State.Ready, client.state)
    }

    private fun advanceToReady(client: SessionClient) {
        client.onControlMessage(
            byteArrayOf(SdpWire.ControlOp.CREDIT.toByte(), 0x00, 0x10.toByte()),
        )
    }

    private fun readyClient(): SessionClient {
        val client = SessionClient(phoneId, hostVersion)
        client.onLinkUp()
        client.onControlMessage(SessionTestFixtures.encodeHelloOffer())
        advanceToReady(client)
        assertEquals(SessionClient.State.Ready, client.state)
        return client
    }
}
