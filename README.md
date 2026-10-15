# gcsf

GCSF is a PSF-family music rip format for Nintendo GameCube sequenced audio. It covers the music and sound effects
that GameCube games play through JAudio, the sound library of Nintendo EAD's JSystem. GCSF reads both of JAudio's
generations: JAudio1 and JAudio2. MusyX is Factor 5's sound library. GCSF also covers the songs that a game plays
through it. Final Fantasy Crystal Chronicles plays its music and its sound effects through Square's Red driver. GCSF
covers them too. GCSF is the GameCube counterpart of the DS formats 2SF and NCSF.

A GCSF rip contains the game's code. The player runs that code on an emulated GameCube: the game's OS, its sound
library and its DSP program. The game plays the sound itself. The mixing, filtering and resampling therefore match the
game. A rip requires a driver profile for the game.

`docs/gcsf.md` specifies the format. Version 1 supports The Legend of Zelda: Four Swords Adventures (USA) and its
Japanese release, The Legend of Zelda: Four Swords Plus, The Legend of Zelda: Twilight Princess (USA), The Legend of
Zelda: The Wind Waker (USA), Luigi's Mansion (USA), Super Mario Sunshine (USA), Pikmin (USA), Donkey Kong Jungle Beat
(USA) and Mario Kart: Double Dash!! (USA). It also supports the 38 songs and 9 jingles of Mario Golf: Toadstool Tour
(USA), the 111 songs and jingles of Mario Power Tennis (USA), the 231 pieces in the Sound Room of WarioWare, Inc.: Mega
Party Game$! (USA), the sequenced music and sound effects of Pikmin 2 (USA) and those of Final Fantasy Crystal
Chronicles (USA). Pac-Man Vs. (USA) and The Legend of Zelda: Collector's Edition (USA) stream
their music. GCSF rips the sequenced sound effects of Pac-Man Vs. and of the menu of Collector's Edition. The menu's
sound table also lists one sequenced piece of music, `JA_BGM_DUMMY`. The menu's code never starts it. GCSF rips it too.
Navi Trackers, a mode that only Four Swords Plus has, plays its music from streams. So does Mario Kart: Double Dash!! on
its courses. GCSF doesn't rip streams. The Wind Waker, Luigi's Mansion and Pac-Man Vs. name none of their sounds. A rip
names an effect after its category and index, for example `se4-0123`, and a piece of music after its file. Super Mario
Sunshine keeps a table of its sounds' names. A rip of it takes its names from that table, for example `MSD_BGM_DOLPIC`.
Pikmin's code lists its pieces of music by their files' names. A rip of Pikmin names a piece after its file, for example
`select`. The game plays the music of its cutscenes as parts of one piece. A rip names a part after the piece and the
part's number, for example `demobgm-5`. The rip leaves out the game's sound effects and the game's streamed music.
Pikmin 2's pieces of music are named after their files too. The game keeps a table of its effects' names. A rip of an
effect takes its name from that table, for example `PSSE_SY_MENU_CURSOR`. A rip of Final Fantasy Crystal Chronicles
names a piece or an effect after its file, for example `music100` or `se010001`. An effect in one of the game's blocks
of effects takes the block's name and its place in the block, for example `se000-001`. The game's streamed music isn't
ripped.

## Tools

| Tool | Description |
|---|---|
| `gcsfrip [options] <input>...` | Rips each input into a `.gcsflib` and a `.minigcsf` per sound. The output goes in a folder next to the input. The folder has the input's name with `_gcsf` added. An input is a GameCube disc image or an extracted disc. An extracted disc is a folder with `sys/main.dol` and `files/`, either directly or in a `DATA` folder. Dolphin extracts discs in these layouts. `--list` lists a game's sounds. `--only` and `--bgm` choose what to rip. Sounds that make no sound are listed and skipped. Streams are left out. The sequence archive can hold music that no sound of the game plays. Each such sequence gets a `.minigcsf` too, named after its file. `--sound-files` takes the sound files from another release of the game (see below). `--help` shows the rest. |
| `gcsfplay [options] <file>...` | Renders each `.minigcsf` to a WAV file next to it at 32,028 Hz. The length and fade come from the file's tags. `-o FILE` writes FILE instead. Its extension selects WAV or FLAC. `--length` and `--fade` override the tags. `--info` shows a file's tags and libraries. `--help` shows the rest. |
| `plugins/foobar2000` | foobar2000 input component: `foo_input_gcsf.component` for macOS and `foo_input_gcsf.dll` for Windows. Both are built with the foobar2000 SDK (see its [README](plugins/foobar2000/README.md)). |
| `plugins/xmplay` | XMPlay input plugin: `xmp-gcsf.dll`, a 32-bit DLL for XMPlay on Windows, built with clang-cl and the XMPlay plugin SDK (see its [README](plugins/xmplay/README.md)). |

`gcsfrip` reads disc images in every common format: ISO (`.iso`, `.gcm`), NKit (`.nkit.iso`), RVZ, WIA, GCZ, CISO and
WDF, whole or split into pieces.

Both tools accept multiple inputs and continue if one fails. A nonexistent input path stops the command before it writes
any files. For example, `gcsfrip game.rvz` writes `game_gcsf/`. `gcsfplay game_gcsf/*.minigcsf` then renders every
sound in it. `--version` shows a tool's version.

### Sound files from another release

Twilight Princess also came out on the Wii, and on the Wii U as Twilight Princess HD. Both keep their sound files in
the GameCube's formats and places, and the GameCube's code plays them. `gcsfrip --sound-files DIR` rips a GameCube disc
with another release's sound files in place of the disc's:

    gcsfrip --bgm --sound-files "Twilight Princess (Wii)" "Twilight Princess (GameCube).rvz"

`DIR` holds the other release's files. `gcsfrip` looks for the start-up archive, `Z2Sound.baa`, anywhere in `DIR`. It
reads the sequence archive (`Seqs/`) and the wave archives (`Waves/`) next to it. The game's code and its other files
come from the GameCube disc. The disc is still required.

- **The Wii.** Extract the disc first. Dolphin extracts a disc from the Filesystem tab of the game's properties. Wiimm's
  ISO Tools extract one with `wit extract`.
- **The Wii U.** `DIR` is the game's title folder from a dump of the game, with `code/`, `content/` and `meta/`. The
  sound files are in `content/Audiores/`. They are the Wii's files, apart from `HomeMenu_0.aw`. Twilight Princess HD
  lacks that file. `gcsfrip` warns about it, and the sounds that use it play without its waves.

The Wii and the Wii U changed four pieces of music. `Z2BGM_BOSS_SNOWWOMAN_D1`, `Z2BGM_WCS_D01` and `Z2BGM_TEMP` sound
different, and `Z2BGM_RODEO` differs a little. The rest is the GameCube's music. `Z2BGM_MENU_SELECT` is a sequence
only on the GameCube. The other releases play it from a stream. GCSF doesn't rip streams.

The Wind Waker HD, on the Wii U, works the same way with a disc of The Wind Waker:

    gcsfrip --sound-files "The Wind Waker HD" "The Wind Waker (GameCube).ciso"

`DIR` is the title folder from a dump of the game. In the American release, the sound files are in
`content/Cafe/US/AudioRes/JAudioRes/`. The Wind Waker HD keeps its sequences in one table, `Seqs/JaiSeq.bdt`, where the
GameCube has an archive. `gcsfrip` reads the table into the disc's archive. The Wind Waker HD changed five sequences.
`p_bigpow`, `p_boco` and `tower` sound different. `defaultse`, the sequence of the sound effects, holds the new effects.
`baachan` only gained padding. The sound table has 98 pieces of music. The last one plays `get_perl` again. `tower`
plays louder than on the GameCube. Its volume in the table is 65 of 127. The GameCube's table gives it 40. Many waves
were resampled from 16,000 Hz to 28,000 Hz. Some new ones play at 44,100 Hz.

### Downloads

The Releases page has `gcsfrip` and `gcsfplay` for 64-bit Windows, the foobar2000 component for 64-bit and 32-bit
foobar2000 on Windows, and the XMPlay plugin. On macOS, build GCSF from source (see "Building").

To install the XMPlay plugin, unzip `xmp-gcsf-X.Y.Z.zip` and put `xmp-gcsf.dll` in XMPlay's folder, or in a folder
inside it. Then restart XMPlay.

### Drag and drop

- **Windows:** drop files on `gcsfrip.exe` or `gcsfplay.exe`. A console window shows the results and stays open until
  you press Enter.
- **macOS:** the build also makes `gcsfrip.app` and `gcsfplay.app` in the build folder. Drop files on one, or open it
  and choose them. The app reports the results for each file and offers to open the output folders.

As above, the results go next to the files. File and folder names can use any characters. On Windows, that requires
Windows 10 version 1903 or later.

## Building

You need Git, CMake 3.20 or later, and C and C++20 compilers. GCSF builds on two libraries: xsflib, for players and
rippers of the PSF family, and ntdlib, for the hardware and formats of Nintendo's consoles. They're Git submodules in
`externals/xsflib` and `externals/ntdlib`: clone GCSF with `git clone --recurse-submodules`, or run
`git submodule update --init --recursive` in a clone. zlib, Zstandard, liblzma, bzip2 and libFLAC are downloaded and
built along with GCSF when they aren't installed.

- **Windows:** Visual Studio 2022 or later with the "Desktop development with C++" workload and its "C++ Clang tools for
  Windows" component. The workload includes CMake. The component adds clang-cl. Configure with `cmake -S . -B build -A
  x64 -T ClangCL` instead of the configure command below. Run the commands in a Developer PowerShell or Developer
  Command Prompt.
- **macOS on Apple Silicon:** the Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`).
  `brew install zstd xz flac` saves building those three.

Linux, Intel Macs, MSVC and 32-bit Windows aren't supported. The exceptions are the 32-bit foobar2000 component and the
XMPlay plugin. Build them with clang-cl, configured with `-A Win32 -T ClangCL`.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

`CMakePresets.json` has `release` and `debug` presets for the same steps: `cmake --preset release`, `cmake --build
--preset release` and `ctest --preset release`. The presets require CMake 3.23 or later. They don't select clang-cl. On
Windows, use the commands above.

ntdlib has the tests of the code in it, such as the disc readers and the DSP. They run when ntdlib is built by
itself. Its disc test builds a small GameCube disc and stores it in every format that the reader supports, with each
compression method and layout. The test then checks that every file reads back the same.

## Layout

```
src/gc/       the GameCube: its disc interface, and the machine that game mode runs
src/gcsf/     the format and the player
src/rip/      the ripper, the game profiles, the length analysis and the drivers' images
src/cli/      gcsfrip and gcsfplay
plugins/foobar2000/  the foobar2000 component
plugins/xmplay/      the XMPlay plugin
driver/       the drivers' sources and the script that builds them
docs/         the format's specification and the release notes
tests/        ctest suites
externals/xsflib/  xsflib: the PSF container, the player framework, the tools' frames, and the build's shared parts
externals/ntdlib/  ntdlib: the CPU, the DSP and the devices that the GameCube shares with the Wii, disc images in
              every format, and JAudio's sound files and length analysis
```

A driver runs on the emulated GameCube. `driver/build.py` builds one for PowerPC with ntdlib's
`tools/build_ppc_driver.py`, and requires a clang with the PowerPC target. The drivers' images are committed in
`src/rip/`: `driver_blob.h` for Four Swords Adventures, `driver_twilight_princess_blob.h` for Twilight Princess,
`driver_zel_blob.h` for The Wind Waker, `driver_luigis_mansion_blob.h` for Luigi's Mansion,
`driver_super_mario_sunshine_blob.h` for Super Mario Sunshine, `driver_pikmin_blob.h` for Pikmin,
`driver_jungle_beat_blob.h` for Donkey Kong Jungle Beat, `driver_double_dash_blob.h` for Mario Kart: Double Dash!!,
`driver_mario_golf_blob.h` for Mario Golf: Toadstool Tour and Mario Power Tennis, `driver_warioware_blob.h` for
WarioWare, Inc.: Mega Party Game$!, `driver_pikmin2_blob.h` for Pikmin 2, `driver_pacman_vs_blob.h` for Pac-Man Vs. and
`driver_collectors_edition_blob.h` for The Legend of Zelda: Collector's Edition and `driver_crystal_chronicles_blob.h`
for Final Fantasy Crystal Chronicles. Building GCSF itself requires no
PowerPC toolchain.

## Licence

GCSF is under the MIT licence (`LICENSE`), and so are xsflib and ntdlib. `THIRD-PARTY-NOTICES.md` lists the libraries
that GCSF uses and what a binary distribution must include. This licence doesn't cover game data. Rips contain a game's
code and sound data and are for personal use.