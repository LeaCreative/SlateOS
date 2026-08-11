#pragma once

// Firmware identity, shown on the watch face (diag builds) so a sealed watch
// can be identified without SWD, and reported to the companion in HELLO.
//
// kVersion is hand-maintained; kBuildId comes from the compiler so that two
// builds of the same version are still distinguishable — which matters because
// images are flashed far more often than the version string changes.

namespace slate {
namespace version {

constexpr const char kVersion[] = "0.1.0-m17";

// __DATE__ is "Mmm dd yyyy", __TIME__ is "hh:mm:ss".
constexpr const char kBuildDate[] = __DATE__;
constexpr const char kBuildTime[] = __TIME__;

}  // namespace version
}  // namespace slate
