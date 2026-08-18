// The BMA driver against a sensor model that behaves like the real part.
//
// Two defects shipped because nothing here modelled the sensor: the feature
// block was read and written eight bytes at a time with the ASIC address set
// only once, and the driver reported the axes in the chip's frame rather than
// the watch's. Both reported success. The step count stayed at zero for the
// whole project and raise-to-wake never fired.
//
// The model below is deliberately strict about the one thing the driver got
// wrong: FEATURES_IN (0x5E) reads and writes at whatever address 0x5B/0x5C last
// named, and that address does NOT survive the end of a transfer. Bosch's own
// driver calls increment_feature_config_addr() after every chunk for exactly
// this reason.

#include "bma42x.hpp"

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

constexpr std::uint8_t kRegChipId = 0x00u;
constexpr std::uint8_t kRegAccData = 0x12u;
constexpr std::uint8_t kRegInternalStatus = 0x2Au;
constexpr std::uint8_t kRegAsicLsb = 0x5Bu;
constexpr std::uint8_t kRegAsicMsb = 0x5Cu;
constexpr std::uint8_t kRegFeaturesIn = 0x5Eu;
constexpr std::uint8_t kRegInitCtrl = 0x59u;
constexpr std::uint8_t kRegCmd = 0x7Eu;
constexpr std::uint8_t kRegPowerCtrl = 0x7Du;
constexpr std::uint8_t kRegPowerConf = 0x7Cu;

constexpr std::size_t kFeatureSize = 70u;
constexpr std::size_t kStepCounterByte = 0x3Au + 1u;
constexpr std::uint8_t kStepCounterEnMask = 0x10u;

/** A BMA425 that enforces the parts of the datasheet the driver got wrong. */
struct FakeBma {
  std::uint8_t regs[256] = {};
  /** The real feature block, addressed only through 0x5B/0x5C + 0x5E. */
  std::uint8_t feature[kFeatureSize] = {};
  /** Where the next FEATURES_IN byte goes, in bytes. */
  std::size_t asic_addr = 0u;
  /** Config-stream landing area, so a mis-addressed upload is visible. */
  std::vector<std::uint8_t> stream;
  bool stream_open = false;
  int feature_reads = 0;
  int feature_writes = 0;
  std::size_t longest_feature_op = 0u;
  std::int16_t accel_x = 0, accel_y = 0, accel_z = 0;

  FakeBma() {
    regs[kRegChipId] = 0x13u;            // BMA425
    regs[kRegInternalStatus] = 0x01u;    // ASIC initialised
    regs[kRegPowerCtrl] = 0x04u;         // accel enabled (InfiniTime PWR_CTRL)
    regs[kRegPowerConf] = 0x03u;         // APS on, fifo_self_wakeup on
  }

  void set_accel(std::int16_t x, std::int16_t y, std::int16_t z) {
    accel_x = x;
    accel_y = y;
    accel_z = z;
  }

  static void put12(std::uint8_t* p, std::int16_t v) {
    // 12-bit left-justified in a little-endian pair, as the part reports it.
    const std::uint16_t raw = static_cast<std::uint16_t>(v * 16);
    p[0] = static_cast<std::uint8_t>(raw & 0xFFu);
    p[1] = static_cast<std::uint8_t>(raw >> 8);
  }

  bool read(std::uint8_t reg, std::uint8_t* data, std::size_t len) {
    if (reg == kRegAccData) {
      if (len < 6u) return false;
      if ((regs[kRegPowerCtrl] & 0x04u) == 0u) {
        // Suspend / acc_en off: the part ACKs and returns zeros (N-61).
        for (std::size_t i = 0u; i < 6u; ++i) data[i] = 0u;
        return true;
      }
      put12(data + 0, accel_x);
      put12(data + 2, accel_y);
      put12(data + 4, accel_z);
      return true;
    }
    if (reg == kRegFeaturesIn) {
      ++feature_reads;
      if (len > longest_feature_op) longest_feature_op = len;
      for (std::size_t i = 0u; i < len; ++i) {
        const std::size_t a = asic_addr + i;
        data[i] = (a < kFeatureSize) ? feature[a] : 0u;
      }
      // The address advances WITHIN a transfer and is then forgotten: the next
      // transfer starts wherever 0x5B/0x5C point, not where this one ended.
      return true;
    }
    for (std::size_t i = 0u; i < len; ++i) {
      data[i] = regs[static_cast<std::uint8_t>(reg + i)];
    }
    return true;
  }

  bool write(std::uint8_t reg, const std::uint8_t* data, std::size_t len) {
    if (reg == kRegFeaturesIn) {
      ++feature_writes;
      if (len > longest_feature_op) longest_feature_op = len;
      if (stream_open) {
        // Config upload: land it where the ASIC address says, so an upload that
        // never moves the address piles up at offset 0 and is detectable.
        if (stream.size() < asic_addr + len) stream.resize(asic_addr + len, 0u);
        for (std::size_t i = 0u; i < len; ++i) stream[asic_addr + i] = data[i];
        return true;
      }
      for (std::size_t i = 0u; i < len; ++i) {
        const std::size_t a = asic_addr + i;
        if (a < kFeatureSize) feature[a] = data[i];
      }
      return true;
    }
    for (std::size_t i = 0u; i < len; ++i) {
      regs[static_cast<std::uint8_t>(reg + i)] = data[i];
    }
    if (reg == kRegAsicLsb || reg == kRegAsicMsb) {
      // Address is (msb << 4 | lsb) in WORDS — bytes = words * 2.
      const std::size_t words =
          (static_cast<std::size_t>(regs[kRegAsicMsb]) << 4) |
          (regs[kRegAsicLsb] & 0x0Fu);
      asic_addr = words * 2u;
    }
    if (reg == kRegInitCtrl) {
      stream_open = (data[0] == 0x00u);
      if (!stream_open) {
        // Closing the stream boots the ASIC, which then reports the feature
        // config start address in 0x5B/0x5C — this is what Bosch's
        // get_feature_config_start_addr reads. Modelled as offset 0 of the
        // feature block.
        regs[kRegAsicLsb] = 0u;
        regs[kRegAsicMsb] = 0u;
        asic_addr = 0u;
      }
    }
    if (reg == kRegCmd && data[0] == 0xB6u) {
      std::memset(feature, 0, sizeof(feature));
      asic_addr = 0u;
    }
    return true;
  }
};

FakeBma* g_dev = nullptr;

bool bus_write(std::uint8_t reg, const std::uint8_t* d, std::size_t n, void*) {
  return g_dev->write(reg, d, n);
}
bool bus_read(std::uint8_t reg, std::uint8_t* d, std::size_t n, void*) {
  return g_dev->read(reg, d, n);
}
void bus_delay(std::uint32_t, void*) {}

slate::bma::Bus make_bus() {
  slate::bma::Bus b;
  b.write_reg = &bus_write;
  b.read_reg = &bus_read;
  b.delay_ms = &bus_delay;
  return b;
}

/**
 * The defect that kept the step count at zero.
 *
 * The driver must set the step-counter enable bit at byte 0x3B of the REAL
 * feature block. Reading and writing the block in chunks without re-addressing
 * meant every chunk hit bytes 0-7: the bit went into a copy of byte 3, the
 * write-back corrupted the head of the config, and the verification read the
 * same wrong bytes and passed. Success was reported for a pedometer that was
 * switched off.
 */
void test_step_counter_bit_lands_in_the_real_block() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());
  expect("chip detected as BMA425", drv.chip() == slate::bma::Chip::BMA425);

  // A recognisable pattern, so a mis-addressed access is unmistakable. The
  // enable bit is cleared explicitly: with a plain i-th-byte pattern, byte 0x3B
  // is 0x3B, which already carries 0x10 — the assertion would pass without the
  // driver doing anything at all.
  for (std::size_t i = 0u; i < kFeatureSize; ++i) {
    dev.feature[i] = static_cast<std::uint8_t>(i);
  }
  dev.feature[kStepCounterByte] &= static_cast<std::uint8_t>(~kStepCounterEnMask);
  expect("fixture starts with the enable bit clear",
         (dev.feature[kStepCounterByte] & kStepCounterEnMask) == 0u);

  // configure() is the real entry point — Core::init calls exactly this.
  std::vector<std::uint8_t> cfg(256u, 0xAAu);
  (void)drv.configure(cfg.data(), cfg.size(), 3u);
  expect("the enable bit is set in the real feature block",
         (dev.feature[kStepCounterByte] & kStepCounterEnMask) != 0u);
  expect("driver agrees the counter is on", drv.step_counter_enabled());

  // Everything else must survive the read-modify-write untouched. Chunked
  // access without re-addressing overwrote the head of the block.
  bool rest_intact = true;
  for (std::size_t i = 0u; i < kFeatureSize; ++i) {
    const std::uint8_t base = static_cast<std::uint8_t>(i);
    const std::uint8_t want =
        (i == kStepCounterByte)
            ? static_cast<std::uint8_t>(
                  (base & static_cast<std::uint8_t>(~kStepCounterEnMask)) |
                  kStepCounterEnMask)
            : base;
    if (dev.feature[i] != want) {
      rest_intact = false;
      std::printf("     feature[%zu] = 0x%02X, want 0x%02X\n", i, dev.feature[i],
                  want);
      break;
    }
  }
  expect("the rest of the feature config is unharmed", rest_intact);
}

/** A refusal must not be reported as success — the original sin here. */
void test_failed_enable_is_not_reported_as_success() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());

  // The sensor accepts the config stream but refuses the feature block, which
  // is the case the old code reported as success.
  struct Deaf {
    static bool wr(std::uint8_t reg, const std::uint8_t* d, std::size_t n, void*) {
      if (reg == kRegFeaturesIn && !g_dev->stream_open) return false;
      return g_dev->write(reg, d, n);
    }
  };
  slate::bma::Bus b = make_bus();
  b.write_reg = &Deaf::wr;
  drv.init(b);

  std::vector<std::uint8_t> cfg(256u, 0xAAu);
  (void)drv.configure(cfg.data(), cfg.size(), 3u);
  expect("the driver does not claim the counter is on",
         !drv.step_counter_enabled());
  expect("so read_steps stays honest", drv.read_steps() == 0u);
}

/**
 * The axes must arrive in the watch's frame, not the chip's.
 *
 * The BMA is mounted rotated in the PineTime, so InfiniTime's Bma421::Process
 * returns {steps, data.y, data.x, data.z} — its X is the sensor's Y. The
 * raise-to-wake thresholds are InfiniTime's and are expressed in that frame, so
 * handing them the chip's frame tests the wrong axis: the arm-level check reads
 * a value that is near ±1g on a wrist and rejects every gesture.
 */
void test_axes_are_reported_in_the_watch_frame() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());

  dev.set_accel(/*chip x=*/100, /*chip y=*/-200, /*chip z=*/300);
  std::int16_t x = 0, y = 0, z = 0;
  expect("accel read succeeds", drv.read_accel(&x, &y, &z));
  expect("watch x is the chip's y", x == -200);
  expect("watch y is the chip's x", y == 100);
  expect("z is unchanged", z == 300);
}

/** 1 g must read as 1024, the scale the thresholds were written against. */
void test_accel_scaling_is_1024_per_g() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());
  dev.set_accel(0, 0, 1024);
  std::int16_t x = 0, y = 0, z = 0;
  (void)drv.read_accel(&x, &y, &z);
  expect("1 g reads as 1024", z == 1024);
  dev.set_accel(0, 0, -1024);
  (void)drv.read_accel(&x, &y, &z);
  expect("-1 g reads as -1024", z == -1024);
}

/**
 * The config upload must not pile every chunk onto the same address.
 *
 * This is the same class of bug one layer up, and it is already fixed; the test
 * exists so it cannot come back silently.
 */
void test_config_upload_is_spread_across_the_stream() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());

  std::vector<std::uint8_t> cfg(512u);
  for (std::size_t i = 0u; i < cfg.size(); ++i) {
    cfg[i] = static_cast<std::uint8_t>(i & 0xFFu);
  }
  (void)drv.configure(cfg.data(), cfg.size(), 3u);

  expect("the whole stream was written, not one chunk repeatedly",
         dev.stream.size() >= cfg.size());
  bool matches = dev.stream.size() >= cfg.size();
  for (std::size_t i = 0u; matches && i < cfg.size(); ++i) {
    if (dev.stream[i] != cfg[i]) {
      matches = false;
      std::printf("     stream[%zu] = 0x%02X, want 0x%02X\n", i, dev.stream[i],
                  cfg[i]);
    }
  }
  expect("and it landed byte for byte", matches);
}

/**
 * Accel config must match InfiniTime: 100 Hz, NORMAL_AVG4, CIC_AVG → 0x28.
 * Continuous mode (0xA8) was a silent divergence after the pedometer enable
 * path was fixed; soft step counts followed.
 */
void test_acc_conf_matches_infinitime() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());
  std::vector<std::uint8_t> cfg(8u, 0u);
  (void)drv.configure(cfg.data(), cfg.size(), 3u);
  expect("ACC_CONF is InfiniTime's 0x28 (not continuous 0xA8)",
         dev.regs[0x40] == 0x28u);
  expect("PWR_CONF keeps fifo_self_wakeup (0x03, not 0x01)",
         dev.regs[kRegPowerConf] == 0x03u);
  expect("PWR_CTRL has acc_en", (dev.regs[kRegPowerCtrl] & 0x04u) != 0u);
}

/**
 * All-zero ACC_DATA with a live ACK is how raise-to-wake dies while the chip
 * id still looks healthy. read_accel must re-enable the accelerometer and
 * retry rather than feeding the detector a gravity-free sample forever.
 */
void test_dead_accel_is_revived() {
  FakeBma dev;
  g_dev = &dev;
  slate::bma::Driver drv;
  drv.init(make_bus());
  dev.set_accel(0, 0, 1024);
  dev.regs[kRegPowerCtrl] = 0u;

  std::int16_t x = 1, y = 1, z = 1;
  expect("read succeeds after revive", drv.read_accel(&x, &y, &z));
  expect("acc_en is back on", (dev.regs[kRegPowerCtrl] & 0x04u) != 0u);
  expect("APS dropped for the retry (fifo_self_wakeup kept)",
         dev.regs[kRegPowerConf] == 0x02u);
  expect("gravity is visible after revive", z == 1024);
  expect("PWR_CTRL snapshot is live", drv.last_power_ctrl() == 0x04u);
}

}  // namespace

int main() {
  test_step_counter_bit_lands_in_the_real_block();
  test_failed_enable_is_not_reported_as_success();
  test_axes_are_reported_in_the_watch_frame();
  test_accel_scaling_is_1024_per_g();
  test_config_upload_is_spread_across_the_stream();
  test_acc_conf_matches_infinitime();
  test_dead_accel_is_revived();
  if (g_fails != 0) {
    std::printf("%d failure(s)\n", g_fails);
    return 1;
  }
  std::printf("all bma42x tests passed\n");
  return 0;
}
