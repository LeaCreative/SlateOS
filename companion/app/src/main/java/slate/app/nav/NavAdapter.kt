package slate.app.nav

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import slate.nav.NavManeuver

/**
 * Generic nav source interface — OsmAnd today; other apps plug in later.
 */
fun interface NavManeuverListener {
    fun onManeuver(m: NavManeuver)
}

/**
 * Listens for:
 * - Slate demo broadcasts (`slate.app.NAV_MANEUVER`)
 * - OsmAnd-style navigation extras when present
 *
 * OsmAnd does not guarantee a stable public broadcast across versions; we
 * accept a documented extra set and the Slate demo action for CI / demos.
 */
class NavAdapter(
    private val context: Context,
    private val listener: NavManeuverListener,
) {
    private var registered = false
    private var last: NavManeuver? = null

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context?, intent: Intent?) {
            if (intent == null) return
            val m = parse(intent) ?: return
            last = m
            listener.onManeuver(m)
        }
    }

    fun start() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(ACTION_SLATE_MANEUVER)
            addAction(ACTION_OSMAND_NAV)
            addAction(ACTION_OSMAND_PLUS_NAV)
        }
        // OsmAnd is a different app, so this receiver must be exported. Parsing
        // still rejects broadcasts without a recognized maneuver payload.
        ContextCompat.registerReceiver(
            context,
            receiver,
            filter,
            ContextCompat.RECEIVER_EXPORTED,
        )
        registered = true
    }

    fun stop() {
        if (!registered) return
        runCatching { context.unregisterReceiver(receiver) }
        registered = false
    }

    fun injectDemo(kind: String) {
        val m = when (kind.lowercase()) {
            "lost_gps", "gps" -> NavManeuver.lostGps(last)
            "disconnected", "disconnect" -> NavManeuver.disconnected(last)
            else -> NavManeuver.demo(kind)
        }
        last = m
        listener.onManeuver(m)
    }

    fun notifyLostGps() {
        val m = NavManeuver.lostGps(last)
        last = m
        listener.onManeuver(m)
    }

    fun notifyDisconnected() {
        val m = NavManeuver.disconnected(last)
        last = m
        listener.onManeuver(m)
    }

    companion object {
        const val ACTION_SLATE_MANEUVER = "slate.app.NAV_MANEUVER"
        const val ACTION_OSMAND_NAV = "net.osmand.navigation"
        const val ACTION_OSMAND_PLUS_NAV = "net.osmand.plus.NAVIGATION"

        fun parse(intent: Intent): NavManeuver? {
            val turn = intent.getStringExtra("turn")
                ?: intent.getStringExtra("turn_type")
                ?: intent.getStringExtra("maneuver")
                ?: return null
            val dist = intent.getIntExtra(
                "distanceM",
                intent.getIntExtra("distance", intent.getIntExtra("dist", 0)),
            )
            val street = intent.getStringExtra("street")
                ?: intent.getStringExtra("streetName")
                ?: intent.getStringExtra("name")
                ?: ""
            val pct = intent.getIntExtra("progressPct", intent.getIntExtra("progress", 0))
            val eta = intent.getLongExtra(
                "etaEpochSec",
                intent.getLongExtra("eta", 0L),
            )
            val status = intent.getStringExtra("status") ?: "ok"
            return NavManeuver(
                turn = turn.lowercase(),
                distanceM = dist,
                street = street,
                progressPct = pct,
                etaEpochSec = eta,
                status = status,
            )
        }
    }
}
