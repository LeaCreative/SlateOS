package slate.app.media

import android.content.ComponentName
import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import slate.app.link.LinkLog
import slate.app.notif.SlateNotificationListener

/**
 * Now-playing bridge for JS. Uses active MediaSessions (requires Notification
 * Listener access, same as the shade). Metadata stays on the phone; JS gets
 * short strings and transport commands.
 */
class MediaAdapter(
    private val context: Context,
    private val onEvent: (json: String) -> Unit,
) {
    private val main = Handler(Looper.getMainLooper())
    private var manager: MediaSessionManager? = null
    private var controller: MediaController? = null
    private var listening = false

    private val sessionListener = MediaSessionManager.OnActiveSessionsChangedListener { sessions ->
        attach(sessions)
    }

    private val callback = object : MediaController.Callback() {
        override fun onMetadataChanged(metadata: android.media.MediaMetadata?) {
            emitNowPlaying()
        }

        override fun onPlaybackStateChanged(state: PlaybackState?) {
            emitNowPlaying()
        }
    }

    fun subscribe() {
        if (listening) {
            emitNowPlaying()
            return
        }
        if (!SlateNotificationListener.isEnabled(context)) {
            emit(
                JSONObject()
                    .put("type", "status")
                    .put("state", "denied")
                    .put("detail", "enable notification access"),
            )
            return
        }
        try {
            val msm = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
            manager = msm
            val cn = ComponentName(context, SlateNotificationListener::class.java)
            msm.addOnActiveSessionsChangedListener(sessionListener, cn, main)
            attach(msm.getActiveSessions(cn))
            listening = true
            LinkLog.i("media.subscribe")
        } catch (t: Throwable) {
            LinkLog.w("media.subscribe failed: ${t.message}")
            emit(
                JSONObject()
                    .put("type", "status")
                    .put("state", "error")
                    .put("detail", (t.message ?: "media").take(80)),
            )
        }
    }

    fun unsubscribe() {
        if (!listening) return
        listening = false
        try {
            controller?.unregisterCallback(callback)
        } catch (_: Throwable) {
        }
        controller = null
        try {
            manager?.removeOnActiveSessionsChangedListener(sessionListener)
        } catch (_: Throwable) {
        }
        manager = null
        LinkLog.i("media.unsubscribe")
    }

    fun play() = transport { it.transportControls.play() }

    fun pause() = transport { it.transportControls.pause() }

    fun next() = transport { it.transportControls.skipToNext() }

    fun previous() = transport { it.transportControls.skipToPrevious() }

    private fun transport(block: (MediaController) -> Unit) {
        val c = controller
        if (c == null) {
            emit(JSONObject().put("type", "status").put("state", "idle"))
            return
        }
        try {
            block(c)
        } catch (t: Throwable) {
            LinkLog.w("media.transport failed: ${t.message}")
        }
    }

    private fun attach(sessions: List<MediaController>?) {
        main.post {
            try {
                controller?.unregisterCallback(callback)
            } catch (_: Throwable) {
            }
            val next = sessions?.firstOrNull()
            controller = next
            if (next == null) {
                emit(JSONObject().put("type", "status").put("state", "idle"))
                return@post
            }
            try {
                next.registerCallback(callback, main)
            } catch (_: Throwable) {
            }
            emitNowPlaying()
        }
    }

    private fun emitNowPlaying() {
        val c = controller
        if (c == null) {
            emit(JSONObject().put("type", "status").put("state", "idle"))
            return
        }
        val meta = c.metadata
        val state = c.playbackState
        val playing = state?.state == PlaybackState.STATE_PLAYING
        val title = meta?.getString(android.media.MediaMetadata.METADATA_KEY_TITLE).orEmpty()
        val artist = meta?.getString(android.media.MediaMetadata.METADATA_KEY_ARTIST).orEmpty()
            .ifEmpty {
                meta?.getString(android.media.MediaMetadata.METADATA_KEY_ALBUM_ARTIST).orEmpty()
            }
        val album = meta?.getString(android.media.MediaMetadata.METADATA_KEY_ALBUM).orEmpty()
        emit(
            JSONObject()
                .put("type", "nowPlaying")
                .put("playing", playing)
                .put("title", title.take(64))
                .put("artist", artist.take(64))
                .put("album", album.take(64))
                .put("app", c.packageName.take(64)),
        )
    }

    private fun emit(o: JSONObject) {
        onEvent(o.toString())
    }
}
