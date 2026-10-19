#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a GCSF guest driver and regenerate its blob header.

The script builds driver/NAME.c into src/rip/driver_NAME_blob.h, whose constants are in the namespace gcsf::rip::NAME.
It runs ntdlib's tools/build_ppc_driver.py with GCSF's paths and names. That tool requires a clang with the PowerPC
target. LLVM's releases and Homebrew's llvm have that target. Apple's clang doesn't. Run the script when a driver's
source changes, and for every driver when a header in driver/ changes. The generated files are committed. Building GCSF
itself requires no PowerPC toolchain.

usage: driver/build.py NAME [--clang PATH]
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith("-"):
        sys.exit("usage: driver/build.py NAME [--clang PATH]")
    name = args.pop(0)
    source = f"driver/{name}.c"
    if not (ROOT / source).is_file():
        sys.exit(f"error: no driver {source}")

    tool = ROOT / "externals" / "ntdlib" / "tools" / "build_ppc_driver.py"
    sys.exit(subprocess.run([sys.executable, str(tool), "--source", source, "--output", f"src/rip/driver_{name}_blob.h",
                             "--namespace", f"gcsf::rip::{name}", "--product", "GCSF", "--command",
                             f"driver/build.py {name}", *args], cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
