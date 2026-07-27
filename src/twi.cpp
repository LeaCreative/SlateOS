#include "twi.hpp"
#include "board.hpp"
#include "nrf52832_regs.hpp"

#include <cstddef>
#include <cstdint>

static constexpr std::uint32_t kPinScl = 7u;
static constexpr std::uint32_t kPinSda = 6u;

namespace {

void gpio_input_pullup(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_INPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP |
      nrf::gpio::PIN_CNF_DRIVE_S0S1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
}

void gpio_output_open_drain_pullup(std::uint32_t pin) {
  // S0D1 drive: pull low actively, release high (open-drain with pull-up).
  constexpr std::uint32_t kDriveS0D1 = (6u << 8);
  nrf::reg<std::uint32_t>(nrf::gpio::pin_cnf(pin)) =
      nrf::gpio::PIN_CNF_DIR_OUTPUT |
      nrf::gpio::PIN_CNF_INPUT_CONNECT |
      nrf::gpio::PIN_CNF_PULLUP |
      kDriveS0D1 |
      nrf::gpio::PIN_CNF_SENSE_DISABLED;
}

bool pin_is_high(std::uint32_t pin) {
  return (nrf::reg<std::uint32_t>(nrf::gpio::IN) & (1u << pin)) != 0u;
}

void pin_set(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTSET) = (1u << pin);
}

void pin_clear(std::uint32_t pin) {
  nrf::reg<std::uint32_t>(nrf::gpio::OUTCLR) = (1u << pin);
}

void twim_disable() {
  nrf::reg<std::uint32_t>(nrf::twim1::ENABLE) = nrf::twim1::DISABLE;
}

void twim_enable() {
  nrf::reg<std::uint32_t>(nrf::twim1::ENABLE) = nrf::twim1::ENABLE_TWIM;
}

void clear_events() {
  nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_STOPPED) = 0u;
  nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_ERROR) = 0u;
  nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_LASTTX) = 0u;
  nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_LASTRX) = 0u;
  nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_SUSPENDED) = 0u;
}

twi::Status map_error() {
  const std::uint32_t src = nrf::reg<std::uint32_t>(nrf::twim1::ERRORSRC);
  nrf::reg<std::uint32_t>(nrf::twim1::ERRORSRC) = src;  // W1C
  if (src & nrf::twim1::ERRORSRC_ANACK) {
    return twi::Status::AddressNack;
  }
  if (src & nrf::twim1::ERRORSRC_DNACK) {
    return twi::Status::DataNack;
  }
  return twi::Status::BusError;
}

// Wait until `event` is set, or timeout / ERROR.  Returns false on failure.
bool wait_event(std::uintptr_t event_addr, std::uint32_t timeout_us,
                twi::Status* out_status) {
  const std::uint32_t start = board::micros();
  while (true) {
    if (nrf::reg<std::uint32_t>(event_addr) != 0u) {
      nrf::reg<std::uint32_t>(event_addr) = 0u;
      return true;
    }
    if (nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_ERROR) != 0u) {
      nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_ERROR) = 0u;
      *out_status = map_error();
      // Force STOP after error so the peripheral leaves the bus.
      nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_STOPPED) = 0u;
      nrf::reg<std::uint32_t>(nrf::twim1::TASKS_STOP) = 1u;
      const std::uint32_t stop_start = board::micros();
      while (nrf::reg<std::uint32_t>(nrf::twim1::EVENTS_STOPPED) == 0u) {
        if (static_cast<std::int32_t>(board::micros() - stop_start) >
            static_cast<std::int32_t>(1000)) {
          break;
        }
      }
      return false;
    }
    if (static_cast<std::int32_t>(board::micros() - start) >
        static_cast<std::int32_t>(timeout_us)) {
      *out_status = twi::Status::Timeout;
      nrf::reg<std::uint32_t>(nrf::twim1::TASKS_STOP) = 1u;
      return false;
    }
  }
}

}  // namespace

namespace twi {

void recover_bus() {
  twim_disable();

  // Bit-bang open-drain SCL/SDA to free a stuck slave.
  gpio_output_open_drain_pullup(kPinScl);
  gpio_output_open_drain_pullup(kPinSda);

  pin_set(kPinScl);
  pin_set(kPinSda);
  board::busy_wait_us(5u);

  // Clock out up to 9 bits until SDA releases.
  for (std::uint32_t i = 0u; i < 9u; ++i) {
    if (pin_is_high(kPinSda)) {
      break;
    }
    pin_clear(kPinScl);
    board::busy_wait_us(5u);
    pin_set(kPinScl);
    board::busy_wait_us(5u);
  }

  // Generate STOP: SDA rising while SCL is high.
  pin_clear(kPinSda);
  board::busy_wait_us(5u);
  pin_set(kPinScl);
  board::busy_wait_us(5u);
  pin_set(kPinSda);
  board::busy_wait_us(5u);

  // Hand pins back to TWIM1.
  gpio_input_pullup(kPinScl);
  gpio_input_pullup(kPinSda);

  nrf::reg<std::uint32_t>(nrf::twim1::PSEL_SCL) = kPinScl;
  nrf::reg<std::uint32_t>(nrf::twim1::PSEL_SDA) = kPinSda;
  twim_enable();
}

void init() {
  twim_disable();

  gpio_input_pullup(kPinScl);
  gpio_input_pullup(kPinSda);

  nrf::reg<std::uint32_t>(nrf::twim1::PSEL_SCL) = kPinScl;
  nrf::reg<std::uint32_t>(nrf::twim1::PSEL_SDA) = kPinSda;
  nrf::reg<std::uint32_t>(nrf::twim1::FREQUENCY) = nrf::twim1::FREQ_400K;
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = 0u;

  clear_events();
  twim_enable();
}

Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len,
             std::uint32_t timeout_us) {
  if (len == 0u) {
    return Status::Ok;
  }

  clear_events();
  nrf::reg<std::uint32_t>(nrf::twim1::ADDRESS) = addr;
  nrf::reg<std::uint32_t>(nrf::twim1::TXD_PTR) =
      reinterpret_cast<std::uint32_t>(data);
  nrf::reg<std::uint32_t>(nrf::twim1::TXD_MAXCNT) = static_cast<std::uint32_t>(len);
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = nrf::twim1::SHORT_LASTTX_STOP;

  nrf::reg<std::uint32_t>(nrf::twim1::TASKS_STARTTX) = 1u;

  Status st = Status::Ok;
  if (!wait_event(nrf::twim1::EVENTS_STOPPED, timeout_us, &st)) {
    recover_bus();
    return st;
  }
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = 0u;
  return Status::Ok;
}

Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len,
            std::uint32_t timeout_us) {
  if (len == 0u) {
    return Status::Ok;
  }

  clear_events();
  nrf::reg<std::uint32_t>(nrf::twim1::ADDRESS) = addr;
  nrf::reg<std::uint32_t>(nrf::twim1::RXD_PTR) =
      reinterpret_cast<std::uint32_t>(data);
  nrf::reg<std::uint32_t>(nrf::twim1::RXD_MAXCNT) = static_cast<std::uint32_t>(len);
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = nrf::twim1::SHORT_LASTRX_STOP;

  nrf::reg<std::uint32_t>(nrf::twim1::TASKS_STARTRX) = 1u;

  Status st = Status::Ok;
  if (!wait_event(nrf::twim1::EVENTS_STOPPED, timeout_us, &st)) {
    recover_bus();
    return st;
  }
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = 0u;
  return Status::Ok;
}

Status write_read(std::uint8_t addr,
                  const std::uint8_t* tx, std::size_t tx_len,
                  std::uint8_t* rx, std::size_t rx_len,
                  std::uint32_t timeout_us) {
  if (tx_len == 0u) {
    return read(addr, rx, rx_len, timeout_us);
  }
  if (rx_len == 0u) {
    return write(addr, tx, tx_len, timeout_us);
  }

  clear_events();
  nrf::reg<std::uint32_t>(nrf::twim1::ADDRESS) = addr;
  nrf::reg<std::uint32_t>(nrf::twim1::TXD_PTR) =
      reinterpret_cast<std::uint32_t>(tx);
  nrf::reg<std::uint32_t>(nrf::twim1::TXD_MAXCNT) = static_cast<std::uint32_t>(tx_len);
  nrf::reg<std::uint32_t>(nrf::twim1::RXD_PTR) =
      reinterpret_cast<std::uint32_t>(rx);
  nrf::reg<std::uint32_t>(nrf::twim1::RXD_MAXCNT) = static_cast<std::uint32_t>(rx_len);

  // Repeated start: LASTTX → STARTRX, then LASTRX → STOP.
  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) =
      nrf::twim1::SHORT_LASTTX_STARTRX | nrf::twim1::SHORT_LASTRX_STOP;

  nrf::reg<std::uint32_t>(nrf::twim1::TASKS_STARTTX) = 1u;

  Status st = Status::Ok;
  if (!wait_event(nrf::twim1::EVENTS_STOPPED, timeout_us, &st)) {
    recover_bus();
    return st;
  }

  nrf::reg<std::uint32_t>(nrf::twim1::SHORTS) = 0u;
  return Status::Ok;
}

}  // namespace twi
