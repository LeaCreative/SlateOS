package slate.app.call

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import androidx.core.content.ContextCompat
import slate.app.link.LinkLog

/**
 * Incoming-call detection for display-only watch alerts.
 *
 * Prefers [TelephonyManager] when [Manifest.permission.READ_PHONE_STATE] is
 * granted. [telephonyActive] is true only while that path is live so the
 * notification fallback does not double-fire CALL_ALERT.
 */
class CallMonitor(
    private val context: Context,
    private val onEvent: (Event) -> Unit,
) {
    sealed class Event {
        data class Ringing(val caller: String) : Event()
        data object Idle : Event()
    }

    @Volatile
    var telephonyActive: Boolean = false
        private set

    private var tm: TelephonyManager? = null
    private var legacyListener: PhoneStateListener? = null
    private var modernCallback: TelephonyCallback? = null
    private var lastState: Int = TelephonyManager.CALL_STATE_IDLE

    fun start() {
        stop()
        val hasPerm = ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.READ_PHONE_STATE,
        ) == PackageManager.PERMISSION_GRANTED
        if (!hasPerm) {
            LinkLog.i("CallMonitor: no READ_PHONE_STATE — notif fallback only")
            telephonyActive = false
            return
        }
        tm = context.getSystemService(TelephonyManager::class.java) ?: run {
            telephonyActive = false
            return
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val cb = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                override fun onCallStateChanged(state: Int) {
                    handleState(state, null)
                }
            }
            modernCallback = cb
            tm?.registerTelephonyCallback(context.mainExecutor, cb)
        } else {
            @Suppress("DEPRECATION")
            val listener = object : PhoneStateListener() {
                @Deprecated("Deprecated in Java")
                override fun onCallStateChanged(state: Int, phoneNumber: String?) {
                    handleState(state, phoneNumber)
                }
            }
            legacyListener = listener
            @Suppress("DEPRECATION")
            tm?.listen(listener, PhoneStateListener.LISTEN_CALL_STATE)
        }
        telephonyActive = true
        LinkLog.i("CallMonitor: telephony listening")
    }

    fun stop() {
        val mgr = tm
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            modernCallback?.let { cb ->
                try {
                    mgr?.unregisterTelephonyCallback(cb)
                } catch (_: Throwable) {
                }
            }
        } else {
            @Suppress("DEPRECATION")
            legacyListener?.let { listener ->
                try {
                    mgr?.listen(listener, PhoneStateListener.LISTEN_NONE)
                } catch (_: Throwable) {
                }
            }
        }
        modernCallback = null
        legacyListener = null
        tm = null
        telephonyActive = false
        lastState = TelephonyManager.CALL_STATE_IDLE
    }

    private fun handleState(state: Int, number: String?) {
        if (state == lastState) return
        val prev = lastState
        lastState = state
        when (state) {
            TelephonyManager.CALL_STATE_RINGING -> {
                val who = number?.takeIf { it.isNotBlank() }
                    ?: peekIncomingNumber()
                    ?: "Incoming call"
                onEvent(Event.Ringing(who.take(32)))
            }
            TelephonyManager.CALL_STATE_IDLE,
            TelephonyManager.CALL_STATE_OFFHOOK,
            -> {
                if (prev == TelephonyManager.CALL_STATE_RINGING) {
                    onEvent(Event.Idle)
                }
            }
        }
    }

    private fun peekIncomingNumber(): String? {
        // API 29+ often redacts the number without READ_CALL_LOG; label is enough.
        return null
    }
}
