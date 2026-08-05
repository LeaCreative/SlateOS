#!/usr/bin/env python3
"""Package a JS sub-app directory into a .zip the companion can sideload.

    python tools/pack-subapp.py companion/examples/timer

Writes build/subapps/<id>-<version>.zip. Send that file to the phone by any
means and open it — Android routes it to SideloadActivity, which installs it
into the same store the repository uses. No APK reinstall.

The directory must contain manifest.json and the entry script it names.
"""
import json
import pathlib
import sys
import zipfile


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2

    src = pathlib.Path(argv[1]).resolve()
    manifest_path = src / "manifest.json"
    if not manifest_path.is_file():
        print(f"error: no manifest.json in {src}")
        return 1

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for key in ("id", "version", "entry"):
        if key not in manifest:
            print(f"error: manifest.json is missing '{key}'")
            return 1

    entry = src / manifest["entry"]
    if not entry.is_file():
        print(f"error: entry '{manifest['entry']}' not found in {src}")
        return 1

    out_dir = pathlib.Path(__file__).resolve().parent.parent / "build" / "subapps"
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{manifest['id']}-{manifest['version']}.zip"

    # Skip docs and editor noise; everything else ships so a sub-app can carry
    # assets alongside its script.
    skip_suffixes = {".md", ".swp", ".orig"}
    written = []
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(src.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix in skip_suffixes:
                continue
            rel = path.relative_to(src).as_posix()
            z.write(path, rel)
            written.append(rel)

    print(f"wrote {out}  ({out.stat().st_size} bytes)")
    print(f"  id      {manifest['id']}")
    print(f"  version {manifest['version']}")
    print(f"  entry   {manifest['entry']}")
    print(f"  files   {', '.join(written)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
