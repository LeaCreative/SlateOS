#pragma once

#include <cstdint>

// M5a scheduler smoke: two tasks + one queue. Runs once after the scheduler
// starts, then self-tears down. Result is sticky for STATUS / RTT.

namespace freertos_smoke {

enum class Result : std::uint8_t {
  Pending = 0,
  Pass = 1,
  Fail = 2,
};

/** Create producer/consumer tasks + queue. Call before vTaskStartScheduler. */
void create_tasks();

Result result();

/** True once the smoke has finished (pass or fail). */
bool finished();

}  // namespace freertos_smoke
