#pragma once

#include <cstddef>
#include <cstdint>

// RAM budgets from roadmap §3.2 — CI / static_assert targets for M10.
namespace slate {
namespace budget {

// Local UI state. Was 3072 with a ~2.9 KB empty reserve (State is ~152 B);
// that pad was pure RAM cost against a ~200 B link margin (I-19). 256 B leaves
// headroom for a few more fields without inviting another multi-KiB hole.
constexpr std::size_t kLocalScreenStateBytes = 272u;
constexpr std::size_t kNotifStoreBytes = 4096u;        // 4 KB
constexpr std::size_t kSettingsStateBytes = 2048u;     // 2 KB (persist mirror)

}  // namespace budget
}  // namespace slate
