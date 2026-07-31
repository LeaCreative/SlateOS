package slate.link

/**
 * Pure occupancy verdict for Slate's single BLE central slot
 * (`SLATE_BLE_MAX_CONNECTIONS 1`). Android gathers signals; this module
 * decides the human-readable kind. Remediation copy for apps lives in the
 * Android `LinkContention` wrapper so package names stay off the JVM tests.
 */
object LinkContentionLogic {
    enum class Kind {
        Clear,
        /** Watch address is in this phone's GATT connected list, but Slate is not the holder. */
        HeldOnThisPhone,
        /** Bonded, not advertising (or connect fails while bonded) — foreign central likely. */
        LikelyForeignCentral,
    }

    data class Verdict(
        val kind: Kind,
        /** Short label for UI (blocker name). */
        val summary: String,
    ) {
        val blocked: Boolean get() = kind != Kind.Clear
    }

    data class Signals(
        val weAreConnected: Boolean,
        /** Watch MAC appears in [BluetoothManager.getConnectedDevices(GATT)]. */
        val gattConnectedOnPhone: Boolean,
        val bonded: Boolean,
        /**
         * Recent scan saw the address advertising. null = scan not run
         * (instant path skips the foreign-central branch).
         */
        val advertisingSeen: Boolean? = null,
        /** True after our connect attempt failed while the device is still bonded. */
        val connectFailedWhileBonded: Boolean = false,
    )

    fun evaluate(s: Signals): Verdict {
        if (s.weAreConnected) {
            return Verdict(Kind.Clear, "Slate holds the link")
        }
        if (s.gattConnectedOnPhone) {
            return Verdict(
                Kind.HeldOnThisPhone,
                "Another app on this phone holds the watch BLE link",
            )
        }
        if (s.advertisingSeen == false && s.bonded) {
            return Verdict(
                Kind.LikelyForeignCentral,
                "Watch is bonded but not advertising — another central may hold it",
            )
        }
        if (s.connectFailedWhileBonded && s.bonded) {
            return Verdict(
                Kind.LikelyForeignCentral,
                "Connect failed while bonded — another central may hold the slot",
            )
        }
        return Verdict(Kind.Clear, "No contention detected")
    }
}
