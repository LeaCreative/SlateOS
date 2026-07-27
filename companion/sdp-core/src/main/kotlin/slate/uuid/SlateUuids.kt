package slate.uuid

import java.util.UUID

/** Slate GATT UUIDs — must match firmware `slate_uuids.hpp`. */
object SlateUuids {
    const val BASE = "e979acfb-c338-44fa-a962-e96e4cf078f3"
    val SERVICE: UUID = UUID.fromString("e979acfb-c338-0000-a962-e96e4cf078f3")
    val RX: UUID = UUID.fromString("e979acfb-c338-0001-a962-e96e4cf078f3")
    val TX: UUID = UUID.fromString("e979acfb-c338-0002-a962-e96e4cf078f3")
    val STATUS: UUID = UUID.fromString("e979acfb-c338-0003-a962-e96e4cf078f3")

    /** Client Characteristic Configuration Descriptor. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
}
