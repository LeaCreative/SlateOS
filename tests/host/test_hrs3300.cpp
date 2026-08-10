// HRS3300 driver: InfiniTime register sequences against a mock TWI bus.

#include "hrs3300.hpp"
#include "hr_controller.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_fails = 0;

void expect(const char* name, bool ok) {
  if (!ok) {
    std::printf("FAIL %s\n", name);
    ++g_fails;
  } else {
    std::printf("ok   %s\n", name);
  }
}

struct MockBus {
  std::uint8_t regs[0x20] = {};
  std::vector<std::pair<std::uint8_t, std::uint8_t>> writes;
};

bool mock_write(std::uint8_t reg, const std::uint8_t* data, std::size_t len,
                void* ctx) {
  auto* m = static_cast<MockBus*>(ctx);
  if (data == nullptr || len == 0u || reg >= sizeof(m->regs)) {
    return false;
  }
  m->regs[reg] = data[0];
  m->writes.emplace_back(reg, data[0]);
  return true;
}

bool mock_read(std::uint8_t reg, std::uint8_t* data, std::size_t len,
               void* ctx) {
  auto* m = static_cast<MockBus*>(ctx);
  if (data == nullptr || reg + len > sizeof(m->regs)) {
    return false;
  }
  for (std::size_t i = 0u; i < len; ++i) {
    data[i] = m->regs[reg + i];
  }
  return true;
}

void test_init_disable_sequence() {
  MockBus mock;
  mock.regs[0x01] = 0x68u;  // power-on default-ish
  slate::hrs::Bus bus;
  bus.write_reg = &mock_write;
  bus.read_reg = &mock_read;
  bus.ctx = &mock;

  slate::hrs::Driver drv;
  drv.init(bus);

  expect("init wrote Enable 0x50", !mock.writes.empty());
  bool saw_pdriver_zero = false;
  bool saw_enable_50 = false;
  bool saw_res_77 = false;
  for (const auto& w : mock.writes) {
    if (w.first == 0x0Cu && w.second == 0x00u) {
      saw_pdriver_zero = true;
    }
    if (w.first == 0x01u && w.second == 0x50u) {
      saw_enable_50 = true;
    }
    if (w.first == 0x16u && w.second == 0x77u) {
      saw_res_77 = true;
    }
  }
  expect("init sleeps via PDRIVER=0", saw_pdriver_zero);
  expect("init programs Enable 0x50", saw_enable_50);
  expect("init programs RES 0x77", saw_res_77);
  expect("driver reports disabled", !drv.enabled());
}

void test_enable_disable() {
  MockBus mock;
  mock.regs[0x01] = 0x50u;
  slate::hrs::Bus bus;
  bus.write_reg = &mock_write;
  bus.read_reg = &mock_read;
  bus.ctx = &mock;

  slate::hrs::Driver drv;
  drv.init(bus);
  mock.writes.clear();
  drv.enable();
  expect("enable sets HEN bit", (mock.regs[0x01] & 0x80u) != 0u);
  expect("enable sets PDRIVER drive", mock.regs[0x0C] == slate::hrs::kLedDrive);
  expect("enabled()", drv.enabled());

  mock.writes.clear();
  drv.disable();
  expect("disable clears HEN", (mock.regs[0x01] & 0x80u) == 0u);
  expect("disable PDRIVER=0", mock.regs[0x0C] == 0u);
  expect("!enabled()", !drv.enabled());
}

void test_unpack_hrs_als() {
  MockBus mock;
  slate::hrs::Bus bus;
  bus.write_reg = &mock_write;
  bus.read_reg = &mock_read;
  bus.ctx = &mock;

  // Fabricate CH0 (HRS) and CH1 (ALS) register values.
  mock.regs[0x09] = 0x12u;  // C0DataM
  mock.regs[0x0A] = 0x03u;  // C0DataH low nibble used
  mock.regs[0x0F] = 0x04u;  // C0dataL low nibble
  mock.regs[0x08] = 0x01u;  // C1dataM
  mock.regs[0x0D] = 0x02u;  // C1dataH
  mock.regs[0x0E] = 0x05u;  // C1dataL

  slate::hrs::Driver drv;
  drv.init(bus);
  const auto sample = drv.read_hrs_als();
  // hrs = (0x12<<8) | ((0x03&0x0f)<<4) | (0x04&0x0f) = 0x1234
  expect("hrs unpack", sample.hrs == 0x1234u);
  // als = ((0x02&0x3f)<<11) | (0x01<<3) | (0x05&0x07) = 0x1000 | 0x08 | 0x05
  expect("als unpack", sample.als == 0x100Du);
}

void test_hrs_measurement_encode() {
  std::uint8_t buf[2] = {0xFFu, 0xFFu};
  expect("encode length 2", slate::hr::encode_measurement(72u, buf) == 2u);
  expect("flags byte 0", buf[0] == 0u);
  expect("bpm byte", buf[1] == 72u);
}

}  // namespace

int main() {
  test_init_disable_sequence();
  test_enable_disable();
  test_unpack_hrs_als();
  test_hrs_measurement_encode();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all ok\n");
  return 0;
}
