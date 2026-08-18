package slate.app.nav

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import net.osmand.aidlapi.IOsmAndAidlCallback
import net.osmand.aidlapi.IOsmAndAidlInterface
import net.osmand.aidlapi.navigation.ADirectionInfo
import net.osmand.aidlapi.navigation.ANavigationUpdateParams
import net.osmand.aidlapi.navigation.ANavigationVoiceRouterMessageParams
import net.osmand.aidlapi.navigation.OnVoiceNavigationParams
import slate.app.link.LinkLog
import slate.app.notif.NotifChange
import slate.app.notif.NotifItem
import slate.app.notif.NotifStore
import slate.nav.NavManeuver
import slate.nav.OsmAndNotifParser
import slate.nav.OsmAndTurnTypes

/**
 * Live OsmAnd → [NavManeuver] bridge.
 *
 * OsmAnd AIDL often binds but [registerForNavigationUpdates] returns -1, so
 * the **notification** is the live source for distances. AIDL, when it
 * actually delivers, only overrides the turn token. OsmAnd has no TurnType
 * for arrival — voice `reached_destination`, dest remaining ≤ 40 m, and
 * the nav notification going away near the pin are the arrival signals.
 */
class OsmAndNavBridge(
    private val context: Context,
    private val scope: CoroutineScope,
    private val listener: NavManeuverListener,
) {
    private var aidl: IOsmAndAidlInterface? = null
    private var boundPkg: String? = null
    private var navCallbackId = -1L
    private var voiceCallbackId = -1L
    private var notifJob: Job? = null

    private var turn = "none"
    private var distanceToTurnM = 0
    private var destinationDistanceM = 0
    private var street = ""
    private var roundaboutExit = 0
    private var etaEpochSec = 0L
    private var sawDestination = false
    private val navNotifKeys = mutableSetOf<String>()
    private var lastEmitted: NavManeuver? = null

    val lastManeuver: NavManeuver? get() = lastEmitted

    private val aidlCallback = object : IOsmAndAidlCallback.Stub() {
        override fun onSearchComplete(resultSet: MutableList<net.osmand.aidlapi.search.SearchResult>?) {}
        override fun onUpdate() {}
        override fun onAppInitialized() {}
        override fun onGpxBitmapCreated(bitmap: net.osmand.aidlapi.gpx.AGpxBitmap?) {}
        override fun onContextMenuButtonClicked(buttonId: Int, pointId: String?, layerId: String?) {}
        override fun onVoiceRouterNotify(params: OnVoiceNavigationParams?) {
            val cmds = params?.commands ?: return
            if (!OsmAndTurnTypes.isArrivalVoiceCommand(cmds)) return
            LinkLog.i("OsmAnd voice reached_destination")
            synchronized(this@OsmAndNavBridge) { markArrivedLocked() }
            emitIfChanged(force = true)
        }
        override fun onKeyEvent(params: android.view.KeyEvent?) {}
        override fun onLogcatMessage(params: net.osmand.aidlapi.logcat.OnLogcatMessageParams?) {}

        override fun updateNavigationInfo(directionInfo: ADirectionInfo?) {
            if (directionInfo == null) return
            val t = OsmAndTurnTypes.toTurn(directionInfo.turnType)
            val d = directionInfo.distanceTo.coerceAtLeast(0)
            synchronized(this@OsmAndNavBridge) {
                if (t != "none") turn = t
                if (d > 0 || t == "arrive" || t == "off_route") distanceToTurnM = d
                promoteLocked()
            }
            emitIfChanged(force = false)
        }
    }

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            val iface = IOsmAndAidlInterface.Stub.asInterface(service)
            aidl = iface
            LinkLog.i("OsmAnd AIDL connected pkg=$boundPkg")
            try {
                val params = ANavigationUpdateParams().apply {
                    setSubscribeToUpdates(true)
                    setCallbackId(-1L)
                }
                navCallbackId = iface.registerForNavigationUpdates(params, aidlCallback)
                if (navCallbackId < 0L) {
                    LinkLog.w("OsmAnd nav updates rejected (callbackId=-1) — using notification")
                } else {
                    LinkLog.i("OsmAnd nav updates callbackId=$navCallbackId")
                }
            } catch (t: Throwable) {
                navCallbackId = -1L
                LinkLog.w("OsmAnd registerForNavigationUpdates: ${t.message}")
            }
            try {
                val voiceParams = ANavigationVoiceRouterMessageParams().apply {
                    setSubscribeToUpdates(true)
                    setCallbackId(-1L)
                }
                voiceCallbackId = iface.registerForVoiceRouterMessages(voiceParams, aidlCallback)
                if (voiceCallbackId < 0L) {
                    LinkLog.w("OsmAnd voice router rejected (callbackId=-1)")
                } else {
                    LinkLog.i("OsmAnd voice router callbackId=$voiceCallbackId")
                }
            } catch (t: Throwable) {
                voiceCallbackId = -1L
                LinkLog.w("OsmAnd registerForVoiceRouterMessages: ${t.message}")
            }
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            LinkLog.w("OsmAnd AIDL disconnected")
            aidl = null
            navCallbackId = -1L
            voiceCallbackId = -1L
        }
    }

    fun start() {
        if (notifJob != null) return
        notifJob = scope.launch {
            NotifStore.changes.collect { change ->
                when (change) {
                    is NotifChange.Upserted -> ingestNotif(change.item)
                    is NotifChange.Removed -> onNotifRemoved(change.key)
                    NotifChange.Cleared -> {}
                }
            }
        }
        NotifStore.snapshot.value.forEach { item -> ingestNotif(item) }
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
    }

    fun replayLast() {
        val m = lastEmitted
        if (m != null) {
            listener.onManeuver(m)
            return
        }
        NotifStore.snapshot.value.forEach { item -> ingestNotif(item) }
    }

    private fun ingestNotif(item: NotifItem) {
        ingestNotif(item.key, item.packageName, item.title, item.text)
    }

    private fun ingestNotif(key: String, pkg: String, title: String, text: String) {
        if (!OsmAndNotifParser.isOsmAndPackage(pkg)) return
        val parsed = OsmAndNotifParser.parse(title, text) ?: return
        synchronized(this) {
            navNotifKeys.add(key)
            // Notification is the live feed. AIDL bind-success must not freeze these.
            if (parsed.turn != "none") turn = parsed.turn
            if (parsed.distanceToTurnM > 0 || parsed.turn == "arrive") {
                distanceToTurnM = parsed.distanceToTurnM
            }
            if (parsed.hasDestination) {
                destinationDistanceM = parsed.destinationDistanceM
            }
            if (parsed.roundaboutExit > 0) roundaboutExit = parsed.roundaboutExit
            if (parsed.street.isNotEmpty()) street = parsed.street
            if (parsed.turn == "arrive") markArrivedLocked() else promoteLocked()
        }
        LinkLog.i(
            "OsmAnd notif turn=${parsed.turn} toTurn=${parsed.distanceToTurnM}m " +
                "dest=${parsed.destinationDistanceM}m exit=${parsed.roundaboutExit}",
        )
        emitIfChanged(force = false)
    }

    private fun onNotifRemoved(key: String) {
        val arrivedOrIdle = synchronized(this) {
            if (!navNotifKeys.remove(key) || navNotifKeys.isNotEmpty()) return
            when {
                turn == "arrive" -> false
                sawDestination && destinationDistanceM <= 80 -> {
                    markArrivedLocked()
                    true
                }
                sawDestination -> {
                    resetIdleLocked()
                    true
                }
                else -> false
            }
        }
        if (arrivedOrIdle) emitIfChanged(force = true)
    }

    private fun markArrivedLocked() {
        turn = "arrive"
        distanceToTurnM = 0
        destinationDistanceM = 0
        sawDestination = true
    }

    private fun resetIdleLocked() {
        turn = "none"
        distanceToTurnM = 0
        destinationDistanceM = 0
        street = "Waiting for OsmAnd"
        roundaboutExit = 0
        sawDestination = false
    }

    private fun promoteLocked() {
        if (destinationDistanceM > 0) sawDestination = true
        turn = OsmAndTurnTypes.promoteArrive(
            turn,
            distanceToTurnM,
            destinationDistanceM,
            sawDestination,
        )
        if (turn == "arrive") {
            distanceToTurnM = 0
            destinationDistanceM = 0
        }
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
                roundaboutExit = roundaboutExit,
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
        if (a.roundaboutExit != b.roundaboutExit) return true
        if (kotlin.math.abs(a.distanceM - b.distanceM) >= 5) return true
        if (a.destinationDistanceM == 0 && b.destinationDistanceM != 0) return true
        if (b.destinationDistanceM == 0 && a.destinationDistanceM != 0) return true
        if (kotlin.math.abs(a.destinationDistanceM - b.destinationDistanceM) >= 20) return true
        return false
    }

    private fun bindOsmAnd() {
        if (boundPkg != null) return
        val pkg = resolveOsmAndPackage() ?: run {
            LinkLog.i("OsmAnd not installed — nav bridge waits on notification/demo")
            return
        }
        boundPkg = pkg
        val intent = Intent("net.osmand.aidl.OsmandAidlServiceV2").setPackage(pkg)
        var flags = Context.BIND_AUTO_CREATE
        if (Build.VERSION.SDK_INT >= 34) {
            flags = flags or Context.BIND_ALLOW_ACTIVITY_STARTS
        }
        val ok = try {
            context.bindService(intent, connection, flags)
        } catch (t: Throwable) {
            LinkLog.w("OsmAnd bindService: ${t.message}")
            boundPkg = null
            false
        }
        LinkLog.i("OsmAnd bindService($pkg)=$ok")
    }

    private fun unsubscribeNav() {
        val iface = aidl ?: return
        if (navCallbackId >= 0L) {
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
        if (voiceCallbackId >= 0L) {
            try {
                val params = ANavigationVoiceRouterMessageParams().apply {
                    setSubscribeToUpdates(false)
                    setCallbackId(voiceCallbackId)
                }
                iface.registerForVoiceRouterMessages(params, aidlCallback)
            } catch (t: Throwable) {
                LinkLog.w("OsmAnd unsubscribe voice: ${t.message}")
            }
            voiceCallbackId = -1L
        }
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
