#include "dfu_nordic.hpp"
#include "flash_map.hpp"
#include "ota_xfer.hpp"
#include "sha256.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fails = 0;
void expect(bool cond, const char* msg) {
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_fails;
  }
}

std::vector<std::uint8_t> g_slot;
std::uint8_t g_batt = 50;
bool g_charging = false;
std::vector<std::vector<std::uint8_t>> g_tx;
bool g_rebooted = false;

bool erase_slot(void*) {
  g_slot.assign(slate::flash_map::kSecondarySize, 0xFFu);
  return true;
}
bool write_slot(std::uint32_t off, const std::uint8_t* d, std::size_t n, void*) {
  if (off + n > g_slot.size()) {
    return false;
  }
  std::memcpy(g_slot.data() + off, d, n);
  return true;
}
std::uint8_t batt(void*) { return g_batt; }
bool chg(void*) { return g_charging; }
bool commit_ok(std::uint32_t, const std::uint8_t*, void*) {
  g_rebooted = true;
  return true;
}
bool send_fn(const std::uint8_t* msg, std::size_t len, void*) {
  g_tx.emplace_back(msg, msg + len);
  return true;
}

void wr_u16(std::uint8_t* p, std::uint16_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}
void wr_u32(std::uint8_t* p, std::uint32_t v) {
  p[0] = static_cast<std::uint8_t>(v & 0xFFu);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

}  // namespace

int main() {
  // SHA-256 known vector: empty string
  std::uint8_t empty_sha[32];
  slate::sha256::hash(nullptr, 0, empty_sha);
  const std::uint8_t empty_exp[32] = {
      0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
      0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
      0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
  expect(std::memcmp(empty_sha, empty_exp, 32) == 0, "sha256 empty");

  // ── OTA unknown battery ──────────────────────────────────────────────────
  {
    g_batt = 0xFF;
    g_charging = false;
    g_tx.clear();
    slate::ota::Receiver r;
    slate::ota::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.commit_ok = commit_ok;
    h.send = send_fn;
    r.init(h);
    g_tx.clear();
    std::uint8_t begin[43] = {};
    begin[0] = slate::ota::kOpBegin;
    wr_u16(begin + 1, 1);
    wr_u32(begin + 3, 16);
    r.on_message(begin, sizeof(begin));
    expect(!g_tx.empty() && g_tx.back()[0] == slate::ota::kOpNak &&
               g_tx.back()[1] ==
                   static_cast<std::uint8_t>(slate::ota::NakReason::LowBattery),
           "ota unknown battery nak");
  }

  // ── OTA low battery ──────────────────────────────────────────────────────
  {
    g_batt = 20;
    g_charging = false;
    g_tx.clear();
    slate::ota::Receiver r;
    slate::ota::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.commit_ok = commit_ok;
    h.send = send_fn;
    r.init(h);
    g_tx.clear();
    std::uint8_t begin[43] = {};
    begin[0] = slate::ota::kOpBegin;
    wr_u16(begin + 1, 1);
    wr_u32(begin + 3, 16);
    r.on_message(begin, sizeof(begin));
    expect(!g_tx.empty() && g_tx.back()[0] == slate::ota::kOpNak &&
               g_tx.back()[1] ==
                   static_cast<std::uint8_t>(slate::ota::NakReason::LowBattery),
           "ota low battery nak");
  }

  // ── OTA happy path + hash ────────────────────────────────────────────────
  {
    g_batt = 80;
    g_rebooted = false;
    g_tx.clear();
    const char* payload = "hello-slate-ota!!";  // 16 bytes
    std::uint8_t sha[32];
    slate::sha256::hash(reinterpret_cast<const std::uint8_t*>(payload), 16, sha);

    slate::ota::Receiver r;
    slate::ota::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.commit_ok = commit_ok;
    h.send = send_fn;
    r.init(h);
    g_tx.clear();

    std::uint8_t begin[43] = {};
    begin[0] = slate::ota::kOpBegin;
    wr_u16(begin + 1, 7);
    wr_u32(begin + 3, 16);
    std::memcpy(begin + 7, sha, 32);
    wr_u32(begin + 39, 0x00010000u);
    r.on_message(begin, sizeof(begin));
    expect(r.transfer_active(), "ota begin active");

    std::uint8_t chunk[7 + 16];
    chunk[0] = slate::ota::kOpChunk;
    wr_u16(chunk + 1, 7);
    wr_u32(chunk + 3, 0);
    std::memcpy(chunk + 7, payload, 16);
    r.on_message(chunk, sizeof(chunk));
    expect(r.received() == 16u, "ota chunk received");

    // Resume BEGIN same id
    r.on_message(begin, sizeof(begin));
    expect(r.received() == 16u, "ota resume keeps offset");

    std::uint8_t commit[3] = {slate::ota::kOpCommit, 7, 0};
    r.on_message(commit, sizeof(commit));
    expect(g_rebooted, "ota commit reboots");
    expect(std::memcmp(g_slot.data(), payload, 16) == 0, "ota slot bytes");
  }

  // ── OTA hash fail ────────────────────────────────────────────────────────
  {
    g_rebooted = false;
    slate::ota::Receiver r;
    slate::ota::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.commit_ok = commit_ok;
    h.send = send_fn;
    r.init(h);
    g_tx.clear();
    std::uint8_t begin[43] = {};
    begin[0] = slate::ota::kOpBegin;
    wr_u16(begin + 1, 1);
    wr_u32(begin + 3, 4);
    // wrong sha left zero
    r.on_message(begin, sizeof(begin));
    std::uint8_t chunk[11] = {slate::ota::kOpChunk, 1, 0, 0, 0, 0, 0,
                              'a', 'b', 'c', 'd'};
    r.on_message(chunk, sizeof(chunk));
    std::uint8_t commit[3] = {slate::ota::kOpCommit, 1, 0};
    r.on_message(commit, sizeof(commit));
    expect(!g_rebooted, "ota bad hash no reboot");
    expect(!g_tx.empty() && g_tx.back()[0] == slate::ota::kOpNak &&
               g_tx.back()[1] ==
                   static_cast<std::uint8_t>(slate::ota::NakReason::HashFail),
           "ota hash fail nak");
  }

  // ── Nordic DFU: happy path (InfiniTime/nrfutil exact sequence) ────────────
  {
    g_batt = 90;
    g_rebooted = false;
    g_tx.clear();
    bool magic_written = false;

    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.write_pending_magic = [](void* p) {
      *static_cast<bool*>(p) = true;
      return true;
    };
    h.ctx = &magic_written;
    h.reboot = [](void*) { g_rebooted = true; };
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    // 1. Start DFU: no response yet
    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    expect(g_tx.empty(), "dfu: no response before size pkt");
    expect(e.state() == slate::dfu::State::ExpectSize, "dfu: ExpectSize");

    // 2. Send 12-byte size packet on Packet characteristic
    //    [sd:u32=0][bl:u32=0][app:u32=8]
    std::uint8_t size_pkt[12] = {};
    wr_u32(size_pkt + 8, 8u);  // 8 bytes of firmware
    e.on_packet(size_pkt, sizeof(size_pkt));
    // Deferred Start response must arrive now
    expect(g_tx.size() == 1u &&
               g_tx.back().size() == 3u &&
               g_tx.back()[0] == slate::dfu::kRspCode &&
               g_tx.back()[1] == slate::dfu::kOpStartDfu &&
               g_tx.back()[2] == slate::dfu::kRspSuccess,
           "dfu: Start response after size pkt");
    expect(e.state() == slate::dfu::State::ExpectInit, "dfu: ExpectInit");

    // 3. Init start (0x02 0x00) — no response
    g_tx.clear();
    const std::uint8_t ctrl_init_start[] = {slate::dfu::kOpInitDfuParams, 0x00};
    e.on_control(ctrl_init_start, sizeof(ctrl_init_start));
    expect(g_tx.empty(), "dfu: no response on init start");
    expect(e.state() == slate::dfu::State::InitReceiving, "dfu: InitReceiving");

    // 4. Init packet bytes (raw .dat, 14 bytes typical; last 2 bytes = CRC-16)
    //    We compute CRC of 8 bytes {1..8} in advance.
    //    CRC-16-CCITT of {1,2,3,4,5,6,7,8}:
    static const std::uint8_t fw_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    // Compute CRC manually to match Engine's local crc16_update
    std::uint16_t expected_crc = 0xFFFFu;
    for (std::size_t i = 0; i < 8; ++i) {
      std::uint16_t c = expected_crc;
      c = static_cast<std::uint16_t>(((c >> 8) & 0xFFu) | ((c << 8) & 0xFFFFu));
      c ^= static_cast<std::uint16_t>(fw_data[i]);
      c ^= static_cast<std::uint16_t>((c & 0xFFu) >> 4);
      c ^= static_cast<std::uint16_t>((c << 12) & 0xFFFFu);
      c ^= static_cast<std::uint16_t>(((c & 0xFFu) << 5) & 0xFFFFu);
      expected_crc = c;
    }
    std::uint8_t init_pkt[14] = {};  // zeroed, last 2 bytes = CRC-16
    init_pkt[12] = static_cast<std::uint8_t>(expected_crc & 0xFFu);
    init_pkt[13] = static_cast<std::uint8_t>((expected_crc >> 8) & 0xFFu);
    e.on_packet(init_pkt, sizeof(init_pkt));

    // 5. Init complete (0x02 0x01) → response 0x10 0x02 0x01
    const std::uint8_t ctrl_init_end[] = {slate::dfu::kOpInitDfuParams, 0x01};
    e.on_control(ctrl_init_end, sizeof(ctrl_init_end));
    expect(g_tx.size() == 1u &&
               g_tx.back()[0] == slate::dfu::kRspCode &&
               g_tx.back()[1] == slate::dfu::kOpInitDfuParams &&
               g_tx.back()[2] == slate::dfu::kRspSuccess,
           "dfu: Init response");

    // 6. Set PRN = 4 → response 0x10 0x08 0x01
    g_tx.clear();
    const std::uint8_t ctrl_prn[] = {slate::dfu::kOpSetPrn, 4};
    e.on_control(ctrl_prn, sizeof(ctrl_prn));
    expect(g_tx.size() == 1u &&
               g_tx.back()[1] == slate::dfu::kOpSetPrn &&
               g_tx.back()[2] == slate::dfu::kRspSuccess,
           "dfu: PRN set response");

    // 7. Receive Firmware → response 0x10 0x03 0x01
    g_tx.clear();
    const std::uint8_t ctrl_recv[] = {slate::dfu::kOpReceiveFw};
    e.on_control(ctrl_recv, sizeof(ctrl_recv));
    expect(g_tx.size() == 1u &&
               g_tx.back()[1] == slate::dfu::kOpReceiveFw &&
               g_tx.back()[2] == slate::dfu::kRspSuccess,
           "dfu: ReceiveFw response");

    // 8. Send 8 bytes in two 20-byte writes (each 4 bytes here for simplicity)
    //    With PRN=4 a receipt notification should fire after 4 packets.
    g_tx.clear();
    for (int i = 0; i < 8; ++i) {
      const std::uint8_t byte_pkt[1] = {fw_data[i]};
      e.on_packet(byte_pkt, sizeof(byte_pkt));
    }
    // PRN=4 → we sent 8 packets → 2 receipts
    int receipt_count = 0;
    for (const auto& t : g_tx) {
      if (!t.empty() && t[0] == slate::dfu::kRspPktReceipt) {
        ++receipt_count;
      }
    }
    expect(receipt_count == 2, "dfu: 2 PRN receipts at interval 4");
    expect(e.received() == 8u, "dfu: all 8 bytes received");

    // 9. Validate → 0x10 0x04 0x01
    g_tx.clear();
    const std::uint8_t ctrl_val[] = {slate::dfu::kOpValidate};
    e.on_control(ctrl_val, sizeof(ctrl_val));
    expect(!g_tx.empty() &&
               g_tx.back()[1] == slate::dfu::kOpValidate &&
               g_tx.back()[2] == slate::dfu::kRspSuccess,
           "dfu: Validate success");
    expect(e.state() == slate::dfu::State::Validated, "dfu: Validated");
    expect(magic_written, "dfu: pending magic written at validate");

    // 10. Activate & Reset
    g_tx.clear();
    const std::uint8_t ctrl_act[] = {slate::dfu::kOpActivateReset};
    e.on_control(ctrl_act, sizeof(ctrl_act));
    expect(g_rebooted, "dfu: activate reboot");

    // Verify slot contents
    expect(std::memcmp(g_slot.data(), fw_data, 8) == 0, "dfu: slot bytes correct");
  }

  // ── Nordic DFU: CRC failure ───────────────────────────────────────────────
  {
    g_batt = 90;
    g_rebooted = false;
    g_tx.clear();

    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.write_pending_magic = [](void*) { return true; };
    h.battery_percent = batt;
    h.reboot = [](void*) { g_rebooted = true; };
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    // Start → size pkt (4 bytes firmware)
    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    std::uint8_t size_pkt[12] = {};
    wr_u32(size_pkt + 8, 4u);
    e.on_packet(size_pkt, sizeof(size_pkt));

    // Init: wrong CRC in init packet (all zeros)
    const std::uint8_t ci0[] = {slate::dfu::kOpInitDfuParams, 0x00};
    e.on_control(ci0, sizeof(ci0));
    std::uint8_t bad_init[14] = {};  // all zeros, CRC = 0x0000
    e.on_packet(bad_init, sizeof(bad_init));
    const std::uint8_t ci1[] = {slate::dfu::kOpInitDfuParams, 0x01};
    e.on_control(ci1, sizeof(ci1));
    const std::uint8_t ctrl_recv[] = {slate::dfu::kOpReceiveFw};
    e.on_control(ctrl_recv, sizeof(ctrl_recv));

    // Send {0xAA, 0xBB, 0xCC, 0xDD}
    const std::uint8_t pkt[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    e.on_packet(pkt, sizeof(pkt));

    // Validate: CRC mismatch expected
    g_tx.clear();
    const std::uint8_t ctrl_val[] = {slate::dfu::kOpValidate};
    e.on_control(ctrl_val, sizeof(ctrl_val));
    expect(!g_tx.empty() &&
               g_tx.back()[1] == slate::dfu::kOpValidate &&
               g_tx.back()[2] == slate::dfu::kRspCrcError,
           "dfu: CRC mismatch → kRspCrcError");
    expect(!g_rebooted, "dfu: no reboot on CRC fail");
  }

  // ── Nordic DFU: abort clears state ───────────────────────────────────────
  {
    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.write_pending_magic = [](void*) { return true; };
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    std::uint8_t size_pkt[12] = {};
    wr_u32(size_pkt + 8, 8u);
    e.on_packet(size_pkt, sizeof(size_pkt));

    // Abort mid-transfer
    const std::uint8_t ctrl_abort[] = {slate::dfu::kOpAbort};
    e.on_control(ctrl_abort, sizeof(ctrl_abort));
    expect(e.state() == slate::dfu::State::Idle, "dfu: abort → Idle");
    expect(e.received() == 0u, "dfu: abort clears received");
  }

  // ── Nordic DFU: disconnect clears state ───────────────────────────────────
  {
    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.write_pending_magic = [](void*) { return true; };
    e.init(h);

    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    std::uint8_t size_pkt[12] = {};
    wr_u32(size_pkt + 8, 8u);
    e.on_packet(size_pkt, sizeof(size_pkt));
    expect(e.state() == slate::dfu::State::ExpectInit, "dfu: mid-transfer before disconnect");
    e.on_disconnect();
    expect(e.state() == slate::dfu::State::Idle, "dfu: disconnect → Idle");
  }

  // ── Nordic DFU: size rejection ────────────────────────────────────────────
  {
    g_tx.clear();
    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    // Size pkt with app_size = 0 (invalid)
    std::uint8_t size_pkt[12] = {};
    e.on_packet(size_pkt, sizeof(size_pkt));
    expect(!g_tx.empty() &&
               g_tx.back()[2] == slate::dfu::kRspDataSize,
           "dfu: zero size → kRspDataSize");
    expect(e.state() == slate::dfu::State::Idle, "dfu: size reject → Idle");
  }

  // ── Nordic DFU: wrong type rejection ─────────────────────────────────────
  {
    g_tx.clear();
    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    const std::uint8_t ctrl_wrong_type[] = {slate::dfu::kOpStartDfu, 0x01};  // SoftDevice only
    e.on_control(ctrl_wrong_type, sizeof(ctrl_wrong_type));
    expect(!g_tx.empty() &&
               g_tx.back()[2] == slate::dfu::kRspNotSupported,
           "dfu: wrong type → kRspNotSupported");
  }

  // ── Nordic DFU: low battery gate ─────────────────────────────────────────
  {
    g_batt = 20;
    g_charging = false;
    g_tx.clear();
    slate::dfu::Engine e;
    slate::dfu::Hooks h;
    h.erase_slot = erase_slot;
    h.write_slot = write_slot;
    h.battery_percent = batt;
    h.charging = chg;
    h.notify_rsp = [](const std::uint8_t* rsp, std::size_t len, void*) {
      g_tx.emplace_back(rsp, rsp + len);
    };
    e.init(h);

    const std::uint8_t ctrl_start[] = {slate::dfu::kOpStartDfu, 0x04};
    e.on_control(ctrl_start, sizeof(ctrl_start));
    expect(!g_tx.empty() &&
               g_tx.back()[2] == slate::dfu::kRspOpFailed,
           "dfu: low battery → kRspOpFailed");
    expect(e.state() == slate::dfu::State::Idle, "dfu: low battery → Idle");
    g_batt = 50;
  }

  if (g_fails == 0) {
    std::printf("test_ota_xfer: OK\n");
    return 0;
  }
  std::printf("test_ota_xfer: %d failures\n", g_fails);
  return 1;
}
