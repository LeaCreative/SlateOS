#pragma once

#include <cstddef>
#include <cstdint>

// RAM budgets from roadmap §3.2 — CI / static_assert targets for M10.
namespace slate {
namespace budget {

constexpr std::size_t kLocalScreenStateBytes = 3072u;  // 3 KB
constexpr std::size_t kNotifStoreBytes = 4096u;        // 4 KB
constexpr std::size_t kSettingsStateBytes = 2048u;     // 2 KB (persist mirror)

}  // namespace budget
}  // namespace slate
