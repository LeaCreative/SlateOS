package slate.app.repo

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import slate.app.link.LinkForegroundService

/**
 * Periodic index checks (§6.6). Respects metered settings via [RepoManager].
 * Default interval: 12 hours.
 */
class RepoUpdateScheduler(
    private val context: Context,
    private val scope: CoroutineScope,
    private val intervalMs: Long = 12L * 60L * 60L * 1000L,
) {
    private var job: Job? = null

    fun start() {
        if (job?.isActive == true) return
        job = scope.launch {
            delay(30_000) // settle after boot
            while (isActive) {
                runOnce()
                delay(intervalMs)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private suspend fun runOnce() {
        val prefs = RepoPrefs(context)
        if (!prefs.autoUpdateEnabled) return
        val cm = context.getSystemService(ConnectivityManager::class.java)
        val net = cm?.activeNetwork
        val caps = net?.let { cm.getNetworkCapabilities(it) }
        val metered = caps != null &&
            !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
        if (metered && !prefs.allowMeteredUpdates) return

        val store = InstalledStore.create(context)
        val hostVer = try {
            context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: "0.1.0"
        } catch (_: Throwable) {
            "0.1.0"
        }
        val manager = RepoManager(
            context = context,
            prefs = prefs,
            store = store,
            hostVersion = hostVer,
            watchProtocol = {
                LinkForegroundService.instance?.watchProtocolVersion() ?: 1
            },
        )
        manager.refreshIndexes(force = false)
    }
}
