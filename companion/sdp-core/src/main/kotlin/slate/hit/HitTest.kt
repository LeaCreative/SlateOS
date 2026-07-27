package slate.hit

import slate.generated.SdpWire

data class HitRect(
    val id: Int,
    val x: Int,
    val y: Int,
    val w: Int,
    val h: Int,
)

fun hitTest(px: Int, py: Int, rects: List<HitRect>): Int {
    var hit = SdpWire.NO_HIT
    for (r in rects) {
        if (r.w == 0 || r.h == 0) continue
        if (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h) {
            hit = r.id
        }
    }
    return hit
}
