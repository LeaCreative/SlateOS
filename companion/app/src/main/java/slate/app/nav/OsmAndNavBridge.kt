package slate.app.nav

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import net.osmand.aidlapi.IOsmAndAidlCallback
import net.osmand.aidlapi.IOsmAndAidlInterface
import net.osmand.aidlapi.navigation.ADirectionInfo
import net.osmand.aidlapi.navigation.ANavigationUpdateParams
import slate.app.link.LinkLog
import slate.app.notif.NotifChange
import slate.app.notif.NotifStore
import slate.nav.NavManeuver
import slate.nav.OsmAndNotifParser
import slate.nav.OsmAndTurnTypes

/**
 * Live OsmAnd → [NavManeuver] bridge.
 *
 * - AIDL [registerForNavigationUpdates]: turn type + metres to next turn
 *   (relative to direction of travel, not phone orientation).
 * - OsmAnd navigation notification (via [NotifStore]): remaining distance to
 *   destination (AIDL does not expose it) plus street text / AIDL fallback.
 */
class OsmAndNavBridge(
    private val context: Context,
    private val scope: CoroutineScope,
    private val listener: NavManeuverListener,
) {
    private var aidl: IOsmAndAidlInterface? = null
    private var boundPkg: String? = null
    private var navCallbackId = -1L
    private var notifJob: Job? = null
    private var connected = false

    private var turn = "none"
    private var distanceToTurnM = 0
    private var destinationDistanceM = 0
    private var street = ""
    private var etaEpochSec = 0L
    private var lastEmitted: NavManeuver? = null

    private val aidlCallback = object : IOsmAndAidlCallback.Stub() {
        override fun onSearchComplete(resultSet: MutableList<net.osmand.aidlapi.search.SearchResult>?) {}
        override fun onUpdate() {}
        override fun onAppInitialized() {}
        override fun onGpxBitmapCreated(bitmap: net.osmand.aidlapi.gpx.AGpxBitmap?) {}
        override fun onContextMenuButtonClicked(buttonId: Int, pointId: String?, layerId: String?) {}
        override fun onVoiceRouterNotify(params: net.osmand.aidlapi.navigation.OnVoiceNavigationParams?) {}
        override fun onKeyEvent(params: android.view.KeyEvent?) {}
        override fun onLogcatMessage(params: net.osmand.aidlapi.logcat.OnLogcatMessageParams?) {}

        override fun updateNavigationInfo(directionInfo: ADirectionInfo?) {
            if (directionInfo == null) return
            val t = OsmAndTurnTypes.toTurn(directionInfo.turnType)
            val d = directionInfo.distanceTo.coerceAtLeast(0)
            synchronized(this@OsmAndNavBridge) {
                if (t != "none") turn = t
                if (d > 0 || t == "arrive" || t == "off_route") distanceToTurnM = d
            }
            emitIfChanged(force = false)
        }
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val iface = IOsmAndAidlInterface.Stub.asInterface(service)
            aidl = iface
            connected = true
            LinkLog.i("OsmAnd AIDL connected pkg=$boundPkg")
            try {
                val params = ANavigationUpdateParams().apply {
                    setSubscribeToUpdates(true)
                    setCallbackId(-1L)
                }
                navCallbackId = iface.registerForNavigationUpdates(params, aidlCallback)
                LinkLog.i("OsmAnd nav updates callbackId=$navCallbackId")
            } catch (t: Throwable) {
                LinkLog.w("OsmAnd registerForNavigationUpdates: ${t.message}")
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            LinkLog.w("OsmAnd AIDL disconnected")
            aidl = null
            connected = false
            navCallbackId = -1L
        }
    }

    fun start() {
        if (notifJob != null) return
        notifJob = scope.launch {
            NotifStore.changes.collectLatest { change ->
                when (change) {
                    is NotifChange.Upserted -> ingestNotif(change.item.packageName, change.item.title, change.item.text)
                    is NotifChange.Removed -> { /* keep last dest until replaced */ }
                    NotifChange.Cleared -> {}
                }
            }
        }
        // Seed from whatever OsmAnd notif is already up.
        NotifStore.snapshot.value.forEach { item ->
            ingestNotif(item.packageName, item.title, item.text)
        }
        bindOsmAnd()
    }

    fun stop() {
        notifJob?.cancel()
        notifJob = null
        unsubscribeNav()
        if (boundPkg != null) {
            runCatching { context.unbindService(connection) }
        }
        aidl = null
        boundPkg = null
        connected = false
    }

    private fun ingestNotif(pkg: String, title: String, text: String) {
        if (!OsmAndNotifParser.isOsmAndPackage(pkg)) return
        val parsed = OsmAndNotifParser.parse(title, text) ?: return
        synchronized(this) {
            if (parsed.turn != "none" && !connected) {
                // Prefer AIDL turn when bound; notification is fallback.
                turn = parsed.turn
            }
            if (parsed.distanceToTurnM > 0 && !connected) {
                distanceToTurnM = parsed.distanceToTurnM
            }
            if (parsed.destinationDistanceM > 0) {
                destinationDistanceM = parsed.destinationDistanceM
            }
            if (parsed.street.isNotEmpty()) {
                street = parsed.street
            }
        }
        emitIfChanged(force = false)
    }

    private fun emitIfChanged(force: Boolean) {
        val m = synchronized(this) {
            NavManeuver(
                turn = turn,
                distanceM = distanceToTurnM,
                street = street.ifEmpty { if (turn == "none") "Waiting for OsmAnd" else "" },
                progressPct = 0,
                etaEpochSec = etaEpochSec,
                destinationDistanceM = destinationDistanceM,
                status = "ok",
            )
        }
        val prev = lastEmitted
        if (!force && prev != null && !materiallyChanged(prev, m)) return
        lastEmitted = m
        listener.onManeuver(m)
    }

    private fun materiallyChanged(a: NavManeuver, b: NavManeuver): Boolean {
        if (a.turn != b.turn) return true
        if (a.street != b.street) return true
        if (kotlin.math.abs(a.distanceM - b.distanceM) >= 10) return true
        if (kotlin.math.abs(a.destinationDistanceM - b.destinationDistanceM) >= 50) return true
        return false
    }

    private fun bindOsmAnd() {
        val pkg = resolveOsmAndPackage() ?: run {
            LinkLog.i("OsmAnd not installed — nav bridge waits on demo/broadcasts")
            return
        }
        boundPkg = pkg
        val intent = Intent("net.osmand.aidl.OsmandAidlServiceV2").setPackage(pkg)
        val ok = try {
            context.bindService(intent, connection, Context.BIND_AUTO_CREATE)
        } catch (t: Throwable) {
            LinkLog.w("OsmAnd bindService: ${t.message}")
            false
        }
        LinkLog.i("OsmAnd bindService($pkg)=$ok")
    }

    private fun unsubscribeNav() {
        val iface = aidl ?: return
        if (navCallbackId < 0L) return
        try {
            val params = ANavigationUpdateParams().apply {
                setSubscribeToUpdates(false)
                setCallbackId(navCallbackId)
            }
            iface.registerForNavigationUpdates(params, aidlCallback)
        } catch (t: Throwable) {
            LinkLog.w("OsmAnd unsubscribe nav: ${t.message}")
        }
        navCallbackId = -1L
    }

    private fun resolveOsmAndPackage(): String? {
        val pm = context.packageManager
        val candidates = listOf("net.osmand.plus", "net.osmand")
        return candidates.firstOrNull { pkg ->
            try {
                pm.getPackageInfo(pkg, 0)
                true
            } catch (_: PackageManager.NameNotFoundException) {
                false
            }
        }
    }
}
