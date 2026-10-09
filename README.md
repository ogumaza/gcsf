# gcsf

GCSF is a PSF-family music rip format for Nintendo GameCube sequenced audio. It covers the music and sound effects
that GameCube games play through JAudio2, the sound library of Nintendo EAD's JSystem. GCSF is the GameCube counterpart
of the DS formats 2SF and NCSF.

A GCSF rip contains the game's code. The player runs that code on an emulated GameCube: the game's OS, its sound
library and its DSP program. The game plays the sound itself. The mixing, filtering and resampling therefore match the
game. A rip requires a driver profile for the game.

`docs/gcsf.md` specifies the format. Version 1 supports The Legend of Zelda: Four Swords Adventures (USA).

## Tools

| Tool | Description |
|---|---|
| `gcsfrip [options] <input>...` | Rips each input into a `.gcsflib` and a `.minigcsf` per sound. The output goes in a folder next to the input. The folder has the input's name with `_gcsf` added. An input is a GameCube disc image or an extracted disc. An extracted disc is a folder with `sys/main.dol` and `files/`, either directly or in a `DATA` folder. Dolphin extracts discs in these layouts. `--list` lists a game's sounds. `--only` and `--bgm` choose what to rip. Sounds that make no sound are listed and skipped. `--help` shows the rest. |
| `gcsfplay [options] <file>...` | Renders each `.minigcsf` to a WAV file next to it at 32,028 Hz. The length and fade come from the file's tags. `-o FILE` writes FILE instead. Its extension selects WAV or FLAC. `--length` and `--fade` override the tags. `--info` shows a file's tags and libraries. `--help` shows the rest. |
| `plugins/foobar2000` | foobar2000 input component: `foo_input_gcsf.component` for macOS and `foo_input_gcsf.dll` for Windows. Both are built with the foobar2000 SDK (see its [README](plugins/foobar2000/README.md)). |

`gcsfrip` reads disc images in every common format: ISO (`.iso`, `.gcm`), NKit (`.nkit.iso`), RVZ, WIA, GCZ, CISO and
WDF, whole or split into pieces.

Both tools accept multiple inputs and continue if one fails. A nonexistent input path stops the command before it writes
any files. For example, `gcsfrip game.rvz` writes `game_gcsf/`. `gcsfplay game_gcsf/*.minigcsf` then renders every
sound in it. `--version` shows a tool's version.

### Downloads

The Releases page has `gcsfrip` and `gcsfplay` for 64-bit and 32-bit Windows, and the foobar2000 component for 64-bit
and 32-bit foobar2000 on Windows. On other systems, build GCSF from source (see "Building").

### Drag and drop

- **Windows:** drop files on `gcsfrip.exe` or `gcsfplay.exe`. A console window shows the results and stays open until
  you press Enter.
- **macOS:** the build also makes `gcsfrip.app` and `gcsfplay.app` in the build folder. Drop files on one, or open it
  and choose them. The app reports the results for each file and offers to open the output folders.

As above, the results go next to the files. File and folder names can use any characters. On Windows, that requires
Windows 10 version 1903 or later.

## Building

You need Git, CMake 3.20 or later, and C and C++20 compilers. zlib, Zstandard, liblzma, bzip2 and libFLAC are downloaded
and built along with GCSF when they aren't installed.

- **Windows:** Visual Studio 2022 or later with the "Desktop development with C++" workload and its "C++ Clang tools for
  Windows" component. The workload includes CMake. The component adds clang-cl. Configure with `cmake -S . -B build -T
  ClangCL` instead of the configure command below. Run the commands in a Developer PowerShell or Developer Command
  Prompt.
- **macOS:** the Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`). `brew install
  zstd xz flac` saves building those three. A universal build (two architectures in `CMAKE_OSX_ARCHITECTURES`) still
  builds them.
- **Linux:** GCC or Clang, CMake and Git from your distribution. The development packages of zlib, Zstandard, liblzma,
  bzip2 and libFLAC (on Debian and Ubuntu, `zlib1g-dev libzstd-dev liblzma-dev libbz2-dev libflac-dev`) save building
  them.

MinGW, GCC and GNU-style Clang aren't supported on Windows. GCC and Clang aren't supported for 32-bit x86 because their
floating-point arithmetic there gives different results from the other builds. CMake rejects these configurations.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

`CMakePresets.json` has `release` and `debug` presets for the same steps: `cmake --preset release`, `cmake --build
--preset release` and `ctest --preset release`. The presets require CMake 3.21 or later.

The disc test (`tests/disc_test.cpp`) builds a small GameCube disc and stores it in every format that the reader
supports, with each compression method and layout. It then checks that every file reads back the same.

## Layout

```
src/common/   small helpers shared by the rest (byte order, SHA-1, DOLs)
src/disc/     disc images in every format, and the files of a game's disc
src/jaudio/   JAudio2's start-up archives: the sound table and the wave systems
src/dsp/      the DSP, which runs the game's DSP program
src/ppc/      the Gekko CPU
src/gc/       the GameCube: its hardware, and a disc made of a rip's files
src/gcsf/     the format and the player
src/rip/      the ripper, the game profiles, the length analysis and the driver's image
src/cli/      gcsfrip and gcsfplay
plugins/foobar2000/  the foobar2000 component
driver/       the driver's source, and the script that builds it
platform/     the Windows manifest and version resource, and the macOS droplet apps of gcsfrip and gcsfplay
docs/         the format's specification
tests/        ctest suites
```

The driver runs on the emulated GameCube. `driver/build.py` builds it for PowerPC and requires a clang with the PowerPC
target. The driver's image is committed as `src/rip/driver_blob.h`. Building GCSF itself requires no PowerPC
toolchain.

## Credits

GCSF is built from WSF, the Wii's rip format in the same family: its CPU, DSP, container, player, tools and foobar2000
component started as WSF's code. That code keeps WSF's copyright notice in `LICENSE`.

## Licence

GCSF is under the MIT licence (`LICENSE`). `THIRD-PARTY-NOTICES.md` lists the libraries that GCSF uses and what a
binary distribution must include. This licence doesn't cover game data. Rips contain a game's code and sound data and
are for personal use.
