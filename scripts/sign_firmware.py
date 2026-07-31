#!/usr/bin/env python3
"""Sign a Slate firmware .bin into an MCUBoot image (ECDSA-P256).

Requires: imgtool (pip install imgtool) and the private key path.

  python scripts/sign_firmware.py --bin build/power/slate_firmware.bin \\
      --version 1.0.0 --key /secure/slate_priv.pem
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SLOT_SIZE = 0x74000  # InfiniTime primary (475136) — custom-BL / signed path
HEADER_SIZE = 32


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bin", type=Path, required=True, help="Raw app .bin (linked at 0x8000)")
    ap.add_argument("--version", required=True, help="Semantic version for imgtool, e.g. 1.2.0")
    ap.add_argument("--key", type=Path, required=True, help="ECDSA-P256 private key PEM")
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Output signed image (default: <bin>-signed.bin)",
    )
    ap.add_argument(
        "--slot-size",
        type=lambda s: int(s, 0),
        default=SLOT_SIZE,
        help="MCUBoot slot size (default 0x74000 InfiniTime)",
    )
    args = ap.parse_args()

    if not args.bin.is_file():
        print(f"missing bin: {args.bin}", file=sys.stderr)
        return 1
    if not args.key.is_file():
        print(f"missing key: {args.key}", file=sys.stderr)
        return 1
    imgtool = shutil.which("imgtool") or shutil.which("imgtool.py")
    if not imgtool:
        print("imgtool not on PATH — pip install imgtool", file=sys.stderr)
        return 1

    out = args.out or args.bin.with_name(args.bin.stem + "-signed.bin")
    cmd = [
        imgtool,
        "sign",
        "--key",
        str(args.key),
        "--header-size",
        str(HEADER_SIZE),
        "--align",
        "4",
        "--slot-size",
        hex(args.slot_size),
        "--version",
        args.version,
        "--pad-header",
        str(args.bin),
        str(out),
    ]
    print(" ".join(cmd))
    subprocess.check_call(cmd)
    print(f"wrote {out}")
    print("For sealed InfiniTime MCUBoot (unsigned create + zip), prefer:")
    print(f"  python scripts/package_dfu.py --bin {args.bin} --version {args.version}")
    print("Pack signed DFU zip with:")
    print(
        f"  adafruit-nrfutil dfu genpkg --dev-type 0x0052 "
        f"--application {out} slate-dfu.zip"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
