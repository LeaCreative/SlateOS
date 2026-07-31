#include "dfu_nordic.hpp"

#include "flash_map.hpp"

#include <cstring>

// InfiniTime-compatible Nordic legacy DFU (SDK ~7.x / Adafruit nrfutil).
//
// Wire sequence:
//   Phone→ Start (0x01 0x04) on Control, no response yet
//   Phone→ 12-byte size packet on Packet [sdSize:u32][blSize:u32][appSize:u32]
//   Watch← 10 01 01  (Start response after erase)
//   Phone→ 0x02 0x00 on Control  (Init start)
//   Phone→ raw .dat init bytes on Packet  (14 bytes typical)
//   Phone→ 0x02 0x01 on Control  (Init complete)
//   Watch← 10 02 01
//   Phone→ PRN set: 0x08 [prn:u8] on Control  (optional, ignored silently)
//   Watch← 10 08 01
//   Phone→ 0x03 on Control  (Receive firmware)
//   Watch← 10 03 01
//   Phone→ image data on Packet in 20-byte chunks (WRITE_NO_RSP)
//     After every PRN packets: Watch← 11 [received:u32 LE]
//   Phone→ 0x04 on Control  (Validate)
//   Watch← 10 04 01  (CRC-16-CCITT passes) or 10 04 05 (CRC error)
//   Phone→ 0x05 on Control  (Activate & Reset)
//   Watch← 10 05 01  then reboot

namespace slate {
namespace dfu {
namespace {

// CRC-16-CCITT (poly 0x1021, init 0xFFFF) — matches Adafruit nrfutil.
std::uint16_t crc16_init() { return 0xFFFFu; }

std::uint16_t crc16_update(std::uint16_t crc, const std::uint8_t* data,
                           std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    crc = static_cast<std::uint16_t>(
        ((crc >> 8) & 0xFFu) | ((crc << 8) & 0xFFFFu));
    crc ^= static_cast<std::uint16_t>(data[i]);
    crc ^= static_cast<std::uint16_t>((crc & 0xFFu) >> 4);
    crc ^= static_cast<std::uint16_t>((crc << 12) & 0xFFFFu);
    crc ^= static_cast<std::uint16_t>(((crc & 0xFFu) << 5) & 0xFFFFu);
  }
  return crc;
}

std::uint32_t rd_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

void wr_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

}  // namespace

void Engine::init(const Hooks& hooks) {
  hooks_ = hooks;
  reset();
}

void Engine::reset() {
  state_ = State::Idle;
  expected_ = 0u;
  received_ = 0u;
  prn_count_ = 0u;
  prn_interval_ = 0u;
  packets_since_prn_ = 0u;
  crc_ = crc16_init();
}

bool Engine::battery_ok() const {
  if (hooks_.battery_percent == nullptr) {
    return true;
  }
  if (hooks_.charging && hooks_.charging(hooks_.ctx)) {
    return true;
  }
  const std::uint8_t p = hooks_.battery_percent(hooks_.ctx);
  if (p > 100u) {
    return false;
  }
  return p >= flash_map::kOtaMinBatteryPercent;
}

void Engine::respond(std::uint8_t req, std::uint8_t status) {
  if (hooks_.notify_rsp == nullptr) {
    return;
  }
  const std::uint8_t rsp[3] = {kRspCode, req, status};
  hooks_.notify_rsp(rsp, sizeof(rsp), hooks_.ctx);
}

void Engine::send_receipt() {
  if (hooks_.notify_rsp == nullptr) {
    return;
  }
  std::uint8_t rsp[5] = {kRspPktReceipt};
  wr_u32(rsp + 1, received_);
  hooks_.notify_rsp(rsp, sizeof(rsp), hooks_.ctx);
}

void Engine::on_control(const std::uint8_t* data, std::size_t len) {
  if (data == nullptr || len == 0u) {
    return;
  }
  const std::uint8_t op = data[0];
  switch (op) {
    case kOpStartDfu: {
      // byte1 = type: 0x04 = application. Reject anything else.
      if (len < 2u || data[1] != kTypeSoftDeviceBootloaderApplication) {
        respond(op, kRspNotSupported);
        return;
      }
      if (!battery_ok()) {
        respond(op, kRspOpFailed);
        return;
      }
      // Do not respond yet; wait for the 12-byte size packet.
      reset();
      state_ = State::ExpectSize;
      break;
    }
    case kOpInitDfuParams: {
      if (state_ != State::ExpectInit && state_ != State::InitReceiving) {
        respond(op, kRspInvalidState);
        return;
      }
      if (len < 2u) {
        respond(op, kRspInvalidState);
        return;
      }
      if (data[1] == 0x00u) {
        // Init start: subsequent Packet writes carry the raw .dat bytes
        state_ = State::InitReceiving;
        // No response here; wait for init complete (0x02 0x01)
        return;
      }
      if (data[1] == 0x01u) {
        // Init complete: parse CRC from init packet
        // Extract expected CRC from the last 2 bytes of the init data
        if (init_len_ >= 2u) {
          expected_crc_ = static_cast<std::uint16_t>(
              init_buf_[init_len_ - 2] |
              (static_cast<std::uint16_t>(init_buf_[init_len_ - 1]) << 8));
        }
        state_ = State::Receiving;
        respond(op, kRspSuccess);
        return;
      }
      respond(op, kRspInvalidState);
      break;
    }
    case kOpReceiveFw: {
      if (state_ != State::Receiving) {
        respond(op, kRspInvalidState);
        return;
      }
      // PRN is already set (or default 0); start counting packets.
      packets_since_prn_ = 0u;
      respond(op, kRspSuccess);
      break;
    }
    case kOpSetPrn: {
      // PRN: set packet receipt notification interval.
      // InfiniTime sends 0x08 0x0A (= 10). Accept any interval including 0.
      prn_interval_ = (len >= 2u) ? data[1] : 0u;
      prn_count_ = 0u;
      packets_since_prn_ = 0u;
      respond(op, kRspSuccess);
      break;
    }
    case kOpValidate: {
      if (state_ != State::Receiving || expected_ == 0u ||
          received_ != expected_) {
        respond(op, kRspCrcError);
        return;
      }
      // CRC-16-CCITT over the whole image must match the init packet.
      if (crc_ != expected_crc_) {
        respond(op, kRspCrcError);
        return;
      }
      // Write InfiniTime MCUBoot pending-image magic before reboot.
      if (hooks_.write_pending_magic != nullptr &&
          !hooks_.write_pending_magic(hooks_.ctx)) {
        respond(op, kRspOpFailed);
        return;
      }
      state_ = State::Validated;
      respond(op, kRspSuccess);
      break;
    }
    case kOpActivateReset: {
      if (state_ != State::Validated) {
        respond(op, kRspInvalidState);
        return;
      }
      respond(op, kRspSuccess);
      if (hooks_.reboot) {
        hooks_.reboot(hooks_.ctx);
      }
      break;
    }
    case kOpAbort: {
      reset();
      respond(op, kRspSuccess);
      break;
    }
    default:
      respond(op, kRspNotSupported);
      break;
  }
}

void Engine::on_packet(const std::uint8_t* data, std::size_t len) {
  if (data == nullptr || len == 0u) {
    return;
  }

  // ── Size packet (12 bytes: sd:u32 bl:u32 app:u32) ──
  if (state_ == State::ExpectSize) {
    if (len < 12u) {
      // Cannot send a valid response without proper sizing; ignore.
      return;
    }
    // Only application field is used; SD and BL must be zero.
    const std::uint32_t app_size = rd_u32(data + 8);
    if (app_size == 0u || app_size > flash_map::kSecondarySize) {
      respond(kOpInitDfuParams, kRspDataSize);
      reset();
      return;
    }
    if (hooks_.erase_slot == nullptr || !hooks_.erase_slot(hooks_.ctx)) {
      respond(kOpStartDfu, kRspOpFailed);
      reset();
      return;
    }
    expected_ = app_size;
    received_ = 0u;
    crc_ = crc16_init();
    init_len_ = 0u;
    state_ = State::ExpectInit;
    // Now send the deferred Start DFU response.
    respond(kOpStartDfu, kRspSuccess);
    return;
  }

  // ── Init packet bytes (raw .dat) ──
  if (state_ == State::InitReceiving) {
    const std::size_t copy_len =
        (init_len_ + len <= kMaxInitBytes) ? len : (kMaxInitBytes - init_len_);
    if (copy_len > 0u) {
      std::memcpy(init_buf_ + init_len_, data, copy_len);
      init_len_ += static_cast<std::uint8_t>(copy_len);
    }
    return;
  }

  // ── Firmware data ──
  if (state_ != State::Receiving) {
    return;
  }
  if (received_ + len > expected_ && expected_ != 0u) {
    respond(kOpReceiveFw, kRspDataSize);
    reset();
    return;
  }
  if (hooks_.write_slot == nullptr ||
      !hooks_.write_slot(received_, data, len, hooks_.ctx)) {
    respond(kOpReceiveFw, kRspOpFailed);
    reset();
    return;
  }
  crc_ = crc16_update(crc_, data, len);
  received_ += static_cast<std::uint32_t>(len);
  ++packets_since_prn_;

  // Send packet receipt notification at PRN interval.
  if (prn_interval_ > 0u && packets_since_prn_ >= prn_interval_) {
    packets_since_prn_ = 0u;
    send_receipt();
  }
}

void Engine::on_disconnect() {
  // An interrupted transfer must not leave partial state on the next connect.
  reset();
}

}  // namespace dfu
}  // namespace slate
