package slate.app.settings

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import slate.session.WatchSettings

/**
 * The phone's copy of the watch's settings, and the phone half of the sync.
 *
 * Persisted, because the pair spend most of their time disconnected: an edit
 * made on the phone with the watch out of range has to survive until the link
 * comes back, and the revision it was stamped with has to survive with it or
 * the edit silently loses the next merge.
 *
 * Two revisions are kept. [revision] is what our own copy carries;
 * [highestSeenRevision] is the largest we have seen from either side. The
 * second is what makes the counter a Lamport clock — see
 * [WatchSettings.nextRevision].
 */
class WatchSettingsStore(context: Context) {
    private val sp = context.applicationContext
        .getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private val _settings = MutableStateFlow(load().also { WatchUiTheme.apply(it) })
    val settings: StateFlow<WatchSettings.Payload> = _settings.asStateFlow()

    private val _pendingSend = MutableStateFlow(sp.getBoolean(KEY_DIRTY, false))

    /**
     * True when our copy has not yet been handed to the watch.
     *
     * Set by [edit], cleared only once a send is actually attempted with a
     * session up, so an edit made offline goes out on the next connection
     * rather than being lost.
     *
     * A flow rather than a flag because the settings screen shows it: the clear
     * happens on the link thread with no accompanying change to [settings], so a
     * plain field would leave the screen saying "waiting" forever.
     */
    val pendingSend: StateFlow<Boolean> = _pendingSend.asStateFlow()

    private var highestSeenRevision: Long = sp.getLong(KEY_HIGHEST_SEEN, 0L)

    /**
     * Apply a local edit: mutate, stamp a new revision, mark for sending.
     *
     * No-ops when nothing actually changed, so re-selecting the value already
     * shown does not burn a revision and does not provoke a needless round trip.
     */
    fun edit(mutate: (WatchSettings.Payload) -> WatchSettings.Payload): Boolean {
        val current = _settings.value
        val next = mutate(current)
        if (!WatchSettings.differs(current, next)) return false
        val rev = WatchSettings.nextRevision(current.revision, highestSeenRevision)
        val stamped = next.copy(revision = rev)
        if (rev > highestSeenRevision) highestSeenRevision = rev
        _pendingSend.value = true
        persist(stamped, dirty = true)
        WatchUiTheme.apply(stamped)
        // Published last: the link collects this flow and sends on change, and
        // it must not observe the new value before the dirty flag that authorises
        // sending it.
        _settings.value = stamped
        return true
    }

    /**
     * Merge an inbound SETTINGS_SYNC from the watch.
     *
     * Returns true when our copy changed, i.e. when the UI should redraw. The
     * merge rule itself lives in [WatchSettings] precisely so both ends run the
     * identical logic and reach the same verdict independently.
     */
    fun onRemote(incoming: WatchSettings.Payload): Boolean {
        if (incoming.revision > highestSeenRevision) {
            highestSeenRevision = incoming.revision
        }
        val current = _settings.value
        if (!WatchSettings.shouldApply(current, incoming, selfIsWatch = false)) {
            // Ours is newer, or the two are identical. If it is genuinely newer
            // the watch needs to hear it, so raise the flag for the sender.
            if (WatchSettings.differs(current, incoming)) _pendingSend.value = true
            persist(current, dirty = _pendingSend.value)
            return false
        }
        _pendingSend.value = false
        persist(incoming, dirty = false)
        WatchUiTheme.apply(incoming)
        _settings.value = incoming
        return true
    }

    /** Payload to put on the wire, or null when there is nothing outstanding. */
    fun takePending(): WatchSettings.Payload? {
        if (!_pendingSend.value) return null
        _pendingSend.value = false
        persist(_settings.value, dirty = false)
        return _settings.value
    }

    /**
     * Our copy, regardless of whether it is dirty.
     *
     * Used for the opening exchange on a fresh session: the watch only speaks
     * first when it has an edit to report, so the phone has to open, and the
     * merge rule sorts out whose copy is newer.
     */
    fun current(): WatchSettings.Payload = _settings.value

    private fun load(): WatchSettings.Payload = WatchSettings.Payload(
        revision = sp.getLong(KEY_REVISION, 0L),
        tiltEnabled = sp.getBoolean(KEY_TILT, true),
        wakeSeconds = sp.getInt(KEY_WAKE, 20),
        showSteps = sp.getBoolean(KEY_STEPS, true),
        showDiag = sp.getBoolean(KEY_DIAG, true),
        hrEnabled = sp.getBoolean(KEY_HR, false),
        uiChrome = sp.getInt(KEY_UI_CHROME, WatchSettings.DEFAULT_UI_CHROME),
        faceBright = sp.getInt(KEY_FACE_BRIGHT, WatchSettings.DEFAULT_FACE_BRIGHT),
        faceDim = sp.getInt(KEY_FACE_DIM, WatchSettings.DEFAULT_FACE_DIM),
    )

    private fun persist(p: WatchSettings.Payload, dirty: Boolean) {
        sp.edit()
            .putLong(KEY_REVISION, p.revision)
            .putBoolean(KEY_TILT, p.tiltEnabled)
            .putInt(KEY_WAKE, p.wakeSeconds)
            .putBoolean(KEY_STEPS, p.showSteps)
            .putBoolean(KEY_DIAG, p.showDiag)
            .putBoolean(KEY_HR, p.hrEnabled)
            .putInt(KEY_UI_CHROME, p.uiChrome)
            .putInt(KEY_FACE_BRIGHT, p.faceBright)
            .putInt(KEY_FACE_DIM, p.faceDim)
            .putLong(KEY_HIGHEST_SEEN, highestSeenRevision)
            .putBoolean(KEY_DIRTY, dirty)
            .apply()
    }

    companion object {
        private const val PREFS = "slate_watch_settings"
        private const val KEY_REVISION = "revision"
        private const val KEY_HIGHEST_SEEN = "highest_seen"
        private const val KEY_TILT = "tilt_enabled"
        private const val KEY_WAKE = "wake_seconds"
        private const val KEY_STEPS = "show_steps"
        private const val KEY_DIAG = "show_diag"
        private const val KEY_HR = "hr_enabled"
        private const val KEY_UI_CHROME = "ui_chrome"
        private const val KEY_FACE_BRIGHT = "face_bright"
        private const val KEY_FACE_DIM = "face_dim"
        private const val KEY_DIRTY = "dirty"

        @Volatile
        private var instance: WatchSettingsStore? = null

        /**
         * Process-wide singleton.
         *
         * The link service and the settings UI are different components in the
         * same process; two stores would each hold their own revision counter
         * and the two would diverge, which is exactly the failure the Lamport
         * counter exists to prevent.
         */
        fun get(context: Context): WatchSettingsStore {
            instance?.let { return it }
            return synchronized(this) {
                instance ?: WatchSettingsStore(context).also { instance = it }
            }
        }
    }
}
