package slate.app.host

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import android.content.Context
import slate.app.apps.ClockApp
import slate.app.apps.NotificationsApp
import slate.app.apps.TestApp
import slate.app.link.LinkLog
import slate.app.link.SharedLink
import slate.app.link.SlateGattClient
import slate.app.notif.NotifChange
import slate.app.notif.NotifPrefs
import slate.app.notif.NotifStore
import slate.app.notif.toJsonArray
import slate.app.script.ScriptRuntimeHost
import slate.compositor.Compositor
import slate.compositor.FocusReason
import slate.compositor.StackOp
import slate.frame.SdpFrame
import slate.host.HostOutbound
import slate.host.PriorityClass
import slate.notif.SystemNotifCodec
import slate.script.ScriptConsole

/**
 * Binds [Compositor] to the GATT link and notification bridge inside the FGS.
 */
class CompositorHost(
    private val context: Context,
    private val gatt: SlateGattClient,
    private val scope: CoroutineScope,
    private val notifPrefs: NotifPrefs,
) {
    val compositor = Compositor(
        nowMs = { System.currentTimeMillis() },
        pushToWatch = { bytes ->
            if (SharedLink.benchmarkPaused) return@Compositor false
            gatt.pushDisplayList(bytes)
        },
        onAdapterCommand = { cmd -> handleAdapter(cmd) },
    )

    private var tickJob: Job? = null
    private var notifJob: Job? = null
    private val clock = ClockApp()
    private val test = TestApp()
    private val notifications = NotificationsApp()
    private val scripts = ScriptRuntimeHost(context, scope, compositor)

    fun start() {
        compositor.register(clock)
        compositor.register(test)
        compositor.register(notifications)
        scope.launch {
            try {
                scripts.ensureTimerRegistered()
                LinkLog.i("JS timer registered; render IPC≈${scripts.lastRenderIpcMs}ms")
            } catch (t: Throwable) {
                ScriptConsole.log("slate.runtime", "error", "JS sandbox: ${t.message}")
                LinkLog.e("JS sandbox failed", t)
            }
        }
        scope.launch {
            gatt.metrics.collect { m ->
                compositor.linkConnected = m.connected
                if (m.connected) {
                    if (compositor.watchProtocolVersion < 1) {
                        compositor.watchProtocolVersion = 1
                    }
                    if (compositor.freeCreditBytes <= 0) {
                        compositor.setCredit(4096)
                    }
                    ensureAmbient()
                    startTicker()
                    startNotifBridge()
                } else {
                    stopTicker()
                }
            }
        }
    }

    fun stop() {
        stopTicker()
        notifJob?.cancel()
        notifJob = null
        scripts.close()
    }

    suspend fun openTimer() {
        scripts.ensureTimerRegistered()
        compositor.requestFocus(
            "slate.timer",
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    fun setWatchProtocolVersion(version: Int) {
        compositor.watchProtocolVersion = version
    }

    fun setCredit(freeBytes: Int) {
        compositor.setCredit(freeBytes)
    }

    suspend fun openTestApp() {
        compositor.requestFocus(
            test.manifest.id,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    suspend fun openNotifications() {
        pushNotifSnapshotToApp()
        compositor.requestFocus(
            notifications.manifest.id,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    private fun startNotifBridge() {
        if (notifJob?.isActive == true) return
        notifJob = scope.launch {
            // Initial sync
            syncAllToSystemChannel()
            pushNotifSnapshotToApp()
            NotifStore.changes.collect { change ->
                when (change) {
                    is NotifChange.Upserted -> {
                        val n = change.item
                        gatt.sendMessage(
                            SdpFrame.CHAN_SYSTEM,
                            SystemNotifCodec.encodeUpsert(
                                key = n.key,
                                category = n.icon.category.atlasId,
                                monogram = n.icon.monogram,
                                title = n.title,
                                text = n.text,
                                whenEpochSec = n.whenMs / 1000L,
                                ongoing = n.ongoing,
                                clearable = n.clearable,
                            ),
                        )
                        pushNotifSnapshotToApp()
                        maybeInterrupt(n.packageName, n.importance)
                    }
                    is NotifChange.Removed -> {
                        gatt.sendMessage(
                            SdpFrame.CHAN_SYSTEM,
                            SystemNotifCodec.encodeRemove(change.key),
                        )
                        pushNotifSnapshotToApp()
                    }
                    NotifChange.Cleared -> {
                        gatt.sendMessage(SdpFrame.CHAN_SYSTEM, SystemNotifCodec.encodeClearAll())
                        pushNotifSnapshotToApp()
                    }
                }
            }
        }
    }

    private suspend fun maybeInterrupt(packageName: String, importance: Int) {
        if (!notifPrefs.mayInterrupt(packageName, importance)) return
        if (!compositor.linkConnected) return
        val deny = compositor.requestFocus(
            notifications.manifest.id,
            PriorityClass.INTERRUPT,
            FocusReason.SystemRaise,
            StackOp.Push,
        )
        if (deny != null) {
            LinkLog.i("notif interrupt denied: $deny")
        }
    }

    private suspend fun pushNotifSnapshotToApp() {
        val json = JSONObject()
            .put("items", NotifStore.snapshot.value.toJsonArray())
            .toString()
        compositor.dispatchSystemEvent(NotificationsApp.ID, NotificationsApp.SOURCE, json)
    }

    private fun syncAllToSystemChannel() {
        for (n in NotifStore.snapshot.value) {
            gatt.sendMessage(
                SdpFrame.CHAN_SYSTEM,
                SystemNotifCodec.encodeUpsert(
                    key = n.key,
                    category = n.icon.category.atlasId,
                    monogram = n.icon.monogram,
                    title = n.title,
                    text = n.text,
                    whenEpochSec = n.whenMs / 1000L,
                    ongoing = n.ongoing,
                    clearable = n.clearable,
                ),
            )
        }
    }

    private fun handleAdapter(cmd: HostOutbound.AdapterCommand) {
        if (cmd.adapter != "notifications") return
        when (cmd.command) {
            "action" -> {
                try {
                    val o = JSONObject(cmd.payloadJson)
                    val key = o.getString("key")
                    val actionId = o.getString("actionId")
                    if (!NotifStore.invokeAction(key, actionId)) {
                        LinkLog.w("notif action missed $key/$actionId")
                    }
                } catch (t: Throwable) {
                    LinkLog.e("notif adapter", t)
                }
            }
        }
    }

    private suspend fun ensureAmbient() {
        if (compositor.stackSnapshot.any { it.appId == clock.manifest.id }) return
        val deny = compositor.requestFocus(
            clock.manifest.id,
            PriorityClass.AMBIENT,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
        if (deny != null) {
            LinkLog.w("ambient clock focus denied: $deny")
        }
    }

    private fun startTicker() {
        if (tickJob?.isActive == true) return
        tickJob = scope.launch {
            while (isActive) {
                if (!SharedLink.benchmarkPaused) {
                    compositor.tick()
                }
                delay(TICK_MS)
            }
        }
    }

    private fun stopTicker() {
        tickJob?.cancel()
        tickJob = null
    }

    companion object {
        const val TICK_MS = 500L
    }
}
