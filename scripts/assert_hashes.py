#!/usr/bin/env python3
"""Decode the FNV-1a file hash painted by boot_diag::assert_paint_and_hang_at.

A sealed watch has no SWD/RTT, so configASSERT paints FNV-1a(basename(__FILE__))
and the line number in hex. This maps a hash back to candidate source files.

Usage:
    python scripts/assert_hashes.py            # list every basename and hash
    python scripts/assert_hashes.py 1A2B3C4D   # resolve one hash from the watch
"""

import sys
from pathlib import Path

SOURCE_SUFFIXES = {".c", ".cpp", ".cc", ".h", ".hpp"}
ROOT = Path(__file__).resolve().parent.parent


def fnv1a32(text: str) -> int:
    h = 2166136261
    for byte in text.encode("utf-8"):
        h ^= byte
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def collect_basenames() -> dict[str, set[str]]:
    """Map basename -> set of repo-relative paths that share it."""
    names: dict[str, set[str]] = {}
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in SOURCE_SUFFIXES or not path.is_file():
            continue
        if "build" in path.parts:
            continue
        names.setdefault(path.name, set()).add(str(path.relative_to(ROOT)))
    return names


def main() -> int:
    names = collect_basenames()
    table = {name: fnv1a32(name) for name in names}

    if len(sys.argv) > 1:
        wanted = int(sys.argv[1].removeprefix("0x").removeprefix("0X"), 16)
        hits = [n for n, h in table.items() if h == wanted]
        if not hits:
            print(f"No source basename hashes to {wanted:08X}.")
            print("The assert may come from a file outside the repo tree.")
            return 1
        print(f"{wanted:08X} matches:")
        for name in sorted(hits):
            for rel in sorted(names[name]):
                print(f"  {rel}")
        return 0

    for name in sorted(table):
        print(f"{table[name]:08X}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
