package slate.ota

/**
 * Heuristic preflight before offering InfiniTime-compatible `slate-dfu.zip`.
 * MCUBoot itself has no BLE — we infer from the running app / advertising.
 */
enum class SealedDfuVerdict {
    /** InfiniTime (or recovery) + Nordic DFU — safe to offer MCUBoot zip. */
    AllowInfiniTimeDfu,
    /** SoftDevice / wasp-style DFU — do not flash Slate MCUBoot image. */
    BlockSoftDevice,
    /** Already Slate — use channel-5 OTA instead. */
    UseSlateOta,
    /** Not enough signal. */
    Unknown,
}

data class SealedDfuProbe(
    val advertisedName: String? = null,
    /** Service UUIDs from advertising or GATT discovery (any string form). */
    val serviceUuids: Set<String> = emptySet(),
    /** DIS Firmware Revision (0x2A26), if read. */
    val firmwareRevision: String? = null,
)

object SealedDfuPreflight {
    /** Nordic legacy DFU service (InfiniTime / Slate). */
    const val NORDIC_DFU = "00001530-1212-efde-1523-785feabcd123"
    /** Slate primary GATT service (must match [slate.uuid.SlateUuids.SERVICE]). */
    const val SLATE_SERVICE = "e979acfb-c338-0000-a962-e96e4cf078f3"

    fun classify(probe: SealedDfuProbe): SealedDfuVerdict {
        val name = probe.advertisedName?.trim().orEmpty()
        val uuids = probe.serviceUuids.map { normalizeUuid(it) }.toSet()
        val fw = probe.firmwareRevision?.trim().orEmpty()

        if (name.contains("pinedfu", ignoreCase = true) ||
            name.contains("pine dfu", ignoreCase = true) ||
            name.equals("PineDFU", ignoreCase = true)
        ) {
            return SealedDfuVerdict.BlockSoftDevice
        }

        if (uuids.any { it.contains("e979acfb") } ||
            name.contains("slate", ignoreCase = true)
        ) {
            return SealedDfuVerdict.UseSlateOta
        }

        val looksInfini =
            name.contains("infinitime", ignoreCase = true) ||
                name.contains("pinetime", ignoreCase = true) ||
                fw.matches(Regex("""\d+\.\d+(\.\d+)?"""))
        val hasNordicDfu = uuids.any { it.contains("1530") && it.contains("1212") }

        if (looksInfini && (hasNordicDfu || fw.isNotEmpty() || name.contains("infinitime", true))) {
            return SealedDfuVerdict.AllowInfiniTimeDfu
        }
        if (hasNordicDfu && looksInfini) {
            return SealedDfuVerdict.AllowInfiniTimeDfu
        }
        if (hasNordicDfu && name.isEmpty() && fw.isEmpty()) {
            // DFU alone is weak; treat as unknown unless name/fw helps.
            return SealedDfuVerdict.Unknown
        }
        if (looksInfini) {
            // InfiniTime advertising without scanned 128-bit DFU UUID is still typical.
            return SealedDfuVerdict.AllowInfiniTimeDfu
        }
        return SealedDfuVerdict.Unknown
    }

    fun userMessage(v: SealedDfuVerdict): String = when (v) {
        SealedDfuVerdict.AllowInfiniTimeDfu ->
            "Looks like InfiniTime + MCUBoot DFU — safe to install slate-dfu.zip (heuristic, not a BL attestation)."
        SealedDfuVerdict.BlockSoftDevice ->
            "SoftDevice/PineDFU detected — do not flash slate-dfu.zip (wasp-style bootloader)."
        SealedDfuVerdict.UseSlateOta ->
            "Already Slate — use companion channel-5 OTA, not the InfiniTime-first-hop zip."
        SealedDfuVerdict.Unknown ->
            "Could not classify target. Prefer a watch advertising InfiniTime, or open recovery DFU."
    }

    private fun normalizeUuid(raw: String): String =
        raw.lowercase().replace("-", "")
}
