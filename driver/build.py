#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build a GCSF guest driver and regenerate its blob header.

Without a name, the script builds Four Swords Adventures' driver, driver.c, into src/rip/driver_blob.h. With a name,
it builds driver/NAME.c into src/rip/driver_NAME_blob.h, whose constants are in the namespace gcsf::rip::NAME.

The script runs ntdlib's tools/build_ppc_driver.py with GCSF's paths and names. That tool requires a clang with the
PowerPC target. LLVM's releases and Homebrew's llvm have that target. Apple's clang doesn't. Run the script only when
a driver's source changes. The generated file is committed. Building GCSF itself requires no PowerPC toolchain.

usage: driver/build.py [NAME] [--clang PATH]
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    args = sys.argv[1:]
    name = args.pop(0) if args and not args[0].startswith("-") else None
    if name:
        source, output, namespace = f"driver/{name}.c", f"src/rip/driver_{name}_blob.h", f"gcsf::rip::{name}"
        command = f"driver/build.py {name}"
    else:
        source, output, namespace, command = "driver/driver.c", "src/rip/driver_blob.h", "gcsf::rip", "driver/build.py"
    if not (ROOT / source).is_file():
        sys.exit(f"error: no driver {source}")

    tool = ROOT / "externals" / "ntdlib" / "tools" / "build_ppc_driver.py"
    sys.exit(subprocess.run([sys.executable, str(tool), "--source", source, "--output", output, "--namespace",
                             namespace, "--product", "GCSF", "--command", command, *args], cwd=ROOT).returncode)


if __name__ == "__main__":
    main()
