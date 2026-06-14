#!/usr/bin/env python3
import json
import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: package_inspect.py <path.geode>", file=sys.stderr)
        return 2

    package = Path(sys.argv[1])
    if not package.is_file():
        print(f"missing package: {package}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(package) as archive:
        names = set(archive.namelist())
        if "mod.json" not in names:
            print("mod.json missing", file=sys.stderr)
            return 1

        manifest = json.loads(archive.read("mod.json").decode("utf-8"))
        expected = {
            "id": "exploited.comsplus",
            "name": "ComsPlus",
            "developer": "Exploited",
        }
        for key, value in expected.items():
            if manifest.get(key) != value:
                print(f"{key} mismatch: {manifest.get(key)!r} != {value!r}", file=sys.stderr)
                return 1

        binaries = [
            name for name in names
            if name.endswith((".dll", ".so", ".dylib")) or ".android64.so" in name or ".android32.so" in name
        ]
        if not binaries:
            print("no platform binary found", file=sys.stderr)
            return 1

        print(f"OK {package}")
        print(f"manifest: {manifest['id']} {manifest['version']} by {manifest['developer']}")
        print("binaries:")
        for binary in sorted(binaries):
            print(f" - {binary}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
