#include "input_router.hpp"

#include "sdp_input.hpp"
#include "sdp_opcodes.hpp"

namespace slate {
namespace {

constexpr std::uint16_t kEdgePx = 16u;

}  // namespace

void InputRouter::init(Hooks hooks, sdp::Interpreter* interp,
                       session::Manager* session) {
  hooks_ = hooks;
  interp_ = interp;
  session_ = session;
  hits_ = nullptr;
  hit_count_ = 0u;
  focus_id_ = input::kNoHit;
}

void InputRouter::set_hits(const input::HitRect* rects, std::size_t count) {
  hits_ = rects;
  hit_count_ = count;
}

void InputRouter::emit(const std::uint8_t* msg, std::size_t len) {
  if (hooks_.send_input == nullptr || msg == nullptr || len == 0u) {
    return;
  }
  if (session_ != nullptr) {
    const auto st = session_->state();
    if (st != session::State::Ready && st != session::State::Active &&
        st != session::State::Idle) {
      return;
    }
  }
  (void)hooks_.send_input(msg, len, hooks_.ctx);
}

const input::HitRect* InputRouter::find_hit(std::uint16_t x,
                                            std::uint16_t y) const {
  const std::size_t i = input::hit_index(x, y, hits_, hit_count_);
  if (i >= hit_count_) {
    return nullptr;
  }
  return &hits_[i];
}

std::uint8_t InputRouter::map_swipe(input::SwipeDir d) {
  switch (d) {
    case input::SwipeDir::Up:    return sdp::swipe_dir::UP;
    case input::SwipeDir::Down:  return sdp::swipe_dir::DOWN;
    case input::SwipeDir::Left:  return sdp::swipe_dir::LEFT;
    case input::SwipeDir::Right: return sdp::swipe_dir::RIGHT;
    default:                     return sdp::swipe_dir::DOWN;
  }
}

std::uint8_t InputRouter::map_button(button::Action a) {
  switch (a) {
    case button::Action::Press:       return sdp::button_action::PRESS;
    case button::Action::LongPress:   return sdp::button_action::LONG_PRESS;
    case button::Action::DoublePress: return sdp::button_action::DOUBLE_PRESS;
    default:                          return sdp::button_action::PRESS;
  }
}

void InputRouter::on_event(const input::Event& ev) {
  std::uint8_t buf[16];

  switch (ev.type) {
    case input::EventType::None:
      return;

    case input::EventType::Button: {
      // Side button: local back when a remote screen is up; else BUTTON.
      if (session_ != nullptr && session_->remote_depth() > 0u &&
          ev.button == button::Action::Press) {
        const std::size_t n = sdp::input_wire::encode_back(buf, sizeof(buf));
        emit(buf, n);
        (void)session_->local_back(0u);
        return;
      }
      const std::size_t n =
          sdp::input_wire::encode_button(buf, sizeof(buf), map_button(ev.button));
      emit(buf, n);
      return;
    }

    case input::EventType::Swipe: {
      const std::uint8_t dir = map_swipe(ev.swipe);

      // Edge swipe from left → BACK (watch-local navigation cue).
      if (ev.x < kEdgePx && dir == sdp::swipe_dir::RIGHT) {
        const std::size_t n = sdp::input_wire::encode_edge_swipe(
            buf, sizeof(buf), sdp::edge::LEFT, dir, static_cast<std::uint8_t>(ev.x));
        emit(buf, n);
        if (session_ != nullptr) {
          (void)session_->local_back(0u);
        }
        return;
      }

      // Local scroll ownership — zero RTT.
      if (interp_ != nullptr && interp_->has_retained()) {
        std::uint16_t off = interp_->scroll_offset();
        if (dir == sdp::swipe_dir::UP) {
          off = static_cast<std::uint16_t>(off + 24u);
        } else if (dir == sdp::swipe_dir::DOWN) {
          off = (off > 24u) ? static_cast<std::uint16_t>(off - 24u) : 0u;
        }
        if (dir == sdp::swipe_dir::UP || dir == sdp::swipe_dir::DOWN) {
          interp_->set_scroll_offset(off);
          const std::size_t n = sdp::input_wire::encode_scroll_pos(
              buf, sizeof(buf), 0u, interp_->scroll_offset());
          emit(buf, n);
          return;
        }
      }

      const std::size_t n = sdp::input_wire::encode_swipe(buf, sizeof(buf), dir);
      emit(buf, n);
      return;
    }

    case input::EventType::Tap:
    case input::EventType::LongPress: {
      const input::HitRect* hit = find_hit(ev.x, ev.y);
      std::uint16_t elem = input::kNoHit;
      std::uint8_t flags = 0u;
      if (hit != nullptr) {
        flags = hit->flags;
        if ((flags & sdp::elem_flags::DISABLED) != 0u) {
          return;  // occupy layout, report nothing
        }
        elem = hit->id;
        if ((flags & sdp::elem_flags::FOCUSABLE) != 0u) {
          focus_id_ = elem;
        }
        if ((flags & sdp::elem_flags::HAPTIC) != 0u && hooks_.haptic) {
          hooks_.haptic(sdp::haptic_pattern::TICK, hooks_.ctx);
        }
      }

      if (ev.type == input::EventType::LongPress) {
        const std::size_t n =
            sdp::input_wire::encode_long_press(buf, sizeof(buf), elem);
        emit(buf, n);
      } else {
        const std::size_t n = sdp::input_wire::encode_tap(
            buf, sizeof(buf), elem, static_cast<std::uint8_t>(ev.x),
            static_cast<std::uint8_t>(ev.y));
        emit(buf, n);
      }
      return;
    }

    case input::EventType::MultiTap: {
      const input::HitRect* hit = find_hit(ev.x, ev.y);
      std::uint16_t elem = input::kNoHit;
      if (hit != nullptr && (hit->flags & sdp::elem_flags::DISABLED) == 0u) {
        elem = hit->id;
        if ((hit->flags & sdp::elem_flags::HAPTIC) != 0u && hooks_.haptic) {
          hooks_.haptic(sdp::haptic_pattern::TICK, hooks_.ctx);
        }
      }
      const std::size_t n = sdp::input_wire::encode_multi_tap(
          buf, sizeof(buf), ev.multi_count, elem,
          static_cast<std::uint8_t>(ev.x), static_cast<std::uint8_t>(ev.y));
      emit(buf, n);
      return;
    }

    default:
      return;
  }
}

}  // namespace slate
