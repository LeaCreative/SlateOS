#include "hr_task.hpp"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

namespace slate {
namespace hr {
namespace {

constexpr TickType_t kSampleTicks =
    pdMS_TO_TICKS(static_cast<TickType_t>(Ppg::kDeltaTms));
constexpr std::uint32_t kGiveUpMs = 30000u;

std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(xTaskGetTickCount() *
                                    portTICK_PERIOD_MS);
}

}  // namespace

Task::Task(hrs::Driver& sensor, Controller& controller)
    : sensor_(sensor), controller_(controller) {}

void Task::start() {
  queue_ = xQueueCreate(8, sizeof(Msg));
  controller_.set_task(this);
  if (xTaskCreate(process, "hr", 500, this, tskIDLE_PRIORITY + 1,
                  reinterpret_cast<TaskHandle_t*>(&task_handle_)) != pdPASS) {
    task_handle_ = nullptr;
  }
}

void Task::process(void* instance) {
  static_cast<Task*>(instance)->work();
}

void Task::push(Msg msg) {
  if (queue_ == nullptr) {
    return;
  }
  (void)xQueueSend(static_cast<QueueHandle_t>(queue_), &msg, 0);
}

void Task::push_from_isr(Msg msg) {
  if (queue_ == nullptr) {
    return;
  }
  BaseType_t woken = pdFALSE;
  (void)xQueueSendFromISR(static_cast<QueueHandle_t>(queue_), &msg, &woken);
  portYIELD_FROM_ISR(woken);
}

void Task::work() {
  while (true) {
    Msg msg = Msg::Disable;
    const TickType_t delay =
        (state_ == States::Measuring) ? kSampleTicks : portMAX_DELAY;
    const BaseType_t got =
        xQueueReceive(static_cast<QueueHandle_t>(queue_), &msg, delay);

    States next = state_;
    if (got == pdTRUE) {
      switch (msg) {
        case Msg::Enable:
          next = States::Measuring;
          value_shown_ = false;
          break;
        case Msg::Disable:
          next = States::Disabled;
          break;
        case Msg::GoToSleep:
          if (state_ == States::Measuring && local_session_) {
            next = States::Disabled;
          }
          break;
        case Msg::WakeUp:
          break;
      }
    }

    if (next == States::Measuring && state_ == States::Disabled) {
      start_measurement();
    } else if (next == States::Disabled && state_ == States::Measuring) {
      stop_measurement();
    }
    state_ = next;

    if (state_ == States::Measuring) {
      handle_sensor();
      ++sample_count_;
    }
  }
}

void Task::start_measurement() {
  sensor_.enable();
  ppg_.reset(true);
  vTaskDelay(pdMS_TO_TICKS(100));
  measurement_ok_ = false;
  sample_count_ = 0u;
  measure_start_ms_ = now_ms();
}

void Task::stop_measurement() {
  sensor_.disable();
  ppg_.reset(true);
  vTaskDelay(pdMS_TO_TICKS(100));
  controller_.update(State::Stopped, 0u);
}

void Task::handle_sensor() {
  const auto sample = sensor_.read_hrs_als();
  const std::int8_t ambient = ppg_.preprocess(sample.hrs, sample.als);
  int bpm = ppg_.heart_rate();

  if (ambient > 0) {
    ppg_.reset(true);
    controller_.update(State::NoTouch, 0u);
    bpm = 0;
    value_shown_ = false;
  }

  if (bpm == -1) {
    ppg_.reset(false);
    bpm = 0;
    controller_.update(State::Running, 0u);
    value_shown_ = false;
  } else if (bpm == -2) {
    bpm = 0;
    if (!value_shown_) {
      controller_.update(State::NotEnoughData, 0u);
    }
  }

  if (bpm != 0) {
    measurement_ok_ = true;
    value_shown_ = true;
    controller_.update(State::Running,
                       static_cast<std::uint8_t>(bpm > 255 ? 255 : bpm));
    return;
  }

  if (local_session_ &&
      (now_ms() - measure_start_ms_) > kGiveUpMs && !measurement_ok_) {
    // Soft give-up: stop LED when local session never locked BPM.
    stop_measurement();
    state_ = States::Disabled;
    controller_.update(State::Stopped, 0u);
  }
}

}  // namespace hr
}  // namespace slate
