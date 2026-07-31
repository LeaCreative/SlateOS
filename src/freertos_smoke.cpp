#include "freertos_smoke.hpp"

#include "rtt.hpp"

#if defined(SLATE_HAS_NIMBLE) && (SLATE_HAS_NIMBLE == 1)

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

namespace freertos_smoke {
namespace {

constexpr UBaseType_t kSmokeCount = 8;
constexpr UBaseType_t kQueueLen = 4;

volatile Result g_result = Result::Pending;
QueueHandle_t g_q = nullptr;
TaskHandle_t g_producer = nullptr;
TaskHandle_t g_consumer = nullptr;

void producer_task(void*) {
  for (UBaseType_t i = 1; i <= kSmokeCount; ++i) {
    const std::uint32_t v = static_cast<std::uint32_t>(i);
    if (xQueueSend(g_q, &v, pdMS_TO_TICKS(50)) != pdPASS) {
      g_result = Result::Fail;
      rtt::log(rtt::Level::Error, "smoke: producer queue send failed");
      vTaskDelete(nullptr);
      return;
    }
    vTaskDelay(1);  // yield so consumer can run (preemption / tick)
  }
  vTaskDelete(nullptr);
}

void consumer_task(void*) {
  std::uint32_t expect = 1;
  for (UBaseType_t i = 0; i < kSmokeCount; ++i) {
    std::uint32_t got = 0;
    if (xQueueReceive(g_q, &got, pdMS_TO_TICKS(200)) != pdPASS) {
      g_result = Result::Fail;
      rtt::log(rtt::Level::Error, "smoke: consumer timeout");
      vTaskDelete(nullptr);
      return;
    }
    if (got != expect) {
      g_result = Result::Fail;
      rtt::log(rtt::Level::Error, "smoke: sequence mismatch");
      vTaskDelete(nullptr);
      return;
    }
    ++expect;
  }
  g_result = Result::Pass;
  rtt::log(rtt::Level::Info, "smoke: FreeRTOS 2-task+queue PASS");
  if (g_q != nullptr) {
    vQueueDelete(g_q);
    g_q = nullptr;
  }
  vTaskDelete(nullptr);
}

}  // namespace

void create_tasks() {
  g_result = Result::Pending;
  g_q = xQueueCreate(kQueueLen, sizeof(std::uint32_t));
  if (g_q == nullptr) {
    g_result = Result::Fail;
    rtt::log(rtt::Level::Error, "smoke: queue create failed");
    return;
  }
  // Consumer slightly higher priority so a full queue still drains.
  if (xTaskCreate(consumer_task, "smkC", configMINIMAL_STACK_SIZE + 64, nullptr,
                  tskIDLE_PRIORITY + 3, &g_consumer) != pdPASS) {
    g_result = Result::Fail;
    rtt::log(rtt::Level::Error, "smoke: consumer create failed");
    return;
  }
  if (xTaskCreate(producer_task, "smkP", configMINIMAL_STACK_SIZE + 64, nullptr,
                  tskIDLE_PRIORITY + 2, &g_producer) != pdPASS) {
    g_result = Result::Fail;
    rtt::log(rtt::Level::Error, "smoke: producer create failed");
    return;
  }
}

Result result() { return g_result; }

bool finished() { return g_result != Result::Pending; }

}  // namespace freertos_smoke

#else

namespace freertos_smoke {

void create_tasks() {}

Result result() { return Result::Pass; }

bool finished() { return true; }

}  // namespace freertos_smoke

#endif
