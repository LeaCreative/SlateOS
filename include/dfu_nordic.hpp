#pragma once

#include <cstddef>
#include <cstdint>

// InfiniTime-compatible Nordic legacy DFU (SDK ~7.x / Adafruit nrfutil).
// Wire-compatible with Gadgetbridge, nRF Connect, and the companion sealed DFU client.
// Writes the MCUBoot SECONDARY slot with a CRC-16-CCITT-verified image, then resets
// for MCUBoot swap.  Host-testable state machine; NimBLE registers the GATT service.

namespace slate {
namespace dfu {

// ── Control-point opcodes ─────────────────────────────────────────────────────
constexpr std::uint8_t kOpStartDfu            = 0x01u;
constexpr std::uint8_t kOpInitDfuParams       = 0x02u;
constexpr std::uint8_t kOpReceiveFw           = 0x03u;
constexpr std::uint8_t kOpValidate            = 0x04u;
constexpr std::uint8_t kOpActivateReset       = 0x05u;
constexpr std::uint8_t kOpAbort               = 0x06u;
constexpr std::uint8_t kOpSetPrn              = 0x08u;  // Packet Receipt Notification

// ── Response / notification opcodes ──────────────────────────────────────────
constexpr std::uint8_t kRspCode               = 0x10u;  // Response: 0x10 req status
constexpr std::uint8_t kRspPktReceipt         = 0x11u;  // 0x11 received:u32 LE

// ── Status codes ──────────────────────────────────────────────────────────────
constexpr std::uint8_t kRspSuccess            = 0x01u;
constexpr std::uint8_t kRspInvalidState       = 0x02u;
constexpr std::uint8_t kRspNotSupported       = 0x03u;
constexpr std::uint8_t kRspDataSize           = 0x04u;
constexpr std::uint8_t kRspCrcError           = 0x05u;
constexpr std::uint8_t kRspOpFailed           = 0x06u;

// ── Start DFU image type ──────────────────────────────────────────────────────
constexpr std::uint8_t kTypeSoftDeviceBootloaderApplication = 0x04u;  // App-only

// ── Init packet buffer ────────────────────────────────────────────────────────
// InfiniTime legacy init packet is 14 bytes for a single SoftDevice entry.
// Allow a small margin for variation.
constexpr std::size_t kMaxInitBytes = 32u;

enum class State : std::uint8_t {
  Idle = 0,
  ExpectSize,    // Waiting for 12-byte size packet after Start
  ExpectInit,    // Waiting for Init-start opcode after size
  InitReceiving, // Receiving raw .dat bytes
  Receiving,     // Receiving firmware image
  Validated,     // Image CRC OK; pending Activate
};

struct Hooks {
  bool (*erase_slot)(void* ctx) = nullptr;
  bool (*write_slot)(std::uint32_t offset, const std::uint8_t* data,
                     std::size_t len, void* ctx) = nullptr;
  // Write InfiniTime pending-image magic at end of secondary before Activate.
  bool (*write_pending_magic)(void* ctx) = nullptr;
  std::uint8_t (*battery_percent)(void* ctx) = nullptr;
  bool (*charging)(void* ctx) = nullptr;
  void (*reboot)(void* ctx) = nullptr;
  // Notify caller with 3-byte response (0x10 req status) or 5-byte PRN (0x11 u32).
  void (*notify_rsp)(const std::uint8_t* rsp, std::size_t len, void* ctx) = nullptr;
  void* ctx = nullptr;
};

class Engine {
public:
  void init(const Hooks& hooks);
  void reset();

  // Control-point writes (opcodes from phone).
  void on_control(const std::uint8_t* data, std::size_t len);
  // Packet characteristic writes (size packet, init bytes, image bytes).
  void on_packet(const std::uint8_t* data, std::size_t len);
  // Call on BLE disconnect to clear interrupted transfer state.
  void on_disconnect();

  State state() const { return state_; }
  std::uint32_t received() const { return received_; }
  std::uint32_t expected() const { return expected_; }

private:
  void respond(std::uint8_t req, std::uint8_t status);
  void send_receipt();
  bool battery_ok() const;

  Hooks hooks_{};
  State state_ = State::Idle;
  std::uint32_t expected_ = 0u;
  std::uint32_t received_ = 0u;
  std::uint16_t expected_crc_ = 0u;
  std::uint16_t crc_ = 0xFFFFu;
  std::uint8_t prn_interval_ = 0u;   // 0 = no PRN
  std::uint8_t prn_count_ = 0u;
  std::uint8_t packets_since_prn_ = 0u;
  std::uint8_t init_buf_[kMaxInitBytes] = {};
  std::uint8_t init_len_ = 0u;
};

}  // namespace dfu
}  // namespace slate
