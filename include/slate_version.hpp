#pragma once

// Firmware identity, shown on the watch face (diag builds) so a sealed watch
// can be identified without SWD, and reported to the companion in HELLO.
//
// kVersion is hand-maintained. The `-mN` suffix is also the MCUBoot image
// version (`imgtool --version 0.1.N`). `scripts/package_dfu.py` reads this
// string so cmake cannot keep a stale header version; bump N with every
// installable DFU. Equal `ih_ver` is not proven to block swaps after IMAGE_OK.

namespace slate {
namespace version {

constexpr const char kVersion[] = "0.1.0-m23";

// __DATE__ is "Mmm dd yyyy", __TIME__ is "hh:mm:ss".
constexpr const char kBuildDate[] = __DATE__;
constexpr const char kBuildTime[] = __TIME__;

}  // namespace version
}  // namespace slate
