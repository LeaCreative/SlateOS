#!/usr/bin/env python3
"""Pack a directory into a .slate zip and print SHA-256 (for index entries)."""
from __future__ import annotations

import hashlib
import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <app-dir> <out.slate>", file=sys.stderr)
        return 2
    src = Path(sys.argv[1])
    out = Path(sys.argv[2])
    if not (src / "manifest.json").is_file():
        print("manifest.json required", file=sys.stderr)
        return 1
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(src.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(src).as_posix())
    digest = hashlib.sha256(out.read_bytes()).hexdigest()
    print(digest)
    print(out.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
