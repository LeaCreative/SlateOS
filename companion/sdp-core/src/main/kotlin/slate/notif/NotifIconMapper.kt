package slate.notif

/**
 * On-watch icon strategy (M9).
 *
 * **Do we need an on-watch atlas?** Yes for *offline* retained notifications
 * (channel 4 / phone away): the watch must draw *something* without the phone.
 * That does **not** mean shipping every app's bitmap.
 *
 * **What lives where**
 * - **Phone:** maps `packageName` → [Category] + monogram letter; builds live
 *   display lists (colored chip + TEXT monogram, or ICON when atlas packs exist).
 * - **Watch:** a *small* category atlas (message/call/mail/… — dozens of glyphs,
 *   not thousands of apps) plus the built-in font. Retained-store records store
 *   `category` + `monogram`, not PNG blobs.
 * - **Optional later:** ASSET-channel pushes for a few user-pinned app icons.
 *
 * Drawing full-resolution launcher icons into every DISPLAY list would waste BLE
 * credit and still leave the offline store without art. Procedural monograms are
 * enough for M9; ICON opcode ids align with [Category.atlasId] for M11 packs.
 */
enum class NotifIconCategory(val atlasId: Int, val label: String) {
    GENERIC(0, "app"),
    MESSAGE(1, "msg"),
    CALL(2, "call"),
    MAIL(3, "mail"),
    CALENDAR(4, "cal"),
    SOCIAL(5, "soc"),
    MEDIA(6, "media"),
    NAVIGATION(7, "nav"),
    HEALTH(8, "hlth"),
    ALARM(9, "alm"),
    SYSTEM(10, "sys"),
}

data class NotifIconRef(
    val category: NotifIconCategory,
    /** ASCII A–Z / 0–9 drawn with built-in font when atlas missing. */
    val monogram: Char,
)

object NotifIconMapper {
    fun map(packageName: String, title: String?): NotifIconRef {
        val cat = categoryForPackage(packageName)
        val mono = monogram(packageName, title)
        return NotifIconRef(cat, mono)
    }

    fun categoryForPackage(pkg: String): NotifIconCategory {
        val p = pkg.lowercase()
        return when {
            p.contains("dialer") || p.contains("telecom") || p.contains("call") ->
                NotifIconCategory.CALL
            p.contains("mms") || p.contains("messaging") || p.contains("sms") ||
                p.contains("whatsapp") || p.contains("telegram") || p.contains("signal") ->
                NotifIconCategory.MESSAGE
            p.contains("gm") || p.contains("mail") || p.contains("outlook") ||
                p.contains("email") ->
                NotifIconCategory.MAIL
            p.contains("calendar") || p.contains("agenda") ->
                NotifIconCategory.CALENDAR
            p.contains("twitter") || p.contains("instagram") || p.contains("facebook") ||
                p.contains("reddit") || p.contains("tiktok") || p.contains("linkedin") ->
                NotifIconCategory.SOCIAL
            p.contains("spotify") || p.contains("youtube") || p.contains("music") ||
                p.contains("podcast") ->
                NotifIconCategory.MEDIA
            p.contains("maps") || p.contains("navigation") || p.contains("waze") ->
                NotifIconCategory.NAVIGATION
            p.contains("fit") || p.contains("health") || p.contains("heart") ->
                NotifIconCategory.HEALTH
            p.contains("clock") || p.contains("deskclock") || p.contains("alarm") ->
                NotifIconCategory.ALARM
            p.startsWith("android") || p.startsWith("com.google.android.apps.wellbeing") ->
                NotifIconCategory.SYSTEM
            else -> NotifIconCategory.GENERIC
        }
    }

    fun monogram(packageName: String, title: String?): Char {
        val fromTitle = title?.firstOrNull { it.isLetterOrDigit() }
        if (fromTitle != null) return fromTitle.uppercaseChar()
        val leaf = packageName.substringAfterLast('.').firstOrNull { it.isLetterOrDigit() }
        return (leaf ?: '?').uppercaseChar()
    }
}
