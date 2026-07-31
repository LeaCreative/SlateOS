#include "spi_bus.hpp"

#include "board.hpp"
#include "nrf52832_regs.hpp"
#include "rtt.hpp"

#include <cstddef>
#include <cstdint>

#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif

// PineTime SPI bus pins (authoritative — see CLAUDE.md / hardware table)
static constexpr std::uint32_t kPinSck = 2u;
static constexpr std::uint32_t kPinMosi = 3u;
static constexpr std::uint32_t kPinMiso = 4u;
static constexpr std::uint32_t kPinDc = 18u;  // ST7789 DC/RS

namespace {

bool g_acquired = false;
std::uint32_t g_cs_asserted = 0u;  // 0 = none; else pin number
spi::Stats g_stats{};

#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
SemaphoreHandle_t g_mtx = nullptr;
#endif

void gpio_output(std::uint32_t pin, bool initial_high) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT | nrf::gpio::PIN_CNF_INPUT_DISCONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED | nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;

  if (initial_high) {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
  } else {
    nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
  }
}

void configure_pins() {
  gpio_output(kPinSck, false);
  gpio_output(kPinMosi, false);
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(kPinMiso)) =
      nrf::gpio::PIN_CNF_DIR_INPUT | nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULL_DISABLED | nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
  gpio_output(kPinDc, true);
  gpio_output(spi::kPinCsDisplay, true);
  gpio_output(spi::kPinCsFlash, true);
}

void wire_spim() {
  nrf::reg<std::uint32_t>(nrf::spim0::PSEL_SCK) = kPinSck;
  nrf::reg<std::uint32_t>(nrf::spim0::PSEL_MOSI) = kPinMosi;
  nrf::reg<std::uint32_t>(nrf::spim0::PSEL_MISO) = kPinMiso;
  nrf::reg<std::uint32_t>(nrf::spim0::FREQUENCY) = nrf::spim0::FREQ_8M;
  nrf::reg<std::uint32_t>(nrf::spim0::CONFIG) = nrf::spim0::CONFIG_MODE3_MSB;
  nrf::reg<std::uint32_t>(nrf::spim0::ORC) = 0xFFu;
}

// Always-on (Release keeps this — NDEBUG must not silence CS/bus violations).
void protocol_fault(const char* msg) {
  ++g_stats.protocol_faults;
  rtt::log(rtt::Level::Error, msg);
}

void deassert_all_cs() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) =
      (1u << spi::kPinCsDisplay) | (1u << spi::kPinCsFlash);
  g_cs_asserted = 0u;
}

void recover_spim() {
  nrf::reg<std::uint32_t>(nrf::spim0::TASKS_STOP) = 1u;
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::DISABLE;
  deassert_all_cs();
  configure_pins();
  wire_spim();
  nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::ENABLE_SPIM;
  ++g_stats.end_timeouts;
  rtt::log(rtt::Level::Error, "spi: EVENTS_END timeout — SPIM recovered");
}

bool wait_end(std::uint32_t timeout_us) {
  const std::uint32_t start = board::micros();
  while (nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) == 0u) {
    if (static_cast<std::int32_t>(board::micros() - start) >
        static_cast<std::int32_t>(timeout_us)) {
      recover_spim();
      return false;
    }
  }
  nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;
  return true;
}

}  // namespace

namespace spi {

void init() {
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::DISABLE;
  configure_pins();
  wire_spim();
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::ENABLE_SPIM;
  g_acquired = false;
  g_cs_asserted = 0u;
  g_stats = {};
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  if (g_mtx == nullptr) {
    g_mtx = xSemaphoreCreateMutex();
  }
#endif
}

void acquire() {
  if (g_acquired) {
    protocol_fault("spi: nested acquire");
  }
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  if (g_mtx != nullptr && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    (void)xSemaphoreTake(g_mtx, portMAX_DELAY);
  }
#endif
  nrf::reg<std::uint32_t>(nrf::spim0::ENABLE) = nrf::spim0::ENABLE_SPIM;
  g_acquired = true;
}

void release() {
  if (!g_acquired) {
    protocol_fault("spi: release without acquire");
  }
  deassert_all_cs();
  g_acquired = false;
#if defined(SLATE_HAS_FREERTOS) && (SLATE_HAS_FREERTOS == 1)
  if (g_mtx != nullptr && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    (void)xSemaphoreGive(g_mtx);
  }
#endif
}

bool is_acquired() { return g_acquired; }

Stats stats() { return g_stats; }

void reset_stats() { g_stats = {}; }

bool transmit(const std::uint8_t* data, std::size_t len) {
  if (!g_acquired) {
    protocol_fault("spi: transmit without acquire");
    return false;
  }
  if (data == nullptr && len > 0u) {
    return false;
  }

  while (len > 0u) {
    const std::size_t chunk = (len > 255u) ? 255u : len;
    nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;
    nrf::reg<std::uint32_t>(nrf::spim0::TXD_PTR) =
        reinterpret_cast<std::uint32_t>(data);
    nrf::reg<std::uint32_t>(nrf::spim0::TXD_MAXCNT) =
        static_cast<std::uint32_t>(chunk);
    nrf::reg<std::uint32_t>(nrf::spim0::RXD_MAXCNT) = 0u;
    nrf::reg<std::uint32_t>(nrf::spim0::TASKS_START) = 1u;
    if (!wait_end(end_wait_budget_us(chunk))) {
      return false;
    }
    data += chunk;
    len -= chunk;
  }
  return true;
}

bool transfer(const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
              std::size_t rx_len) {
  if (!g_acquired) {
    protocol_fault("spi: transfer without acquire");
    return false;
  }
  if (tx != nullptr && tx_len > 0u) {
    if (!transmit(tx, tx_len)) {
      return false;
    }
  }
  if (rx == nullptr || rx_len == 0u) {
    return true;
  }
  alignas(4) static std::uint8_t zeros[255] = {};
  while (rx_len > 0u) {
    const std::size_t chunk = (rx_len > sizeof(zeros)) ? sizeof(zeros) : rx_len;
    nrf::reg<std::uint32_t>(nrf::spim0::EVENTS_END) = 0u;
    nrf::reg<std::uint32_t>(nrf::spim0::TXD_PTR) =
        reinterpret_cast<std::uint32_t>(zeros);
    nrf::reg<std::uint32_t>(nrf::spim0::TXD_MAXCNT) =
        static_cast<std::uint32_t>(chunk);
    nrf::reg<std::uint32_t>(nrf::spim0::RXD_PTR) =
        reinterpret_cast<std::uint32_t>(rx);
    nrf::reg<std::uint32_t>(nrf::spim0::RXD_MAXCNT) =
        static_cast<std::uint32_t>(chunk);
    nrf::reg<std::uint32_t>(nrf::spim0::TASKS_START) = 1u;
    if (!wait_end(end_wait_budget_us(chunk))) {
      return false;
    }
    rx += chunk;
    rx_len -= chunk;
  }
  return true;
}

void cs_assert(std::uint32_t pin) {
  if (pin != kPinCsDisplay && pin != kPinCsFlash) {
    protocol_fault("spi: cs_assert unknown pin");
    return;
  }
  if (g_cs_asserted != 0u && g_cs_asserted != pin) {
    protocol_fault("spi: dual CS assert on shared bus");
    deassert_all_cs();
  }
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
  g_cs_asserted = pin;
}

void cs_deassert(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
  if (g_cs_asserted == pin) {
    g_cs_asserted = 0u;
  }
}

void dc_command() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << kPinDc);
}

void dc_data() {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << kPinDc);
}

}  // namespace spi
