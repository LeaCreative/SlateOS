package slate.app.host

import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry

/** Minimal LifecycleOwner so CameraX can bind inside the link FGS. */
class ServiceLifecycleOwner : LifecycleOwner {
    private val registry = LifecycleRegistry(this)

    init {
        registry.currentState = Lifecycle.State.RESUMED
    }

    override val lifecycle: Lifecycle
        get() = registry

    fun destroy() {
        registry.currentState = Lifecycle.State.DESTROYED
    }
}
