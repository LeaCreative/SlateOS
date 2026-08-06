package slate.generated

/** SDP wire constants — generated from shared/sdp_wire.json. */
object SdpWire {
    const val DISPLAY_SIZE = 240
    const val MAX_LIST_BYTES = 4096
    const val MAX_OPS = 512
    const val PALETTE_SIZE = 16
    const val MAX_ELEM_DEPTH = 8
    const val MAX_HIT_ELEMS = 32
    const val MAX_FONT_ID = 1
    const val MAX_ATLAS_ID = 0
    const val MAX_ASSET_ID = 0
    const val MAX_ICON_ID = 0
    const val MAX_IMAGE_ID = 0
    const val NO_HIT = 0xFFFF

    object Op {
        const val CLEAR = 0x01
        const val SET_PALETTE = 0x02
        const val RECT = 0x03
        const val RECT_ROUND = 0x04
        const val LINE = 0x05
        const val CIRCLE = 0x06
        const val ARC = 0x07
        const val POLYLINE = 0x08
        const val CLIP_RECT = 0x09
        const val CLIP_CLEAR = 0x0A
        const val TEXT = 0x10
        const val TEXT_BOX = 0x11
        const val ICON = 0x12
        const val IMAGE = 0x13
        const val PROGRESS_BAR = 0x20
        const val PROGRESS_ARC = 0x21
        const val BEGIN_ELEM = 0x30
        const val END_ELEM = 0x31
        const val SCROLL_REGION = 0x40
        const val PATCH = 0x50
        const val PATCH_REF = 0x51
        const val HAPTIC = 0x60
        const val BACKLIGHT = 0x61
        const val EXT_MIN = 0xE0
        const val TEXT_SCALED = 0xE0
        const val EXT_MAX = 0xEF
        const val COMMIT = 0xF0
        const val RETAIN = 0xF1
    }

    object ColorTag {
        const val LITERAL_RGB565 = 0x00
        const val PALETTE_MIN = 0x01
        const val PALETTE_MAX = 0x10
    }

    object Style {
        const val MODE_FILL = 0x00
        const val MODE_STROKE = 0x01
        const val MODE_FILL_STROKE = 0x02
        const val MODE_RESERVED = 0x03
        const val MODE_MASK = 0x03
        const val WIDTH_SHIFT = 0x02
        const val WIDTH_MASK = 0x0F
        const val RESERVED_MASK = 0xC0
    }

    object ElemFlags {
        const val EMIT_TOUCH = 0x01
        const val NO_HIT = 0x02
        const val HAPTIC = 0x04
        const val FOCUSABLE = 0x08
        const val DISABLED = 0x10
        const val ALLOWED = 31
    }

    object TextBoxFlags {
        const val WRAP = 0x01
        const val ELLIPSIS_END = 0x02
        const val VCENTER = 0x04
        const val ALLOWED = 7
    }

    object CommitFlags {
        const val FADE = 0x01
        const val NO_CLEAR = 0x02
        const val ALLOWED = 3
    }

    object Align {
        const val LEFT = 0x00
        const val CENTER = 0x01
        const val RIGHT = 0x02
        const val MAX = 2
    }

    object PatchFormat {
        const val RGB565 = 0x00
        const val RGB332 = 0x01
        const val PAL4 = 0x02
        const val MONO1 = 0x03
        const val MAX = 3
    }

    object PatchEncoding {
        const val RAW = 0x00
        const val RLE = 0x01
        const val MAX = 1
    }

    object HapticPattern {
        const val TICK = 0x00
        const val SHORT = 0x01
        const val DOUBLE = 0x02
        const val LONG = 0x03
        const val ERROR = 0x04
        const val MAX = 4
    }

    object SwipeDir {
        const val UP = 0x00
        const val DOWN = 0x01
        const val LEFT = 0x02
        const val RIGHT = 0x03
        const val MAX = 3
    }

    object InputOp {
        const val TAP = 0x01
        const val LONG_PRESS = 0x02
        const val SWIPE = 0x03
        const val BUTTON = 0x04
        const val SCROLL_POS = 0x05
        const val BACK = 0x06
        const val SESSION_END = 0x07
        const val MULTI_TAP = 0x08
        const val EDGE_SWIPE = 0x09
        const val TOUCH_DOWN = 0x0A
        const val TOUCH_UP = 0x0B
    }

    object ButtonAction {
        const val PRESS = 0x00
        const val LONG_PRESS = 0x01
        const val DOUBLE_PRESS = 0x02
        const val MAX = 2
    }

    object SessionEndReason {
        const val USER_BACK = 0x00
        const val TIMEOUT = 0x01
        const val LINK_LOST = 0x02
        const val PHONE_REQUEST = 0x03
        const val ERROR = 0x04
        const val MAX = 4
    }

    object Edge {
        const val TOP = 0x00
        const val BOTTOM = 0x01
        const val LEFT = 0x02
        const val RIGHT = 0x03
        const val MAX = 3
    }

    object ControlOp {
        const val HELLO_OFFER = 0x01
        const val HELLO_ACCEPT = 0x02
        const val HELLO_REJECT = 0x03
        const val HEARTBEAT = 0x04
        const val SET_PROFILE = 0x05
        const val PROFILE_ACK = 0x06
        const val SCREEN_PUSH = 0x07
        const val SCREEN_POP = 0x08
        const val SCREEN_REPLACE = 0x09
        const val CREDIT = 0x0A
        const val GOODBYE = 0x0B
        const val TIME_SYNC = 0x20
        const val CONFIRM_STATUS_REQUEST = 0xE0
        const val CONFIRM_STATUS = 0xE1
    }

    const val PROTOCOL_VERSION = 1

}
