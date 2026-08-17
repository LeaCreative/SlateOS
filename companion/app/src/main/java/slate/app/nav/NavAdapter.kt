package slate.app.nav

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import slate.nav.NavManeuver

/**
 * Generic nav source interface — OsmAnd today; other apps plug in later.
 */
fun interface NavManeuverListener {
    fun onManeuver(m: NavManeuver)
}

/**
 * Listens for:
 * - OsmAnd AIDL + OsmAnd navigation notification ([OsmAndNavBridge])
 * - Slate demo broadcasts (`slate.app.NAV_MANEUVER`)
 * - OsmAnd-style navigation extras when present on intents
 *
 * Maneuver [NavManeuver.turn] is always relative to direction of travel.
 */
class NavAdapter(
    private val context: Context,
    private val scope: CoroutineScope,
    private val listener: NavManeuverListener,
) {
    private var registered = false
    private var last: NavManeuver? = null
    private var osmAnd: OsmAndNavBridge? = null

    private val fanOut = NavManeuverListener { m ->
        last = m
        listener.onManeuver(m)
    }

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
        ContextCompat.registerReceiver(
            context,
            receiver,
            filter,
            ContextCompat.RECEIVER_EXPORTED,
        )
        registered = true
        if (osmAnd == null) {
            osmAnd = OsmAndNavBridge(context, scope, fanOut)
        }
        osmAnd?.start()
    }

    fun stop() {
        osmAnd?.stop()
        osmAnd = null
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
            val dest = intent.getIntExtra(
                "destinationDistanceM",
                intent.getIntExtra(
                    "time_distance_left",
                    intent.getIntExtra("distanceLeft", 0),
                ),
            )
            val status = intent.getStringExtra("status") ?: "ok"
            return NavManeuver(
                turn = turn.lowercase(),
                distanceM = dist,
                street = street,
                progressPct = pct,
                etaEpochSec = eta,
                destinationDistanceM = dest,
                status = status,
            )
        }
    }
}
