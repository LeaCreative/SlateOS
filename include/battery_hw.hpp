#pragma once

namespace slate {
namespace battery_hw {

void init();
void set_adc_raw(int raw);

/** Last raw SAADC count, or -1 before the first sample (diag / calibration). */
int last_adc_raw();

/** SAADC sample + publish to battery::hooks (≤ ~10 ms busy-wait). */
void sample_now();

}  // namespace battery_hw
}  // namespace slate
