package slate.app.host

import android.content.Context
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import slate.app.apps.ClockApp
import slate.app.apps.LauncherApp
import slate.app.apps.NotificationsApp
import slate.app.apps.TestApp
import slate.app.camera.CameraPreviewSession
import slate.app.link.LinkContention
import slate.app.link.LinkLog
import slate.app.link.PhoneId
import slate.app.link.SharedLink
import slate.app.link.SlateGattClient
import slate.app.location.LocationAdapter
import slate.app.map.MapAdapter
import slate.app.nav.NavAdapter
import slate.app.repo.InstalledStore
import slate.app.repo.RepoPrefs
import slate.app.settings.WatchSettingsStore
import slate.app.notif.NotifChange
import slate.app.notif.NotifPrefs
import slate.app.notif.NotifStore
import slate.app.notif.toJsonArray
import slate.app.script.AndroidJsEngine
import slate.app.script.ScriptRuntimeHost
import slate.script.ScriptConsole
import slate.compositor.Compositor
import slate.compositor.FocusReason
import slate.compositor.StackOp
import slate.dsl.displayList
import slate.frame.SdpFrame
import slate.host.HostOutbound
import slate.host.PriorityClass
import slate.input.InputEventDecoder
import slate.notif.SystemNotifCodec
import slate.session.ConfirmStatus
import slate.session.SessionClient
import slate.generated.SdpWire
import slate.session.TimeSync
import slate.session.WatchSettings
import java.util.TimeZone
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

    private val controlListener: (ByteArray) -> Unit = { msg ->
        // GATT callbacks arrive on a binder thread; keep session/compositor
        // on the link scope so they never race Main or each other.
        val copy = msg.copyOf()
        scope.launch { onControlMessage(copy) }
    }
    private val inputListener: (ByteArray) -> Unit = { msg ->
        val copy = msg.copyOf()
        scope.launch { onInputMessage(copy) }
    }

    val compositor = Compositor(
        nowMs = { System.currentTimeMillis() },
        pushToWatch = { bytes ->
            if (SharedLink.benchmarkPaused) {
                LinkLog.w("pushToWatch dropped: benchmark paused")
                return@Compositor false
            }
            if (session.state != SessionClient.State.Ready) {
                LinkLog.w("pushToWatch dropped: session=${session.state}")
                return@Compositor false
            }
            // Drop an identical re-push arriving within the coalesce window.
            // Focusing an app emitted the same list three times in ~33 ms
            // (onFocus, onRender and the scheduler flush), which is six
            // messages against the watch's 20 ms drain — most were discarded
            // by the single-slot inbox, and whether the screen appeared came
            // down to which one survived.
            val now = System.currentTimeMillis()
            val digest = bytes.contentHashCode()
            if (digest == lastPushDigest && now - lastPushAtMs < PUSH_COALESCE_MS) {
                LinkLog.i("pushToWatch: skipped duplicate ${bytes.size} B list")
                return@Compositor true
            }
            lastPushDigest = digest
            lastPushAtMs = now

            // No credit gate here. One was tried (P-8) and removed: it never
            // once delivered a held list, because presses are always further
            // apart than its timeout — but it did swallow the press that
            // happened to land inside the window, so a screen opened only on
            // the *second* attempt. What actually made pushes reliable is the
            // 250 ms inter-message gap in SlateGattClient, which stops the
            // pre-display CONTROL from holding the watch's single-slot inbox
            // when the list arrives behind it.
            sendListNow(bytes)
        },
        onAdapterCommand = { appId, cmd -> handleAdapter(appId, cmd) },
        onPushDropped = { appId, reason, bytes ->
            LinkLog.w("pushToWatch DROPPED: $appId, $bytes B — $reason")
        },
        onScreenStackOp = { op ->
            session.scheduleDisplay(
                when (op) {
                    StackOp.Push -> SessionClient.PendingDisplay.Push
                    StackOp.Replace -> SessionClient.PendingDisplay.Replace
                },
            )
        },
        onScreenPop = {
            // Phone SessionClient stays Ready after HELLO (no Active state).
            // Still guard only on a live session — a pop after link-down is noise.
            if (session.state != SessionClient.State.Disconnected) {
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

    /**
     * The app drawer. Its list is rebuilt on every focus from the installed
     * store, so a package installed mid-session shows up without a restart.
     * Only JS sub-apps live in that store — Kotlin apps are registered in code
     * and are deliberately not launchable from the watch.
     */
    // Sub-apps are reached from the watch launcher and managed in the
    // repository screen — there are deliberately no per-sub-app open helpers
    // here any more. One method per sub-app meant a code change was needed
    // before a newly installed one could be opened at all, which is backwards
    // for downloaded apps.
    private val launcher = LauncherApp {
        val prefs = RepoPrefs(context)
        InstalledStore.create(context).list()
            .filter { prefs.showsInLauncher(it.id) }
            .map { LauncherApp.Entry(id = it.id, name = it.manifest().name) }
    }
    private val scripts = ScriptRuntimeHost(context, scope, compositor)

    private val serviceLifecycle = ServiceLifecycleOwner()
    private var navAdapter: NavAdapter? = null
    private var cameraSession: CameraPreviewSession? = null
    private var navSubscribed = false

    // Location serves any sub-app, so unlike nav/camera the subscriber has to
    // be remembered rather than assumed. Null means nothing is listening and
    // the GPS is not being held on by us.
    private var locationAdapter: LocationAdapter? = null
    private var locationSubscriberId: String? = null

    // The map holds its own location subscription rather than sharing the one
    // above: the two are independent features and only one sub-app holds focus
    // at a time, so entangling their lifecycles would buy nothing.
    private var mapAdapter: MapAdapter? = null
    private var mapSubscriberId: String? = null

    /** Last display list pushed, for duplicate coalescing. */
    private var lastPushDigest: Int = 0
    private var lastPushAtMs: Long = 0L
    /**
     * Swipe-to-launcher while GATT is up but HELLO has not finished (Connected).
     * [pushToWatch] drops until Ready, so without this the gesture is a silent
     * no-op until the user disconnects — classic post-OTA symptom.
     */
    @Volatile
    private var pendingLauncherOpen = false

    /**
     * Write the pre-display CONTROL and then the display list.
     *
     * The pair shares the watch's single-slot inbox, which is why the write
     * pump spaces messages by 250 ms — see [SlateGattClient].
     */
    private fun sendListNow(bytes: ByteArray): Boolean {
        LinkLog.i("pushToWatch: ${bytes.size} B display list")
        val pre = session.takePreDisplayControl()
        val op = pre[0].toInt() and 0xFF
        val depth = compositor.stackSnapshot.size
        // The watch AppInbox is one slot. A pre-display CONTROL that sits there
        // through a face/diag paint (~230–900 ms) causes the following DISPLAY
        // to be dropped — companion logs a successful write, watch never paints.
        // Firmware already treats the first DISPLAY at remote_depth==0 as Push,
        // and defaults pending_ to Replace after every apply, so most CONTROL
        // prefixes are redundant. Only send SCREEN_PUSH when stacking on an
        // existing remote screen (depth > 1).
        val sendPre = op == SdpWire.ControlOp.SCREEN_PUSH && depth > 1
        if (sendPre) {
            gatt.sendMessage(SdpFrame.CHAN_CONTROL, pre)
        } else {
            LinkLog.i(
                "pushToWatch: skip pre-display CONTROL op=0x${op.toString(16)} " +
                    "stackDepth=$depth",
            )
        }
        return gatt.pushDisplayList(bytes)
    }

    /** Wall-clock of the last TIME_SYNC sent, for the periodic resend. */
    private var lastTimeSyncMs = 0L

    /** Phone's copy of the watch settings; see [slate.session.WatchSettings]. */
    val watchSettings = WatchSettingsStore.get(context)

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
        compositor.register(launcher)
        scope.launch {
            try {
                scripts.ensureSeeded()
                scripts.ensureTimerRegistered()
                LinkLog.i("JS packages seeded; timer ready; render IPC≈${scripts.lastRenderIpcMs}ms")
            } catch (t: Throwable) {
                ScriptConsole.log("slate.runtime", "error", "JS sandbox: ${t.message}")
                LinkLog.e("JS sandbox failed", t)
                val brick = t.message.orEmpty().contains("sandbox", ignoreCase = true) ||
                    t.message.orEmpty().contains("already bound", ignoreCase = true)
                if (brick) {
                    LinkLog.w("JS sandbox seed brick — forceReset + retry")
                    runCatching { scripts.resetRuntime() }
                    try {
                        scripts.ensureSeeded()
                        scripts.ensureTimerRegistered()
                        LinkLog.i(
                            "JS packages seeded after reset; " +
                                "render IPC≈${scripts.lastRenderIpcMs}ms",
                        )
                    } catch (t2: Throwable) {
                        ScriptConsole.log(
                            "slate.runtime",
                            "error",
                            "JS sandbox still down: ${t2.message}",
                        )
                        LinkLog.e("JS sandbox failed after reset", t2)
                    }
                }
            }
        }
        // A settings edit made anywhere in the process — the phone's settings
        // screen is a separate Activity — goes out as soon as there is a session
        // to carry it. Applying an inbound change also moves this flow, but
        // takePending() is empty in that case, so the watch is not answered back.
        scope.launch {
            watchSettings.settings.collect { sendSettingsSync() }
        }
        scope.launch {
            gatt.metrics.collect { m ->
                val wasConnected = compositor.linkConnected
                compositor.linkConnected = m.connected
                if (m.connected) {
                    if (!wasConnected) {
                        session.onLinkUp()
                    }
                    // No ambient base. ClockApp used to sit at the bottom of
                    // the stack, so relinquishing the last sub-app focused it
                    // and pushed its small-digit clock over the watch's own
                    // face (N-34) — visible as soon as exiting a sub-app
                    // started working reliably.
                    //
                    // It was reinstated on a correlation nobody could explain:
                    // pushes stopped landing when it was removed. Those causes
                    // are now known and fixed — the TWIM SHORTS constants
                    // (N-31), the sandbox crash (N-38) and the stale stack
                    // (N-39) — so the correlation no longer justifies it.
                    //
                    // With an empty stack the watch falls back to its own local
                    // face, which is the big-digit one and is what should be
                    // underneath a sub-app anyway (N-27 local_back).
                    //
                    // If display pushes stop landing again — dl_ok flat on diag
                    // line 3 when a sub-app is opened — the ambient base did
                    // matter and this is the change to revert.
                    startTicker()
                    startNotifBridge()
                } else {
                    stopTicker()
                    stopConfirmPoll()
                    _confirmUi.value = ConfirmUi.Idle
                    SharedLink.publishConfirmUi(ConfirmUi.Idle)
                    if (wasConnected) {
                        pendingLauncherOpen = false
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
        // Leaving a location listener registered past the service outlives the
        // sub-app that asked for it and keeps the GPS warm for nobody.
        stopLocation()
        stopMap()
        serviceLifecycle.destroy()
        scripts.close()
        // Close the shared V8 host too — otherwise a sticky restart can leave
        // androidx's bind gate shut with no handle (N-51 after OTA / FGS churn).
        runBlocking(Dispatchers.IO) {
            AndroidJsEngine.forceReset()
        }
        _confirmUi.value = ConfirmUi.Idle
        SharedLink.publishConfirmUi(ConfirmUi.Idle)
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
        onSettingsSync(msg)
        val result = session.onControlMessage(msg)
        for (out in result.outbound) {
            gatt.sendMessage(SdpFrame.CHAN_CONTROL, out)
        }
        session.helloOffer?.let { offer ->
            compositor.watchProtocolVersion = offer.protocolVersion
        }
        // Only a positive advertisement refreshes the window. The watch sends
        // its full buffer at depth 0 and zero while a screen is up, and zeroing
        // here would stall every push until the user went back to the face.
        // The zero case is handled by releasing credit when a screen goes away
        // (Compositor.releaseCredit) rather than by trusting this number.
        if (session.freeCreditBytes > 0) {
            compositor.setCredit(session.freeCreditBytes)
        }
        // Keep the watch clock set. The watch has no RTC battery and boots at
        // 1970, so an unsynced watch shows 00:00 forever — this is the only
        // thing that sets it until GATT CTS lands (I-12). Sending only on the
        // Ready edge meant one missed edge left the clock wrong for the whole
        // session, so also resend periodically; CONTROL traffic (heartbeats)
        // drives this often enough that no timer is needed.
        val nowMs = System.currentTimeMillis()
        if (session.state == SessionClient.State.Ready &&
            (!wasReady || nowMs - lastTimeSyncMs >= TIME_RESYNC_INTERVAL_MS)
        ) {
            lastTimeSyncMs = nowMs
            if (!wasReady) {
                // N-25: do NOT send on the Ready edge. HELLO_ACCEPT has just
                // gone out, and the watch's AppInbox holds exactly one message
                // (N-8) — anything arriving before the app task drains (20 ms)
                // is dropped with no retransmit, because these are writes
                // without response. The clock then stayed at 1970 for the whole
                // session. Send after the handshake has been consumed, and
                // repeat: the cost of a redundant 5-byte write is nothing next
                // to another 15-minute wait for the periodic resync.
                scope.launch {
                    for (delayMs in TIME_SYNC_RETRY_DELAYS_MS) {
                        delay(delayMs)
                        if (session.state != SessionClient.State.Ready) return@launch
                        sendTimeSync()
                    }
                }
            } else {
                sendTimeSync()
            }
        }
        if (!wasReady && session.state == SessionClient.State.Ready) {
            openSettingsExchange()
            if (pendingLauncherOpen) {
                pendingLauncherOpen = false
                LinkLog.i("openLauncher: flushing deferred open (session now Ready)")
                scope.launch { openLauncher() }
            }
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

    /**
     * CONTROL 0x20 — firmware wall_clock path (pre-CTS).
     *
     * Carries **local** wall-clock, not UTC. The watch has no timezone
     * database and renders the value it is given straight, so sending a UTC
     * epoch put the face four hours behind in UTC+4. This matches CTS, which
     * is also local time — hence `apply_cts_sync` on the firmware side.
     * Recomputed on every send, so DST and travel are picked up by the
     * periodic resync.
     */
    fun sendTimeSync(nowMillis: Long = System.currentTimeMillis()) {
        if (session.state != SessionClient.State.Ready) return
        val offsetMs = TimeZone.getDefault().getOffset(nowMillis)
        val localEpoch = (nowMillis + offsetMs) / 1000L
        LinkLog.i(
            "time sync → watch: local epoch=$localEpoch " +
                "(utc=${nowMillis / 1000L}, offset=${offsetMs / 60000}min, " +
                "${TimeZone.getDefault().id})",
        )
        gatt.sendMessage(SdpFrame.CHAN_CONTROL, TimeSync.encodeUnix(localEpoch))
    }

    /**
     * CONTROL 0x21 in — the watch reporting its settings.
     *
     * Runs before [SessionClient.onControlMessage] rather than inside it: this
     * is app state, not session state, and the session client is shared with the
     * host tests where no settings store exists.
     */
    private fun onSettingsSync(msg: ByteArray) {
        if (msg.isEmpty() || (msg[0].toInt() and 0xFF) != WatchSettings.OP) return
        val incoming = WatchSettings.decode(msg) ?: run {
            LinkLog.w("settings sync: undecodable ${msg.size} B message from watch")
            return
        }
        if (watchSettings.onRemote(incoming)) {
            LinkLog.i(
                "settings ← watch (rev ${incoming.revision}): " +
                    "raise=${incoming.tiltEnabled} timeout=${incoming.wakeSeconds}s " +
                        "steps=${incoming.showSteps} diag=${incoming.showDiag} " +
                        "hr=${incoming.hrEnabled}",
            )
        } else {
            // Ours is newer or identical. Either way the watch has now told us
            // where it stands; if we are genuinely ahead, push so it converges.
            sendSettingsSync()
        }
    }

    /**
     * Hand our copy to the watch.
     *
     * Fire and forget: with no session the edit stays on the phone and
     * [WatchSettingsStore.pendingSend] keeps it queued for the next connection,
     * mirroring the firmware's `take_settings_dirty()`.
     */
    fun sendSettingsSync(force: Boolean = false) {
        if (session.state != SessionClient.State.Ready) return
        val pending = watchSettings.takePending()
        if (pending == null && !force) return
        val p = pending ?: watchSettings.current()
        LinkLog.i(
            "settings → watch (rev ${p.revision}): raise=${p.tiltEnabled} " +
                "timeout=${p.wakeSeconds}s steps=${p.showSteps} diag=${p.showDiag} " +
                "hr=${p.hrEnabled}",
        )
        gatt.sendMessage(SdpFrame.CHAN_CONTROL, WatchSettings.encode(p))
    }

    /**
     * Open the settings exchange on a fresh session.
     *
     * The watch only speaks first when it has an unsent edit, so if the phone
     * stayed quiet two sides that had both changed would sit on different values
     * indefinitely. The phone opens; the merge rule decides whose copy wins and
     * the watch answers with its own if ours is not newer.
     *
     * Delayed for the same reason as the time sync (N-25): HELLO_ACCEPT has just
     * gone out and the watch's AppInbox holds exactly one message, so anything
     * arriving before the app task drains is dropped with no retransmit.
     */
    private fun openSettingsExchange() {
        scope.launch {
            delay(SETTINGS_OPEN_DELAY_MS)
            if (session.state != SessionClient.State.Ready) return@launch
            sendSettingsSync(force = true)
        }
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
        // Log the edge: previously the only trace of a confirmed image was the
        // confirm poll going quiet, which reads exactly like the watch having
        // stopped answering.
        val prev = SharedLink.lastConfirmStatus
        if (prev == null || prev.needsConfirm != snap.needsConfirm) {
            if (snap.needsConfirm) {
                LinkLog.i("watch image ON TRIAL — ${snap.dwellMsRemaining}ms dwell remaining")
            } else {
                LinkLog.i("watch image CONFIRMED (IMAGE_OK written)")
            }
        }
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
            // Swipe right-to-left is a reserved system gesture: it opens the
            // launcher from wherever you are, so it is claimed here rather
            // than offered to the focused sub-app. Everything else goes
            // straight through.
            //
            // Deliberately idempotent. This used to skip the open when the
            // launcher was already the focused app, which made the gesture a
            // one-shot: if the push had been rejected, or the watch had
            // rebooted and reset to its own face, the host still believed the
            // launcher was up and swallowed every further swipe. Re-focusing
            // an already-focused app re-sends its screen, which is exactly the
            // recovery wanted here.
            if (input.op == SdpWire.InputOp.SWIPE &&
                input.dir == SdpWire.SwipeDir.LEFT
            ) {
                LinkLog.i("swipe LEFT — opening launcher " +
                    "(focused=${compositor.focusedAppId})")
                openLauncher()
                return@launch
            }
            if (input.op == SdpWire.InputOp.SWIPE) {
                // Every swipe that is not the launcher gesture, so a direction
                // that never arrives is visible in the log rather than silent.
                LinkLog.i("swipe dir=${input.dir} — passed to focused app")
            }
            compositor.dispatchInput(input)
            // A tapped row cannot focus another app itself — sub-apps have no
            // such authority — so it parks the id and the host acts on it.
            launcher.takePendingLaunch()?.let { launchFromLauncher(it) }
        }
    }

    /**
     * Tell the user on the watch that a sub-app did not start.
     *
     * Without this the tap does nothing at all: the launcher closes, no screen
     * arrives, and the watch is left on whatever was underneath. "Nothing
     * happened" is indistinguishable from a dropped push, and it is the exact
     * ambiguity N-47 was filed about on the display path.
     */
    private suspend fun showLaunchFailure(appId: String, cause: Throwable) {
        val name = try {
            InstalledStore.create(context).get(appId)?.manifest()?.name ?: appId
        } catch (_: Throwable) {
            appId
        }
        val bytes = displayList {
            palette(0, slate.wire.Colors.BLACK)
            palette(1, slate.wire.Colors.WHITE)
            palette(2, slate.wire.rgb(0xFD20))
            clear(slate.wire.pal(0))
            textScaled(
                font = 1, x = 120, y = 78, align = SdpWire.Align.CENTER,
                color = slate.wire.pal(2), scale = 2, text = "Did not start",
            )
            textScaled(
                font = 1, x = 120, y = 110, align = SdpWire.Align.CENTER,
                color = slate.wire.pal(1), scale = 2, text = name.take(16),
            )
            textScaled(
                font = 1, x = 120, y = 140, align = SdpWire.Align.CENTER,
                color = slate.wire.pal(1), scale = 1, text = "See the phone log",
            )
            commit()
        }
        compositor.pushSystemScreen(bytes)
    }

    /** Swipe-left, or the phone-side button. */
    suspend fun openLauncher() {
        // metrics and the compositor flag can briefly disagree after a reconnect
        // race; prefer the live GATT reading so a Ready session is not denied.
        if (!compositor.linkConnected && gatt.metrics.value.connected) {
            LinkLog.w("openLauncher: repairing linkConnected from GATT metrics")
            compositor.linkConnected = true
        }
        if (!compositor.linkConnected) {
            LinkLog.w(
                "openLauncher: linkConnected=false " +
                    "(session=${session.state}) — watch may have shown Not connected",
            )
        }
        // Display lists only go out in Ready. Remember the gesture so the
        // Ready edge can open the drawer instead of requiring a reconnect.
        if (session.state != SessionClient.State.Ready) {
            pendingLauncherOpen = true
            LinkLog.i(
                "openLauncher: deferred until Ready " +
                    "(session=${session.state}, linkConnected=${compositor.linkConnected})",
            )
            return
        }
        pendingLauncherOpen = false
        val already = compositor.focusedAppId == LauncherApp.APP_ID
        val deny = compositor.requestFocus(
            LauncherApp.APP_ID,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            if (already) StackOp.Replace else StackOp.Push,
        )
        if (deny != null) {
            LinkLog.i("openLauncher: DENIED — $deny (session=${session.state})")
        }
    }

    /**
     * Replace the launcher with the chosen sub-app rather than stacking on it,
     * so BACK from the sub-app returns to the watch face, not to the drawer.
     *
     * Registration is allowed to fail. A sub-app is downloaded JavaScript and
     * its runtime is a bindable system service that can be reclaimed; neither
     * is reliable enough to put the BLE link behind. This used to be unguarded,
     * so a failure inside `ensureRegistered` propagated out of the coroutine in
     * [onInputMessage] and killed the companion — the watch then sat on the
     * sub-app's retained screen while the phone came back knowing nothing about
     * it, which is the state the operator hit needing a reconnect.
     */
    private suspend fun launchFromLauncher(appId: String) {
        try {
            scripts.ensureRegistered(appId)
        } catch (t: Throwable) {
            val msg = t.message.orEmpty()
            val sandboxBrick =
                msg.contains("sandbox", ignoreCase = true) ||
                    msg.contains("already bound", ignoreCase = true)
            if (sandboxBrick) {
                LinkLog.w("launcher: $appId sandbox fault — reset + retry (${t.message})")
                scripts.evict(appId)
                runCatching { scripts.resetRuntime() }
                try {
                    scripts.ensureRegistered(appId)
                } catch (t2: Throwable) {
                    LinkLog.e("launcher: $appId failed to start after sandbox reset", t2)
                    ScriptConsole.log(appId, "error", "failed to start: ${t2.message}")
                    showLaunchFailure(appId, t2)
                    return
                }
            } else {
                LinkLog.e("launcher: $appId failed to start", t)
                ScriptConsole.log(appId, "error", "failed to start: ${t.message}")
                showLaunchFailure(appId, t)
                return
            }
        }
        val deny = compositor.requestFocus(
            appId,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Replace,
        )
        LinkLog.i("launcher -> $appId" + if (deny != null) " DENIED: $deny" else "")
        // Focus can succeed while the app yields nothing to draw — a script
        // that threw, or one whose list never got past maybePush. Without this
        // the log shows a successful launch and the watch shows the old screen.
        if (deny == null && compositor.focusedAppId == appId &&
            !compositor.lastPushSucceededFor(appId)
        ) {
            LinkLog.w("launcher -> $appId focused but produced NO display list")
        }
    }

    suspend fun openTestApp() {
        // Timestamped either side of requestFocus: the press itself was never
        // logged, so "nothing is happening" could not be distinguished from
        // "the push has not been issued yet".
        val t0 = System.currentTimeMillis()
        LinkLog.i(
            "openTestApp: requesting focus " +
                "(session=${session.state}, linkConnected=${compositor.linkConnected})",
        )
        val deny = compositor.requestFocus(
            test.manifest.id,
            PriorityClass.NORMAL,
            FocusReason.UserNavigation,
            StackOp.Push,
        )
        LinkLog.i(
            "openTestApp: focus returned after ${System.currentTimeMillis() - t0}ms" +
                if (deny != null) " — DENIED: $deny" else "",
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
        // resetStack() below drops every sub-app's screen, so the app that
        // subscribed can no longer draw a fix and has no way to unsubscribe.
        // Holding the GPS on for it would be a battery leak triggered by a
        // dropped connection (N-39 was the same class of bug: host state
        // surviving a link the watch had already forgotten).
        stopLocation()
        // The watch reverts to its local face on disconnect, so anything left
        // on the host's stack is stale the moment the link drops.
        scope.launch { compositor.resetStack() }
    }

    private fun startNotifBridge() {
        if (notifJob?.isActive == true) return
        notifJob = scope.launch {
            // Wait for session Ready (post-accept CREDIT), not merely GATT up.
            // A 3 s timer from connect still overlapped HELLO and dumped ~30
            // SYSTEM upserts ahead of any DISPLAY in the FIFO write queue.
            while (compositor.linkConnected &&
                session.state != SessionClient.State.Ready
            ) {
                delay(100L)
            }
            if (!compositor.linkConnected ||
                session.state != SessionClient.State.Ready
            ) {
                return@launch
            }
            delay(NOTIF_SYNC_DEFER_MS)
            if (!compositor.linkConnected ||
                session.state != SessionClient.State.Ready
            ) {
                return@launch
            }
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

    private fun handleAdapter(appId: String, cmd: HostOutbound.AdapterCommand) {
        when (cmd.adapter) {
            "notifications" -> handleNotifAdapter(cmd)
            "nav" -> handleNavAdapter(cmd)
            "camera" -> handleCameraAdapter(cmd)
            "haptic" -> handleHaptic(cmd)
            "phone" -> handlePhoneAdapter(cmd)
            "location" -> handleLocationAdapter(appId, cmd)
            "map" -> handleMapAdapter(appId, cmd)
            else -> Unit
        }
    }

    /**
     * The OSM vector map.
     *
     * The host owns the screen while this is running: [MapAdapter] renders the
     * map and pushes it with [Compositor.pushHostDisplayList], the same
     * privileged path the camera preview uses. The sub-app draws status screens
     * only, and declares `refreshPolicy: "manual"` so the compositor does not
     * repaint it over the map.
     *
     * There is no refresh command to handle, by design — see `slate.map` in
     * shared-js/slate_host.js. The companion decides when to redraw.
     */
    private fun handleMapAdapter(appId: String, cmd: HostOutbound.AdapterCommand) {
        when (cmd.command) {
            "subscribe" -> {
                val radius = try {
                    JSONObject(cmd.payloadJson).optDouble("radiusM", MapAdapter.DEFAULT_RADIUS_M)
                } catch (_: Throwable) {
                    MapAdapter.DEFAULT_RADIUS_M
                }
                stopMap()
                mapSubscriberId = appId
                mapAdapter = MapAdapter(
                    context = context,
                    scope = scope,
                    onDisplayList = { bytes ->
                        val target = mapSubscriberId ?: return@MapAdapter
                        scope.launch { compositor.pushHostDisplayList(target, bytes) }
                    },
                    onStatus = { json ->
                        val target = mapSubscriberId ?: return@MapAdapter
                        scope.launch { compositor.dispatchSystemEvent(target, "map", json) }
                    },
                ).also { it.start(radius) }
                LinkLog.i("map.subscribe for $appId r=${radius.toInt()}m")
            }
            "unsubscribe" -> {
                if (mapSubscriberId == appId || mapSubscriberId == null) {
                    stopMap()
                    LinkLog.i("map.unsubscribe for $appId")
                }
            }
        }
    }

    private fun stopMap() {
        mapAdapter?.stop()
        mapAdapter = null
        mapSubscriberId = null
    }

    /**
     * The phone's position, for whichever sub-app asked.
     *
     * Unlike nav and camera, this is not bound to one app id: the subscriber is
     * whoever issued the command, so any sub-app holding the `location`
     * permission can use it. The permission check itself already happened —
     * `BindingSurface.filterOutbound` drops a location command from an app that
     * does not declare it, so a command arriving here is authorised.
     *
     * One subscriber at a time. A second subscribe replaces the first rather
     * than running two listeners, and the previous app is told it lost the
     * stream instead of simply going quiet.
     */
    private fun handleLocationAdapter(appId: String, cmd: HostOutbound.AdapterCommand) {
        when (cmd.command) {
            "subscribe", "request" -> {
                val o = try {
                    JSONObject(cmd.payloadJson)
                } catch (_: Throwable) {
                    JSONObject()
                }
                if (locationSubscriberId != null && locationSubscriberId != appId) {
                    emitLocationStatus(locationSubscriberId!!, "unavailable")
                }
                stopLocation()
                locationSubscriberId = appId
                val adapter = LocationAdapter(context) { json ->
                    val target = locationSubscriberId ?: return@LocationAdapter
                    scope.launch {
                        compositor.dispatchSystemEvent(target, "location", json)
                    }
                }
                locationAdapter = adapter
                val status = if (cmd.command == "request") {
                    adapter.requestSingle()
                } else {
                    adapter.subscribe(
                        minIntervalMs = o.optLong("minIntervalMs", 5000L),
                        minDistanceM = o.optDouble("minDistanceM", 0.0).toFloat(),
                    )
                }
                LinkLog.i("location.${cmd.command} for $appId -> ${status.wire}")
                // A terminal state means no fix is ever coming; do not leave a
                // listener and a subscriber id behind pretending otherwise.
                if (status != LocationAdapter.Status.Searching) stopLocation()
            }
            "unsubscribe" -> {
                if (locationSubscriberId == appId || locationSubscriberId == null) {
                    stopLocation()
                    LinkLog.i("location.unsubscribe for $appId")
                }
            }
        }
    }

    private fun emitLocationStatus(appId: String, state: String) {
        val json = JSONObject().put("type", "status").put("state", state).toString()
        scope.launch { compositor.dispatchSystemEvent(appId, "location", json) }
    }

    private fun stopLocation() {
        locationAdapter?.stop()
        locationAdapter = null
        locationSubscriberId = null
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

    /**
     * Vibrate the phone on behalf of a sub-app.
     *
     * Deliberately not the same thing as [handleHaptic], which pushes a HAPTIC
     * op to the watch motor. This one never touches the link — it is the only
     * sub-app action so far whose whole effect is on the handset, so it works
     * with the watch disconnected.
     *
     * Duration is clamped in the JS binding and again here: a sub-app must not
     * be able to buzz the phone indefinitely.
     */
    private fun handlePhoneAdapter(cmd: HostOutbound.AdapterCommand) {
        if (cmd.command != "vibrate") return
        val ms = try {
            JSONObject(cmd.payloadJson).optInt("ms", 150)
        } catch (_: Throwable) {
            150
        }.coerceIn(0, 2000)
        if (ms == 0) return
        val vibrator: Vibrator? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            (context.getSystemService(Context.VIBRATOR_MANAGER_SERVICE) as? VibratorManager)
                ?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
        }
        if (vibrator == null || !vibrator.hasVibrator()) {
            LinkLog.i("phone.vibrate: no vibrator on this device")
            return
        }
        vibrator.vibrate(
            VibrationEffect.createOneShot(ms.toLong(), VibrationEffect.DEFAULT_AMPLITUDE),
        )
        LinkLog.i("phone.vibrate: ${ms}ms")
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


    private fun startTicker() {
        if (tickJob?.isActive == true) return
        tickJob = scope.launch {
            while (isActive) {
                if (!SharedLink.benchmarkPaused) {
                    compositor.tick()
                    // Safety net if the Ready-edge flush was missed (e.g. HELLO
                    // while a prior Ready state was already latched).
                    if (pendingLauncherOpen &&
                        session.state == SessionClient.State.Ready
                    ) {
                        pendingLauncherOpen = false
                        LinkLog.i("openLauncher: ticker flush of deferred open")
                        openLauncher()
                    }
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

        /**
         * Resend the wall clock this often while connected. The watch keeps
         * time on RTC1 once set, so this is drift correction and a safety net
         * for a missed Ready edge — not a per-tick cost.
         */
        const val TIME_RESYNC_INTERVAL_MS = 15 * 60 * 1000L

        /**
         * Window in which an identical display list is treated as a duplicate.
         * Only collapses the onFocus/onRender/flush burst (~33 ms). Must stay
         * well under a human re-swipe: 500 ms previously "succeeded" on the
         * phone while the watch dropped the first list, then skipped the
         * recovery swipe as a duplicate.
         */
        const val PUSH_COALESCE_MS = 80L

        /**
         * Quiet period after session Ready before bulk-syncing notifications.
         * Ready already means HELLO_ACCEPT was acknowledged (CREDIT); this
         * gap lets deferred launcher / time-sync CONTROL win the inbox.
         */
        const val NOTIF_SYNC_DEFER_MS = 1_500L

        /**
         * Cumulative delays after the session goes Ready, for the initial time
         * sync. Spaced past the watch's 20 ms drain so the message cannot be
         * swallowed by the single-slot inbox, then repeated twice in case it
         * still collides with a heartbeat or confirm-status reply.
         */
        val TIME_SYNC_RETRY_DELAYS_MS = longArrayOf(300L, 2_000L, 8_000L)

        /**
         * Delay before the opening SETTINGS_SYNC. Same single-slot-inbox reason
         * as the time sync, and offset from it so the two do not collide in the
         * one message the watch can hold.
         */
        const val SETTINGS_OPEN_DELAY_MS = 1_200L
    }
}
