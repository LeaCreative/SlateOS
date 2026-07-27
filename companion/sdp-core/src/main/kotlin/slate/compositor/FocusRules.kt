package slate.compositor

import slate.host.PriorityClass

/**
 * Focus-stealing rules (M8).
 *
 * ## When may an app steal focus?
 *
 * Let `F` be the focused entry and `R` the requester.
 *
 * 1. **Higher priority always steals.** If `R.rank > F.rank`, the request is granted.
 *    Example: INTERRUPT call overlay displaces NORMAL mail; CRITICAL SOS displaces anything.
 * 2. **Equal priority.** Steal only when [FocusReason.UserNavigation] (explicit user
 *    open / launcher) or [FocusReason.SameApp] (app re-focusing itself). Peer apps cannot
 *    bounce each other via background INTERRUPT spam at the same rank.
 * 3. **Lower priority never steals.** Request is denied; the app may remain registered
 *    and retry later. Ambient cannot cover an active NORMAL screen.
 *
 * Empty stack / no focus: any priority may take focus (first wins).
 *
 * ## What happens to the displaced app?
 *
 * - It receives **Blur** (lifecycle), not Destroy.
 * - With [StackOp.Push], it stays on the stack beneath the new top and is restored
 *   (Focus again) when the stealer [StackOp] pops / relinquish.
 * - With [StackOp.Replace], the displaced entry is removed from the stack (Stop) if it
 *   is not the ambient base; ambient is never destroyed by Replace of a higher layer.
 * - **AMBIENT** is the singleton base: at most one ambient entry; higher screens push
 *   above it. Stealing ambient with NORMAL+ leaves ambient paused underneath.
 */
object FocusRules {
    fun canSteal(
        requester: PriorityClass,
        focused: PriorityClass?,
        reason: FocusReason,
    ): Boolean {
        if (focused == null) return true
        val cmp = requester.rank - focused.rank
        return when {
            cmp > 0 -> true
            cmp < 0 -> false
            else -> reason == FocusReason.UserNavigation || reason == FocusReason.SameApp
        }
    }
}

enum class FocusReason {
    /** Launcher / user explicitly opened the app. */
    UserNavigation,
    /** Background raise (notification, adapter). */
    SystemRaise,
    /** Same app id refreshing focus. */
    SameApp,
}

enum class StackOp {
    Push,
    Replace,
}

enum class FocusDenyReason {
    PriorityTooLow,
    ProtocolTooOld,
    UnknownApp,
    NotConnected,
}
