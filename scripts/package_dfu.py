#!/usr/bin/env python3
"""Build an InfiniTime-compatible MCUBoot image + Nordic DFU zip for sealed watches.

Uses `imgtool create` (unsigned) with slot-size 475136 — same contract as
InfiniTime's create_image.sh / pinetime-mcuboot-bootloader.

  python scripts/package_dfu.py --bin build/debug/slate_firmware.bin --version 0.1.0

Requires: imgtool (pip install imgtool), adafruit-nrfutil on PATH for the zip step.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# InfiniTime primary / secondary slot size (bytes).
SLOT_SIZE = 475136  # 0x74000
HEADER_SIZE = 32


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--bin",
        type=Path,
        required=True,
        help="Raw app .bin (linked at 0x8020; imgtool --pad-header adds 32B header)",
    )
    ap.add_argument("--version", required=True, help="Semantic version, e.g. 0.1.0")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory (default: same as --bin)",
    )
    ap.add_argument(
        "--skip-zip",
        action="store_true",
        help="Only write the MCUBoot image; skip adafruit-nrfutil",
    )
    args = ap.parse_args()

    if not args.bin.is_file():
        print(f"missing bin: {args.bin}", file=sys.stderr)
        return 1

    imgtool = shutil.which("imgtool") or shutil.which("imgtool.py")
    if not imgtool:
        print("imgtool not on PATH — pip install imgtool", file=sys.stderr)
        return 1

    out_dir = args.out_dir or args.bin.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    image = out_dir / "slate-mcuboot-image.bin"
    zip_path = out_dir / "slate-dfu.zip"

    cmd = [
        imgtool,
        "create",
        "--align",
        "4",
        "--header-size",
        str(HEADER_SIZE),
        "--pad-header",
        "--slot-size",
        str(SLOT_SIZE),
        "--version",
        args.version,
        str(args.bin),
        str(image),
    ]
    print(" ".join(cmd))
    subprocess.check_call(cmd)
    print(f"wrote {image}")

    if args.skip_zip:
        return 0

    nrfutil = shutil.which("adafruit-nrfutil") or shutil.which("nrfutil")
    if not nrfutil:
        print(
            "adafruit-nrfutil not on PATH — image ready; install adafruit-nrfutil to pack zip",
            file=sys.stderr,
        )
        print(f"  adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application {image} {zip_path}")
        return 0

    zcmd = [
        nrfutil,
        "dfu",
        "genpkg",
        "--dev-type",
        "0x0052",
        "--application",
        str(image),
        str(zip_path),
    ]
    print(" ".join(zcmd))
    subprocess.check_call(zcmd)
    print(f"wrote {zip_path}")
    print("Flash with Gadgetbridge / Amazfish / nRF Connect (legacy DFU) while InfiniTime is running.")
    print("See docs/flash-sealed.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
