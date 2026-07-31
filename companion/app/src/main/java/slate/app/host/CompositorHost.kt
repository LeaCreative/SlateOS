package slate.app.host

import android.content.Context
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import slate.app.apps.ClockApp
import slate.app.apps.NotificationsApp
import slate.app.apps.TestApp
import slate.app.camera.CameraPreviewSession
import slate.app.link.LinkContention
import slate.app.link.LinkLog
import slate.app.link.PhoneId
import slate.app.link.SharedLink
import slate.app.link.SlateGattClient
import slate.app.nav.NavAdapter
import slate.app.notif.NotifChange
import slate.app.notif.NotifPrefs
import slate.app.notif.NotifStore
import slate.app.notif.toJsonArray
import slate.app.script.ScriptRuntimeHost
import slate.compositor.Compositor
import slate.compositor.FocusReason
import slate.compositor.StackOp
import slate.dsl.displayList
import slate.frame.SdpFrame
import slate.host.HostOutbound
import slate.host.PriorityClass
import slate.input.InputEventDecoder
import slate.notif.SystemNotifCodec
import slate.script.ScriptConsole
import slate.session.ConfirmStatus
import slate.session.SessionClient
import slate.session.TimeSync
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Binds [Compositor] to the GATT link and notification / nav / camera bridges.
 */
class CompositorHost(
    private val context: Context,
    private val gatt: SlateGattClient,
    private val scope: CoroutineScope,
    private val notifPrefs: NotifPrefs,
) {
    private val hostVersion = run {
        try {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "0.1.0"
        } catch (_: Throwable) {
            "0.1.0"
        }
    }

    val session = SessionClient(
        phoneId = PhoneId.load(context),
        hostVersion = hostVersion,
    )

    private val controlListener: (ByteArray) -> Unit = { msg -> onControlMessage(msg) }
    private val inputListener: (ByteArray) -> Unit = { msg -> onInputMessage(msg) }

    val compositor = Compositor(
        nowMs = { System.currentTimeMillis() },
        pushToWatch = { bytes ->
            if (SharedLink.benchmarkPaused) return@Compositor false
            if (session.state != SessionClient.State.Ready) return@Compositor false
            gatt.sendMessage(SdpFrame.CHAN_CONTROL, session.takePreDisplayControl())
            gatt.pushDisplayList(bytes)
        },
        onAdapterCommand = { cmd -> handleAdapter(cmd) },
        onScreenStackOp = { op ->
            session.scheduleDisplay(
                when (op) {
                    StackOp.Push -> SessionClient.PendingDisplay.Push
                    StackOp.Replace -> SessionClient.PendingDisplay.Replace
                },
            )
        },
        onScreenPop = {
            if (session.state == SessionClient.State.Ready) {
                gatt.sendMessage(SdpFrame.CHAN_CONTROL, session.encodeScreenPop())
            }
        },
    )

    private var tickJob: Job? = null
    private var notifJob: Job? = null
    private var confirmPollJob: Job? = null
    private val clock = ClockApp()
    private val test = TestApp()
    private val notifications = NotificationsApp()
    private val scripts = ScriptRuntimeHost(context, scope, compositor)

    private val serviceLifecycle = ServiceLifecycleOwner()
    private var navAdapter: NavAdapter? = null
    private var cameraSession: CameraPreviewSession? = null
    private var navSubscribed = false

    private val _confirmUi = MutableStateFlow<ConfirmUi>(ConfirmUi.Idle)
    val confirmUi: StateFlow<ConfirmUi> = _confirmUi.asStateFlow()

    /** Phone-visible trial / IMAGE_OK state (mirrors CONTROL 0xE1). */
    sealed class ConfirmUi {
        data object Idle : ConfirmUi()
        data class OnTrial(val secondsRemaining: Int) : ConfirmUi()
        data object Confirmed : ConfirmUi()
        data object StuckWarning : ConfirmUi()
    }

    fun start() {
        gatt.addControlListener(controlListener)
        gatt.addInputListener(inputListener)
        compositor.register(clock)
        compositor.register(test)
        compositor.register(notifications)
        scope.launch {
            try {
                scripts.ensureSeeded()
                scripts.ensureTimerRegistered()
                LinkLog.i("JS packages seeded; timer ready; render IPC≈${scripts.lastRenderIpcMs}ms")
            } catch (t: Throwable) {
                ScriptConsole.log("slate.runtime", "error", "JS sandbox: ${t.message}")
                LinkLog.e("JS sandbox failed", t)
            }
        }
        scope.launch {
            gatt.metrics.collect { m ->
                val wasConnected = compositor.linkConnected
                compositor.linkConnected = m.connected
                if (m.connected) {
                    if (!wasConnected) {
                        session.onLinkUp()
                    }
                    ensureAmbient()
                    startTicker()
                    startNotifBridge()
                } else {
                    stopTicker()
                    stopConfirmPoll()
                    _confirmUi.value = ConfirmUi.Idle
                    SharedLink.publishConfirmUi(ConfirmUi.Idle)
                    if (wasConnected) {
                        session.onLinkDown()
                        onLinkLost()
                    }
                }
            }
        }
    }

    fun stop() {
        gatt.removeControlListener(controlListener)
        gatt.removeInputListener(inputListener)
        stopTicker()
        stopConfirmPoll()
        notifJob?.cancel()
        notifJob = null
        stopNav()
        stopCamera()
        serviceLifecycle.destroy()
        scripts.close()
        _confirmUi.value = ConfirmUi.Idle
        SharedLink.publishConfirmUi(ConfirmUi.Idle)
    }

    suspend fun openTimer() {
        scripts.ensureRegistered(ScriptRuntimeHost.TIMER_ID)
        compositor.requestFocus(
            ScriptRuntimeHost.TIMER_ID,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    suspend fun openNavigation() {
        scripts.ensureRegistered(ScriptRuntimeHost.NAV_ID)
        compositor.requestFocus(
            ScriptRuntimeHost.NAV_ID,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
    }

    suspend fun openCamera() {
        scripts.ensureRegistered(ScriptRuntimeHost.CAMERA_ID)
        compositor.requestFocus(
            ScriptRuntimeHost.CAMERA_ID,
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

    /** HEARTBEAT control payload while session is Ready (2 s interval). */
    fun sessionHeartbeat(): ByteArray? =
        if (session.state == SessionClient.State.Ready) session.encodeHeartbeat() else null

    /** Best-effort GOODBYE before tearing down GATT (intentional disconnect). */
    fun sendGoodbye() {
        if (session.state == SessionClient.State.Disconnected) return
        gatt.sendMessage(SdpFrame.CHAN_CONTROL, session.encodeGoodbye())
        session.onLinkDown()
    }

    private fun onControlMessage(msg: ByteArray) {
        val wasReady = session.state == SessionClient.State.Ready
        val result = session.onControlMessage(msg)
        for (out in result.outbound) {
            gatt.sendMessage(SdpFrame.CHAN_CONTROL, out)
        }
        session.helloOffer?.let { offer ->
            compositor.watchProtocolVersion = offer.protocolVersion
        }
        if (session.freeCreditBytes > 0) {
            compositor.setCredit(session.freeCreditBytes)
        }
        if (!wasReady && session.state == SessionClient.State.Ready) {
            sendTimeSync()
            val addr = SharedLink.associatedAddress
                ?: gatt.metrics.value.deviceAddress.takeIf { it.isNotBlank() }
            if (addr != null) {
                val v = LinkContention.checkInstant(
                    context,
                    addr,
                    weAreConnected = gatt.metrics.value.connected,
                )
                if (v.blocked) {
                    _confirmUi.value = ConfirmUi.StuckWarning
                    SharedLink.publishConfirmUi(ConfirmUi.StuckWarning)
                    return
                }
            }
            requestConfirmStatus()
            startConfirmPoll()
        }
        session.confirmStatus?.let { snap ->
            applyConfirmSnapshot(snap)
        }
    }

    /** CONTROL 0x20 unix epoch — firmware wall_clock path (pre-CTS). */
    fun sendTimeSync(epochSeconds: Long = System.currentTimeMillis() / 1000L) {
        if (session.state != SessionClient.State.Ready) return
        gatt.sendMessage(SdpFrame.CHAN_CONTROL, TimeSync.encodeUnix(epochSeconds))
    }

    fun requestConfirmStatus() {
        if (session.state != SessionClient.State.Ready) return
        gatt.sendMessage(SdpFrame.CHAN_CONTROL, session.encodeConfirmStatusRequest())
    }

    private fun startConfirmPoll() {
        if (confirmPollJob?.isActive == true) return
        val readyAt = System.currentTimeMillis()
        confirmPollJob = scope.launch {
            while (isActive) {
                delay(1_000)
                if (session.state != SessionClient.State.Ready) break
                requestConfirmStatus()
                val snap = session.confirmStatus
                if (snap == null) {
                    // HELLO Ready but no CONFIRM_STATUS yet (old FW) — keep Quiet.
                    if (System.currentTimeMillis() - readyAt > 15_000L &&
                        _confirmUi.value !is ConfirmUi.Confirmed
                    ) {
                        // Only warn if we already saw trial, or never got a reply
                        // after long Ready while still expecting confirm UX.
                        if (_confirmUi.value is ConfirmUi.OnTrial) {
                            _confirmUi.value = ConfirmUi.StuckWarning
                            SharedLink.publishConfirmUi(ConfirmUi.StuckWarning)
                        }
                    }
                    continue
                }
                applyConfirmSnapshot(snap)
                if (!snap.needsConfirm) {
                    // Leave Confirmed visible briefly; stop polling.
                    delay(3_000)
                    break
                }
                if (System.currentTimeMillis() - readyAt > 15_000L &&
                    snap.needsConfirm
                ) {
                    // HELLO Ready but trial never cleared — often another
                    // central stole the slot mid-dwell.
                    _confirmUi.value = ConfirmUi.StuckWarning
                    SharedLink.publishConfirmUi(ConfirmUi.StuckWarning)
                }
            }
        }
    }

    private fun stopConfirmPoll() {
        confirmPollJob?.cancel()
        confirmPollJob = null
    }

    private fun applyConfirmSnapshot(snap: ConfirmStatus.Snapshot) {
        SharedLink.lastConfirmStatus = snap
        if (_confirmUi.value is ConfirmUi.StuckWarning && snap.needsConfirm) {
            SharedLink.publishConfirmUi(ConfirmUi.StuckWarning)
            return
        }
        val ui = when {
            !snap.needsConfirm -> ConfirmUi.Confirmed
            else -> ConfirmUi.OnTrial(
                secondsRemaining = ((snap.dwellMsRemaining + 999L) / 1000L)
                    .toInt()
                    .coerceAtLeast(0),
            )
        }
        _confirmUi.value = ui
        SharedLink.publishConfirmUi(ui)
    }

    private fun onInputMessage(msg: ByteArray) {
        val input = InputEventDecoder.decode(msg) ?: return
        scope.launch {
            compositor.dispatchInput(input)
        }
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

    private fun onLinkLost() {
        navAdapter?.notifyDisconnected()
        stopCamera()
    }

    private fun startNotifBridge() {
        if (notifJob?.isActive == true) return
        notifJob = scope.launch {
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
        when (cmd.adapter) {
            "notifications" -> handleNotifAdapter(cmd)
            "nav" -> handleNavAdapter(cmd)
            "camera" -> handleCameraAdapter(cmd)
            "haptic" -> handleHaptic(cmd)
            else -> Unit
        }
    }

    private fun handleNotifAdapter(cmd: HostOutbound.AdapterCommand) {
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

    private fun handleNavAdapter(cmd: HostOutbound.AdapterCommand) {
        when (cmd.command) {
            "subscribe" -> {
                if (navAdapter == null) {
                    navAdapter = NavAdapter(context) { m ->
                        scope.launch {
                            compositor.dispatchSystemEvent(
                                ScriptRuntimeHost.NAV_ID,
                                "nav",
                                m.toJson(),
                            )
                        }
                    }
                }
                navAdapter?.start()
                navSubscribed = true
            }
            "unsubscribe" -> {
                navSubscribed = false
                stopNav()
            }
            "demo" -> {
                val kind = try {
                    JSONObject(cmd.payloadJson).optString("kind", "left")
                } catch (_: Throwable) {
                    "left"
                }
                if (navAdapter == null) {
                    handleNavAdapter(
                        HostOutbound.AdapterCommand("nav", "subscribe", "{}"),
                    )
                }
                navAdapter?.injectDemo(kind)
            }
        }
    }

    private fun handleCameraAdapter(cmd: HostOutbound.AdapterCommand) {
        when (cmd.command) {
            "start" -> {
                val o = try {
                    JSONObject(cmd.payloadJson)
                } catch (_: Throwable) {
                    JSONObject()
                }
                stopCamera()
                val session = CameraPreviewSession(
                    context = context,
                    lifecycleOwner = serviceLifecycle,
                    onPatchList = { bytes ->
                        scope.launch {
                            compositor.pushHostDisplayList(ScriptRuntimeHost.CAMERA_ID, bytes)
                        }
                    },
                    onStatus = { state, fps ->
                        scope.launch {
                            val json = JSONObject()
                                .put("type", "status")
                                .put("state", state)
                                .put("fpsHint", fps)
                                .toString()
                            compositor.dispatchSystemEvent(
                                ScriptRuntimeHost.CAMERA_ID,
                                "camera",
                                json,
                            )
                        }
                    },
                    onCaptured = {
                        scope.launch {
                            val json = JSONObject().put("type", "captured").toString()
                            compositor.dispatchSystemEvent(
                                ScriptRuntimeHost.CAMERA_ID,
                                "camera",
                                json,
                            )
                        }
                    },
                )
                session.slot = o.optInt("slot", 0)
                session.patchX = o.optInt("x", 90)
                session.patchY = o.optInt("y", 40)
                session.patchW = o.optInt("w", 60)
                session.patchH = o.optInt("h", 60)
                cameraSession = session
                session.start()
            }
            "stop" -> stopCamera()
            "capture" -> cameraSession?.captureStill()
        }
    }

    private fun handleHaptic(cmd: HostOutbound.AdapterCommand) {
        val pattern = try {
            JSONObject(cmd.payloadJson).optInt("pattern", 1)
        } catch (_: Throwable) {
            1
        }
        if (SharedLink.benchmarkPaused || !compositor.linkConnected) return
        gatt.pushDisplayList(
            displayList {
                haptic(pattern)
                commit()
            },
        )
    }

    private fun stopNav() {
        navAdapter?.stop()
        navAdapter = null
        navSubscribed = false
    }

    private fun stopCamera() {
        cameraSession?.close()
        cameraSession = null
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
