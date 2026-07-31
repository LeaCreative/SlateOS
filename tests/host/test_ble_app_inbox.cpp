#include "ble_link.hpp"
#include "sdp_frame.hpp"
#include "sdp_opcodes.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

void expect_true(const char* name, bool cond) {
  if (!cond) {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

void expect_eq(const char* name, unsigned got, unsigned want) {
  if (got != want) {
    std::printf("FAIL %s: got %u want %u\n", name, got, want);
    ++g_failures;
  } else {
    std::printf("PASS %s\n", name);
  }
}

enum class Ctx : std::uint8_t { None = 0, Host, App };

Ctx g_ctx = Ctx::None;
int g_handler_host = 0;
int g_handler_app = 0;
int g_apply_host = 0;
int g_apply_app = 0;
std::uint8_t g_last_ch = 0xFF;
std::vector<std::uint8_t> g_last_msg;

void on_app(std::uint8_t ch, const std::uint8_t* msg, std::size_t len, void*) {
  if (g_ctx == Ctx::Host) {
    ++g_handler_host;
  } else if (g_ctx == Ctx::App) {
    ++g_handler_app;
  }
  g_last_ch = ch;
  g_last_msg.assign(msg, msg + len);
  // Simulate interpreter/apply ownership — must only run under App.
  if (g_ctx == Ctx::Host) {
    ++g_apply_host;
  } else if (g_ctx == Ctx::App) {
    ++g_apply_app;
  }
}

void push_complete(ble::Link& link, std::uint8_t channel, const std::uint8_t* payload,
                   std::size_t len) {
  std::uint8_t seq = 0u;
  std::vector<std::vector<std::uint8_t>> pkts;
  auto save = [](const std::uint8_t* p, std::size_t n, void* ctx) -> bool {
    auto* v = static_cast<std::vector<std::vector<std::uint8_t>>*>(ctx);
    v->emplace_back(p, p + n);
    return true;
  };
  expect_true("fragment ok",
              sdp::frame::fragment_message(channel, 0, &seq, payload, len, save,
                                           &pkts));
  g_ctx = Ctx::Host;
  for (const auto& p : pkts) {
    link.on_rx_write(p.data(), p.size());
  }
  g_ctx = Ctx::None;
}

void test_inbox_ram_budget() {
  // N-8: zero-copy borrow of the reassembler buffer — metadata only. The
  // original 4 KiB copy slot pushed heap past the MSP stack reserve and the
  // ARM image no longer linked.
  expect_true("inbox <= 16 B", ble::Link::inbox_storage_bytes() <= 16u);
  std::printf("  inbox_storage_bytes=%zu (zero-copy: pointer + metadata)\n",
              ble::Link::inbox_storage_bytes());
}

void test_host_does_not_dispatch() {
  g_handler_host = g_handler_app = 0;
  g_apply_host = g_apply_app = 0;
  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);

  const std::uint8_t payload[] = {sdp::op::CLEAR, 0x00, 0x00, 0x00, sdp::op::COMMIT,
                                  0x00};
  push_complete(link, sdp::frame::kChanDisplay, payload, sizeof(payload));

  expect_eq("no handler on host ingest", static_cast<unsigned>(g_handler_host), 0u);
  expect_eq("no apply on host ingest", static_cast<unsigned>(g_apply_host), 0u);
  expect_true("inbox busy after host push", link.inbox_busy());

  g_ctx = Ctx::App;
  expect_true("drain delivers", link.drain_app_messages());
  g_ctx = Ctx::None;

  expect_eq("handler on app drain", static_cast<unsigned>(g_handler_app), 1u);
  expect_eq("apply on app drain", static_cast<unsigned>(g_apply_app), 1u);
  expect_eq("channel display", g_last_ch, sdp::frame::kChanDisplay);
  expect_true("payload preserved",
              g_last_msg.size() == sizeof(payload) &&
                  std::memcmp(g_last_msg.data(), payload, sizeof(payload)) == 0);
  expect_true("inbox free after drain", !link.inbox_busy());
  expect_true("second drain empty", !link.drain_app_messages());
}

void test_busy_drops_second() {
  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);

  const std::uint8_t a[] = {0x01, 0x02};
  const std::uint8_t b[] = {0x03, 0x04, 0x05};
  push_complete(link, sdp::frame::kChanControl, a, sizeof(a));
  expect_eq("handoff drops 0", link.handoff_drop_count(), 0u);

  push_complete(link, sdp::frame::kChanControl, b, sizeof(b));
  expect_eq("handoff drops 1", link.handoff_drop_count(), 1u);

  g_ctx = Ctx::App;
  g_handler_app = 0;
  expect_true("drain first only", link.drain_app_messages());
  expect_eq("still first payload len", static_cast<unsigned>(g_last_msg.size()), 2u);
  expect_eq("first byte", g_last_msg[0], 0x01u);
  g_ctx = Ctx::None;
}

void test_control_ordering_with_display() {
  // Single global slot ⇒ FIFO across channels; CONTROL before DISPLAY if
  // drained in order of arrival (second drops while busy).
  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);
  const std::uint8_t ctrl[] = {sdp::control_op::HEARTBEAT};
  push_complete(link, sdp::frame::kChanControl, ctrl, sizeof(ctrl));
  g_ctx = Ctx::App;
  link.drain_app_messages();
  expect_eq("got control", g_last_ch, sdp::frame::kChanControl);
  g_ctx = Ctx::None;

  const std::uint8_t disp[] = {sdp::op::COMMIT, 0x00};
  push_complete(link, sdp::frame::kChanDisplay, disp, sizeof(disp));
  g_ctx = Ctx::App;
  link.drain_app_messages();
  expect_eq("got display after", g_last_ch, sdp::frame::kChanDisplay);
  g_ctx = Ctx::None;
}

void test_wake_callback() {
  int wakes = 0;
  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);
  link.set_app_wake(
      [](void* ctx) {
        *static_cast<int*>(ctx) += 1;
      },
      &wakes);
  const std::uint8_t p[] = {0x0A};
  push_complete(link, sdp::frame::kChanSystem, p, sizeof(p));
  expect_eq("wake once", static_cast<unsigned>(wakes), 1u);
  push_complete(link, sdp::frame::kChanSystem, p, sizeof(p));
  expect_eq("no wake on drop", static_cast<unsigned>(wakes), 1u);
}

// Simulate concurrent NimBLE host ingest + app-task local renders: host may
// only enqueue; interpreter/"apply" and local paint share the App context.
void test_concurrent_host_ingest_app_render() {
  g_handler_host = g_handler_app = 0;
  g_apply_host = g_apply_app = 0;
  int local_paints = 0;
  int paint_while_apply_host = 0;

  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);

  const std::uint8_t payload[] = {sdp::op::CLEAR, 0x00, 0x00, 0x00, sdp::op::COMMIT,
                                  0x00};

  for (int i = 0; i < 8; ++i) {
    // Host: ingest (must not apply).
    push_complete(link, sdp::frame::kChanDisplay, payload, sizeof(payload));
    expect_eq("still no host apply", static_cast<unsigned>(g_apply_host), 0u);

    // App: local face paint (same ownership as drain).
    g_ctx = Ctx::App;
    ++local_paints;
    if (g_apply_host != 0) {
      ++paint_while_apply_host;
    }

    // App: drain remote list (apply only here).
    if (link.inbox_busy()) {
      (void)link.drain_app_messages();
    }
    g_ctx = Ctx::None;
  }

  expect_eq("all applies on app", static_cast<unsigned>(g_apply_app), 8u);
  expect_eq("zero host applies", static_cast<unsigned>(g_apply_host), 0u);
  expect_eq("local paints", static_cast<unsigned>(local_paints), 8u);
  expect_eq("no paint under host apply", static_cast<unsigned>(paint_while_apply_host),
            0u);
  expect_eq("no host handler", static_cast<unsigned>(g_handler_host), 0u);
}

// While a message is pending, every arriving fragment is dropped (borrowed
// buffer must stay stable); after drain, leftover continuations are dropped
// by the reassembler and a fresh message goes through cleanly.
void test_busy_window_then_resync() {
  ble::Link link;
  link.init(false);
  link.set_app_handler(&on_app, nullptr);

  const std::uint8_t a[] = {0x11, 0x22};
  push_complete(link, sdp::frame::kChanControl, a, sizeof(a));

  // Multi-fragment message lands entirely inside the busy window.
  std::vector<std::uint8_t> big(400, 0x5A);
  std::uint8_t seq = 0u;
  std::vector<std::vector<std::uint8_t>> pkts;
  auto save = [](const std::uint8_t* p, std::size_t n, void* ctx) -> bool {
    auto* v = static_cast<std::vector<std::vector<std::uint8_t>>*>(ctx);
    v->emplace_back(p, p + n);
    return true;
  };
  sdp::frame::fragment_message(sdp::frame::kChanDisplay, 0, &seq, big.data(),
                               big.size(), save, &pkts);
  expect_true("busy multi >=2", pkts.size() >= 2u);
  g_ctx = Ctx::Host;
  link.on_rx_write(pkts[0].data(), pkts[0].size());  // FIRST dropped by gate
  g_ctx = Ctx::None;
  expect_eq("first frag counted", link.handoff_drop_count(), 1u);

  // Drain frees the slot mid-message.
  g_ctx = Ctx::App;
  expect_true("drain a", link.drain_app_messages());
  expect_eq("a delivered intact", static_cast<unsigned>(g_last_msg.size()), 2u);
  g_ctx = Ctx::None;

  // Leftover continuations: reassembler has no state → Dropped, not stitched.
  g_ctx = Ctx::Host;
  for (std::size_t i = 1; i < pkts.size(); ++i) {
    link.on_rx_write(pkts[i].data(), pkts[i].size());
  }
  g_ctx = Ctx::None;
  expect_true("no phantom message", !link.inbox_busy());

  // Fresh message resyncs.
  const std::uint8_t fresh[] = {0x77};
  push_complete(link, sdp::frame::kChanControl, fresh, sizeof(fresh));
  g_ctx = Ctx::App;
  expect_true("fresh delivered", link.drain_app_messages());
  expect_eq("fresh byte", g_last_msg[0], 0x77u);
  g_ctx = Ctx::None;
}

}  // namespace

int main() {
  test_inbox_ram_budget();
  test_busy_window_then_resync();
  test_host_does_not_dispatch();
  test_busy_drops_second();
  test_control_ordering_with_display();
  test_wake_callback();
  test_concurrent_host_ingest_app_render();
  if (g_failures != 0) {
    std::printf("%d FAILURES\n", g_failures);
    return 1;
  }
  std::printf("all ble_app_inbox / handoff tests passed\n");
  return 0;
}
