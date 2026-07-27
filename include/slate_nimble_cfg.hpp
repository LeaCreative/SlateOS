#pragma once

// NimBLE mbuf / MSYS pool sizing for Slate on nRF52832.
// Normative RAM budget: roadmap §3.2 — "NimBLE mbuf pool + host state | 6 KB".
//
// ── Requirements ────────────────────────────────────────────────────────────
// Single central connection, ATT MTU 247 (ATT payload 244), DLE up to 251 octets.
//
// One ACL/ATT PDU of max size needs a contiguous mbuf block of at least:
//   251 (LL data) + mbuf header overhead ≈ 32–40 B  →  round to 292 B block
// (NimBLE default MSYS_1_BLOCK_SIZE often 292.)
//
// Concurrent buffers needed for a responsive peripheral notify path:
//   RX path:  2  (one being parsed, one receiving)
//   TX path:  3  (fragmented notify in flight + retry slack)
//   HS misc:  2  (GATT prep / temporary)
//   ────────────────
//   Total:    7–8 blocks
//
// ── Chosen config ───────────────────────────────────────────────────────────
//   MSYS_1_BLOCK_SIZE  = 292
//   MSYS_1_BLOCK_COUNT = 8
//
// Pool bytes ≈ COUNT × (SIZE + os_mbuf overhead)
//            ≈ 8 × (292 + 40) = 2656 B   ← mbuf pool alone
//
// Host static state (conn, L2CAP, GATTS tables, att cache) on a trimmed NimBLE
// build with one connection is typically 3.0–4.5 KB depending on feature flags.
// Combined: ~5.7–7.2 KB.
//
// ── Verdict on the 6 KB target ──────────────────────────────────────────────
// The *mbuf pool* itself is well under 6 KB (~2.6 KB).
// "mbuf pool + host state" at MTU 247 + DLE with one connection is *tight*:
//   • Feasible at ~6 KB if GATTS service count is small, bonding tables minimal,
//     and HS logging disabled.
//   • Realistic measured floor on InfiniTime-class NimBLE builds is often
//     **7–8 KB** for host+mbuf once DIS/BAS/HRS and bonding are enabled.
// If the 6 KB envelope is a hard CI gate, drop MSYS_1_BLOCK_COUNT to 6
// (saves ~664 B) and accept more TX back-pressure under bursty notifies —
// do **not** shrink BLOCK_SIZE below 292 while advertising MTU 247 + DLE.
//
// Without DLE (27-byte LL PDUs) throughput caps near ~54 kB/s; mbuf count can
// shrink, but Slate requires DLE for navigation/camera payloads.

#define SLATE_MSYS_1_BLOCK_SIZE   292
#define SLATE_MSYS_1_BLOCK_COUNT  8
#define SLATE_MSYS_POOL_BYTES_EST (SLATE_MSYS_1_BLOCK_COUNT * (SLATE_MSYS_1_BLOCK_SIZE + 40))

// Max simultaneous connections.
#define SLATE_BLE_MAX_CONNECTIONS 1

// Prefer ATT MTU 247.
#define SLATE_BLE_ATT_PREFERRED_MTU 247
