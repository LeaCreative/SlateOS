#pragma once

#include "hit_test.hpp"
#include "renderer.hpp"
#include "sdp_parser.hpp"

#include <cstddef>
#include <cstdint>

namespace sdp {

struct PushStats {
  Status status = Status::Ok;
  std::uint32_t parse_us = 0u;
  std::uint32_t render_us = 0u;
  std::uint32_t ops = 0u;
  std::size_t bytes = 0u;
};

class Interpreter {
public:
  void init(Renderer* renderer);

  // Validate + (on COMMIT success) retain and render.
  // On any failure: previous retained list and screen stay intact.
  PushStats push_list(const std::uint8_t* data, std::size_t size);

  // Re-render from retained (wake / local scroll). No phone round-trip.
  void rerender_retained();

  // Local scroll — clamps to content, then re-renders.
  void set_scroll_offset(std::uint16_t offset);
  std::uint16_t scroll_offset() const { return scroll_offset_; }

  const input::HitRect* hit_rects() const { return hit_rects_; }
  std::size_t hit_count() const { return hit_count_; }

  bool has_retained() const { return retained_len_ > 0u; }
  std::uint16_t retain_ttl_s() const { return retain_ttl_s_; }

private:
  void rebuild_hits_and_scroll_meta();
  void render_retained_to_display();

  Renderer* renderer_ = nullptr;

  alignas(4) std::uint8_t staging_[kMaxListBytes] = {};
  alignas(4) std::uint8_t retained_[kMaxListBytes] = {};
  std::size_t retained_len_ = 0u;
  std::uint16_t retain_ttl_s_ = 0u;

  input::HitRect hit_rects_[kMaxHitElems] = {};
  std::size_t hit_count_ = 0u;

  std::uint8_t scroll_y_ = 0u;
  std::uint8_t scroll_h_ = 0u;
  std::uint16_t scroll_content_h_ = 0u;
  std::uint16_t scroll_offset_ = 0u;
  bool have_scroll_ = false;
};

}  // namespace sdp
