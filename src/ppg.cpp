#include "ppg.hpp"

#include <cstdint>
#include <string.h>

#define sqrt_internal sqrtf
#define FFT_SPEED_OVER_PRECISION
#include "arduinoFFT.h"

namespace slate {
namespace hr {
namespace {

float linear_interpolation(const float* x_values, const float* y_values,
                           int length, float point_x) {
  if (point_x > x_values[length - 1]) {
    return y_values[length - 1];
  }
  if (point_x <= x_values[0]) {
    return y_values[0];
  }
  int index = 0;
  while (point_x > x_values[index] && index < length - 1) {
    ++index;
  }
  const float point_x0 = x_values[index - 1];
  const float point_x1 = x_values[index];
  const float point_y0 = y_values[index - 1];
  const float point_y1 = y_values[index];
  const float mu = (point_x - point_x0) / (point_x1 - point_x0);
  return point_y0 * (1.0f - mu) + point_y1 * mu;
}

float peak_search(float* x_vals, float* y_vals, float threshold, float& width,
                  float start, float end, int length) {
  int peaks = 0;
  bool enabled = false;
  float min_bin = 0.0f;
  float max_bin = 0.0f;
  float peak_center = 0.0f;
  float prev_value = linear_interpolation(x_vals, y_vals, length, start - 0.01f);
  float curr_value = linear_interpolation(x_vals, y_vals, length, start);
  float idx = start;
  while (idx < end) {
    const float next_value =
        linear_interpolation(x_vals, y_vals, length, idx + 0.01f);
    if (curr_value < threshold) {
      enabled = true;
    }
    if (curr_value >= threshold && enabled) {
      if (prev_value < threshold) {
        min_bin = idx;
      } else if (next_value <= threshold) {
        max_bin = idx;
        ++peaks;
        width = max_bin - min_bin;
        peak_center = width / 2.0f + min_bin;
      }
    }
    prev_value = curr_value;
    curr_value = next_value;
    idx += 0.01f;
  }
  if (peaks != 1) {
    width = 0.0f;
    peak_center = 0.0f;
  }
  return peak_center;
}

float spectrum_mean(const std::array<float, Ppg::kSpectrumLength>& signal,
                    int start, int end) {
  int total = 0;
  float mean = 0.0f;
  for (int idx = start; idx < end; ++idx) {
    mean += signal.at(static_cast<std::size_t>(idx));
    ++total;
  }
  if (total > 0) {
    mean /= static_cast<float>(total);
  }
  return mean;
}

float signal_to_noise(const std::array<float, Ppg::kSpectrumLength>& signal,
                      int start, int end, float max_v) {
  const float mean = spectrum_mean(signal, start, end);
  return max_v / mean;
}

void filter_30_to_240(std::array<float, Ppg::kDataLength>& signal) {
  const int length = static_cast<int>(signal.size());
  float exp_alpha = 0.816f;
  float exp_avg = 0.0f;
  for (int loop = 0; loop < 4; ++loop) {
    exp_avg = signal.front();
    for (int idx = 0; idx < length; ++idx) {
      exp_avg = (exp_alpha * signal.at(static_cast<std::size_t>(idx))) +
                ((1.0f - exp_alpha) * exp_avg);
      signal[static_cast<std::size_t>(idx)] = exp_avg;
    }
  }
  exp_alpha = 0.268f;
  for (int loop = 0; loop < 4; ++loop) {
    exp_avg = signal.front();
    for (int idx = 0; idx < length; ++idx) {
      exp_avg = (exp_alpha * signal.at(static_cast<std::size_t>(idx))) +
                ((1.0f - exp_alpha) * exp_avg);
      signal[static_cast<std::size_t>(idx)] -= exp_avg;
    }
  }
}

float spectrum_max(const std::array<float, Ppg::kSpectrumLength>& data,
                   int start, int end) {
  float max_v = 0.0f;
  for (int idx = start; idx < end; ++idx) {
    if (data.at(static_cast<std::size_t>(idx)) > max_v) {
      max_v = data.at(static_cast<std::size_t>(idx));
    }
  }
  return max_v;
}

void detrend(std::array<float, Ppg::kDataLength>& signal) {
  const int size = static_cast<int>(signal.size());
  const float offset = signal.front();
  const float slope =
      (signal.at(static_cast<std::size_t>(size - 1)) - offset) /
      static_cast<float>(size - 1);
  for (int idx = 0; idx < size; ++idx) {
    signal[static_cast<std::size_t>(idx)] -=
        (slope * static_cast<float>(idx) + offset);
  }
  for (int idx = 0; idx < size - 1; ++idx) {
    signal[static_cast<std::size_t>(idx)] =
        signal[static_cast<std::size_t>(idx + 1)] -
        signal[static_cast<std::size_t>(idx)];
  }
}

// numpy.hanning(64) first half — InfiniTime hardcoded table.
static constexpr float kHanning[Ppg::kDataLength >> 1] = {
    0.0f,        0.00248461f, 0.00991376f, 0.0222136f,  0.03926189f,
    0.06088921f, 0.08688061f, 0.11697778f, 0.15088159f, 0.1882551f,
    0.22872687f, 0.27189467f, 0.31732949f, 0.36457977f, 0.41317591f,
    0.46263495f, 0.51246535f, 0.56217185f, 0.61126047f, 0.65924333f,
    0.70564355f, 0.75f,       0.79187184f, 0.83084292f, 0.86652594f,
    0.89856625f, 0.92664544f, 0.95048443f, 0.96984631f, 0.98453864f,
    0.99441541f, 0.99937846f,
};

}  // namespace

Ppg::Ppg() {
  data_average_.fill(0.0f);
  spectrum_.fill(0.0f);
}

std::int8_t Ppg::preprocess(std::uint16_t hrs, std::uint16_t als) {
  if (data_index_ < kDataLength) {
    data_hrs_[data_index_++] = hrs;
  }
  als_value_ = als;
  if (als_value_ > als_threshold_) {
    return 1;
  }
  return 0;
}

int Ppg::heart_rate() {
  if (data_index_ < kDataLength) {
    if (!enough_data_) {
      return -2;
    }
    return 0;
  }
  enough_data_ = true;
  const int hr = process_heart_rate(reset_spectral_avg_);
  reset_spectral_avg_ = false;
  for (int idx = 0; idx < static_cast<int>(kDataLength - kOverlapWindow);
       ++idx) {
    data_hrs_[static_cast<std::size_t>(idx)] =
        data_hrs_[static_cast<std::size_t>(idx + kOverlapWindow)];
  }
  data_index_ = static_cast<std::uint16_t>(kDataLength - kOverlapWindow);
  return hr;
}

void Ppg::reset(bool reset_daq_buffer) {
  if (reset_daq_buffer) {
    data_index_ = 0u;
    enough_data_ = false;
  }
  avg_index_ = 0u;
  data_average_.fill(0.0f);
  last_peak_location_ = 0.0f;
  als_threshold_ = 0xFFFFu;
  als_value_ = 0u;
  reset_spectral_avg_ = true;
  spectrum_.fill(0.0f);
}

int Ppg::process_heart_rate(bool init) {
  for (std::size_t i = 0u; i < kDataLength; ++i) {
    v_real_[i] = static_cast<float>(data_hrs_[i]);
  }
  detrend(v_real_);
  filter_30_to_240(v_real_);
  v_imag_.fill(0.0f);
  int hann_idx = 0;
  for (int idx = 0; idx < static_cast<int>(kDataLength); ++idx) {
    if (idx >= static_cast<int>(kDataLength >> 1)) {
      --hann_idx;
    }
    v_real_[static_cast<std::size_t>(idx)] *= kHanning[hann_idx];
    if (idx < static_cast<int>(kDataLength >> 1)) {
      ++hann_idx;
    }
  }
  {
    ArduinoFFT<float> fft(v_real_.data(), v_imag_.data(), kDataLength,
                          kSampleFreq);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();
  }
  spectrum_average(v_real_.data(), spectrum_.data(),
                   static_cast<int>(spectrum_.size()), init);
  peak_location_ = 0.0f;
  float threshold = kPeakDetectionThreshold;
  float peak_width = 0.0f;
  const int spec_len = static_cast<int>(spectrum_.size());
  const float max_v =
      spectrum_max(spectrum_, static_cast<int>(kHrRoiBegin),
                   static_cast<int>(kHrRoiEnd));
  const float snr = signal_to_noise(spectrum_, static_cast<int>(kHrRoiBegin),
                                    static_cast<int>(kHrRoiEnd), max_v);
  if (snr > kSignalToNoiseThreshold && spectrum_.at(0) < kDcThreshold) {
    threshold *= max_v;
    for (int idx = 0; idx < static_cast<int>(kDataLength); ++idx) {
      v_imag_[static_cast<std::size_t>(idx)] = static_cast<float>(idx);
    }
    peak_location_ = peak_search(
        v_imag_.data(), spectrum_.data(), threshold, peak_width,
        static_cast<float>(kHrRoiBegin), static_cast<float>(kHrRoiEnd),
        spec_len);
    peak_location_ *= kFreqResolution;
  }
  if (peak_width > kMaxPeakWidth) {
    peak_location_ = 0.0f;
  }
  if (peak_location_ < kMinHr || peak_location_ > kMaxHr) {
    peak_location_ = 0.0f;
  }
  if (peak_location_ == 0.0f) {
    reset_spectral_avg_ = true;
  }
  als_threshold_ = static_cast<std::uint16_t>(als_value_ * kAlsFactor);
  peak_location_ = heart_rate_average(peak_location_);
  int rtn = -1;
  if (peak_location_ == 0.0f && last_peak_location_ > 0.0f) {
    last_peak_location_ = 0.0f;
  } else {
    last_peak_location_ = peak_location_;
    rtn = static_cast<int>((peak_location_ * 60.0f) + 0.5f);
  }
  return rtn;
}

void Ppg::spectrum_average(const float* data, float* spectrum, int length,
                           bool reset) {
  if (reset) {
    spectral_avg_count_ = 0u;
  }
  const float count = static_cast<float>(spectral_avg_count_);
  for (int idx = 0; idx < length; ++idx) {
    spectrum[idx] = (spectrum[idx] * count + data[idx]) / (count + 1.0f);
  }
  if (spectral_avg_count_ < kSpectralAvgMax) {
    ++spectral_avg_count_;
  }
}

float Ppg::heart_rate_average(float hr) {
  ++avg_index_;
  avg_index_ = static_cast<std::uint16_t>(avg_index_ % data_average_.size());
  data_average_[avg_index_] = hr;
  float avg = 0.0f;
  float total = 0.0f;
  for (const float& value : data_average_) {
    if (value > 0.0f) {
      avg += value;
      total += 1.0f;
    }
  }
  if (total > 0.0f) {
    avg /= total;
  } else {
    avg = 0.0f;
  }
  return avg;
}

}  // namespace hr
}  // namespace slate
