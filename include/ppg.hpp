#pragma once

#include <array>
#include <cstdint>

// InfiniTime Controllers::Ppg — FFT heart-rate from HRS3300 samples.

namespace slate {
namespace hr {

class Ppg {
public:
  Ppg();
  /** Returns 1 when ambient light suggests off-wrist. */
  std::int8_t preprocess(std::uint16_t hrs, std::uint16_t als);
  /**
   * BPM, or -2 (not enough data), 0 (no new value), -1 (reset / lost lock).
   */
  int heart_rate();
  void reset(bool reset_daq_buffer);

  static constexpr int kDeltaTms = 100;
  static constexpr std::uint16_t kDataLength = 64u;
  static constexpr std::uint16_t kSpectrumLength = kDataLength >> 1;

private:
  static constexpr float kSampleFreq = 1000.0f / static_cast<float>(kDeltaTms);
  static constexpr float kFreqResolution = kSampleFreq / kDataLength;
  static constexpr std::uint16_t kOverlapWindow = 5u;
  static constexpr std::uint16_t kSpectralAvgMax = 2u;
  static constexpr float kPeakDetectionThreshold = 0.6f;
  static constexpr float kMaxPeakWidth = 2.5f;
  static constexpr float kSignalToNoiseThreshold = 3.0f;
  static constexpr std::uint16_t kHrRoiBegin =
      static_cast<std::uint16_t>((30.0f / 60.0f) / kFreqResolution + 0.5f);
  static constexpr std::uint16_t kHrRoiEnd =
      static_cast<std::uint16_t>((240.0f / 60.0f) / kFreqResolution + 0.5f);
  static constexpr float kMinHr = 40.0f / 60.0f;
  static constexpr float kMaxHr = 230.0f / 60.0f;
  static constexpr float kDcThreshold = 0.5f;
  static constexpr float kAlsFactor = 2.0f;

  int process_heart_rate(bool init);
  float heart_rate_average(float hr);
  void spectrum_average(const float* data, float* spectrum, int length,
                        bool reset);

  std::array<std::uint16_t, kDataLength> data_hrs_{};
  std::array<float, kDataLength> v_real_{};
  std::array<float, kDataLength> v_imag_{};
  std::array<float, kSpectrumLength> spectrum_{};
  std::array<float, 20> data_average_{};

  std::uint16_t avg_index_ = 0u;
  std::uint16_t spectral_avg_count_ = 0u;
  float last_peak_location_ = 0.0f;
  std::uint16_t als_threshold_ = 0xFFFFu;
  std::uint16_t als_value_ = 0u;
  std::uint16_t data_index_ = 0u;
  float peak_location_ = 0.0f;
  bool reset_spectral_avg_ = true;
  bool enough_data_ = false;
};

}  // namespace hr
}  // namespace slate
