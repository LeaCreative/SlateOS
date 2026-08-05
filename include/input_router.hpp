#pragma once

#include "hit_test.hpp"
#include "input_event.hpp"
#include "sdp_interpreter.hpp"
#include "session.hpp"

#include <cstddef>
#include <cstdint>

// Local-first input: scroll / focus / haptic / back with zero phone RTT.
// Discrete semantic events are encoded and sent on channel 2 when linked.

namespace slate {

class InputRouter {
public:
  struct Hooks {
    bool (*send_input)(const std::uint8_t* msg, std::size_t len,
                       void* ctx) = nullptr;
    void (*haptic)(std::uint8_t pattern, void* ctx) = nullptr;
    void* ctx = nullptr;
  };

  void init(Hooks hooks, sdp::Interpreter* interp, session::Manager* session);

  void set_hits(const input::HitRect* rects, std::size_t count);

  // Handle one structured device event. May re-render locally and/or TX.
  void on_event(const input::Event& ev);

  std::uint16_t focus_id() const { return focus_id_; }

  /** True if (x,y) lands on a registered element. Diagnostics only. */
  bool has_hit(std::uint16_t x, std::uint16_t y) const {
    return find_hit(x, y) != nullptr;
  }

private:
  void emit(const std::uint8_t* msg, std::size_t len);
  const input::HitRect* find_hit(std::uint16_t x, std::uint16_t y) const;
  static std::uint8_t map_swipe(input::SwipeDir d);
  static std::uint8_t map_button(button::Action a);

  Hooks hooks_{};
  sdp::Interpreter* interp_ = nullptr;
  session::Manager* session_ = nullptr;
  const input::HitRect* hits_ = nullptr;
  std::size_t hit_count_ = 0u;
  std::uint16_t focus_id_ = input::kNoHit;
};

}  // namespace slate
