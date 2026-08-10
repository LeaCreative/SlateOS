#include "hrs3300.hpp"

namespace slate {
namespace hrs {

void Driver::init(const Bus& bus) {
  bus_ = bus;
  enabled_ = false;
  if (bus_.cfg_int_pin != nullptr) {
    bus_.cfg_int_pin(bus_.ctx);
  }

  disable();
  if (bus_.delay_ms != nullptr) {
    bus_.delay_ms(100u, bus_.ctx);
  }

  // HRS disabled, 50 ms wait between ADC periods, current 12.5 mA (InfiniTime).
  (void)wr(static_cast<std::uint8_t>(Reg::Enable), 0x50u);
  (void)wr(static_cast<std::uint8_t>(Reg::PDriver), kLedDrive);
  // HRS and ALS 15-bit → ~50 ms LED / ADC period.
  (void)wr(static_cast<std::uint8_t>(Reg::Res), 0x77u);
  (void)wr(static_cast<std::uint8_t>(Reg::Hgain), 0x00u);
}

void Driver::enable() {
  std::uint8_t value = 0u;
  if (!rd(static_cast<std::uint8_t>(Reg::Enable), &value)) {
    return;
  }
  value = static_cast<std::uint8_t>(value | 0x80u);
  (void)wr(static_cast<std::uint8_t>(Reg::Enable), value);
  (void)wr(static_cast<std::uint8_t>(Reg::PDriver), kLedDrive);
  enabled_ = true;
}

void Driver::disable() {
  std::uint8_t value = 0u;
  if (rd(static_cast<std::uint8_t>(Reg::Enable), &value)) {
    value = static_cast<std::uint8_t>(value & ~static_cast<std::uint8_t>(0x80u));
    (void)wr(static_cast<std::uint8_t>(Reg::Enable), value);
  }
  (void)wr(static_cast<std::uint8_t>(Reg::PDriver), 0x00u);
  enabled_ = false;
}

PackedHrsAls Driver::read_hrs_als() {
  constexpr std::uint8_t kBase = 0x08u;
  constexpr std::uint8_t kLen = 0x0Fu - kBase + 1u;

  PackedHrsAls res{};
  std::uint8_t buf[kLen] = {};
  if (bus_.read_reg == nullptr ||
      !bus_.read_reg(kBase, buf, kLen, bus_.ctx)) {
    return res;
  }

  const auto at = [&](std::uint8_t reg) -> std::uint8_t {
    return buf[static_cast<std::uint8_t>(reg - kBase)];
  };

  const std::uint8_t m0 = at(static_cast<std::uint8_t>(Reg::C0DataM));
  const std::uint8_t h0 = at(static_cast<std::uint8_t>(Reg::C0DataH));
  const std::uint8_t l0 = at(static_cast<std::uint8_t>(Reg::C0dataL));
  res.hrs = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(m0) << 8) |
      (static_cast<std::uint16_t>(h0 & 0x0Fu) << 4) |
      static_cast<std::uint16_t>(l0 & 0x0Fu));

  const std::uint8_t m1 = at(static_cast<std::uint8_t>(Reg::C1dataM));
  const std::uint8_t h1 = at(static_cast<std::uint8_t>(Reg::C1dataH));
  const std::uint8_t l1 = at(static_cast<std::uint8_t>(Reg::C1dataL));
  res.als = static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(h1 & 0x3Fu) << 11) |
      (static_cast<std::uint16_t>(m1) << 3) |
      static_cast<std::uint16_t>(l1 & 0x07u));

  return res;
}

bool Driver::wr(std::uint8_t reg, std::uint8_t v) {
  if (bus_.write_reg == nullptr) {
    return false;
  }
  return bus_.write_reg(reg, &v, 1u, bus_.ctx);
}

bool Driver::rd(std::uint8_t reg, std::uint8_t* v) {
  if (bus_.read_reg == nullptr || v == nullptr) {
    return false;
  }
  return bus_.read_reg(reg, v, 1u, bus_.ctx);
}

}  // namespace hrs
}  // namespace slate
