#pragma once

namespace slate {
namespace battery_hw {

void init();
void set_adc_raw(int raw);

/** SAADC sample + publish to battery::hooks (≤ ~10 ms busy-wait). */
void sample_now();

}  // namespace battery_hw
}  // namespace slate
