package slate.app.link

import android.bluetooth.BluetoothAdapter

/**
 * Normalises Bluetooth addresses coming out of CompanionDeviceManager.
 *
 * CDM reports addresses as `AssociationInfo.getDeviceMacAddress()`, and
 * `MacAddress.toString()` renders lowercase hex ("e8:01:34:22:08:89").
 * `BluetoothAdapter.checkBluetoothAddress()` accepts only uppercase hex, so
 * handing a CDM address straight to `getRemoteDevice()` throws
 * IllegalArgumentException. Everything that turns a CDM address into a
 * BluetoothDevice has to come through here first.
 */
object BtAddress {

    /** Uppercased address, or null when it is absent or malformed. */
    fun normalize(raw: String?): String? {
        val addr = raw?.trim()?.uppercase() ?: return null
        if (!BluetoothAdapter.checkBluetoothAddress(addr)) {
            LinkLog.w("ignoring malformed Bluetooth address '$raw'")
            return null
        }
        return addr
    }
}
