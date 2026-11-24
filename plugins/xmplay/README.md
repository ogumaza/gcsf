# xmp-gcsf

XMPlay input plugin for GCSF rips (`.minigcsf`). It uses the same player core as `gcsfplay`. The player core runs the
game code on an emulated GameCube: the OS, the sound library and the DSP program of the game.

XMPlay is a 32-bit program for Windows. The plugin is therefore a 32-bit DLL, `xmp-gcsf.dll`, built with clang-cl. It
requires XMPlay 3.8.2 or later.

## Installing

The Releases page has `xmp-gcsf-X.Y.Z.zip`, with the DLL and the licences. Put `xmp-gcsf.dll` in XMPlay's folder, or in
a folder inside it, and restart XMPlay.

## Features

- Tags become XMPlay tags: `game` as album and `year` as date. The Message info window lists every tag except `_lib`,
  `length`, `fade`, `loop_start`, `loop_length`, `volume`, `utf8` and `gcsf_var`.
- Length and fade come from the `length` and `fade` tags. Files without a length tag play for the default length and
  fade. They stop early when the sound ends by itself.
- Each track plays as a soundtrack album plays it: from the first sound, after 0.5 s of silence, with 1 s of silence
  after it. A loop plays the passes in its tags, or another number of passes with its `loop_start` and `loop_length`
  tags.
- The options page of the plugin, among the decoders in the XMPlay options: default length and fade, the curve of the
  fade, endless playback, rendering ahead, and the settings of an album track: skip silence at the start, the silences
  before and after a track, the loops, one pass for long loops, the fade length for loops, ignore length and fade tags,
  and skip silence at the end. XMPlay stores the settings.
- During playback, the plugin renders ahead on a separate thread as fast as the CPU allows. It buffers up to 10 minutes
  and 10 seconds of audio (about 75 MiB). That holds a whole track of up to 10 minutes and its fade. `gcsfrip` gives a
  longer length only to music whose two passes take longer. Seeking within the buffer is instant. Seeking further ahead
  waits for rendering to reach the requested position. Rendering ahead keeps one CPU core busy until it reaches the end
  of the track or the buffer limit. Longer tracks also save the state of the emulation every 2 seconds. Seeking back
  before the buffered audio restores the latest state before the target and renders from there. At most 128 states are
  kept. The intervals between them grow as the track continues. With rendering ahead turned off, every track saves
  states. Seeking past what has played then renders up to the target.
- Output is 16-bit stereo at 32,028 Hz, the rate of the GameCube audio interface, rounded down. It isn't resampled.

## Building

**On GitHub (no Windows machine needed):** the `XMPlay plugin` workflow (`.github/workflows/xmplay.yml`) builds the DLL
and uploads it with the licences. Start it from the Actions tab of the repository (Run workflow). GitHub lists a manual
workflow only once it's on the default branch (`main`). The workflow requires GitHub Actions to be enabled for the
repository. It downloads the XMPlay plugin SDK from un4seen.com (`externals/xsflib/ci/xmplay-sdk`).

**On Windows:** you need Visual Studio 2022 or later with the C++ workload and the "C++ Clang tools for Windows"
component, plus CMake 3.20 or later. Download the XMPlay plugin SDK (<https://www.un4seen.com/files/xmp-sdk.zip>) and
unpack it. Use the folder that contains `xmpin.h` as the SDK path below.

```
cmake -S . -B build-xmplay -A Win32 -T ClangCL -DXMPLAY_SDK_DIR=C:/path/to/xmp-sdk
cmake --build build-xmplay --config Release --target xmp-gcsf
```

This produces `build-xmplay/plugins/xmplay/Release/xmp-gcsf.dll`. Everything is built with the static C runtime (`/MT`).
The plugin then requires no runtime DLLs.

## Performance

Playback runs on one CPU core. Most of the time goes to emulating the DSP and the GameCube CPU. A 64-bit build is faster
than a 32-bit one. The 64-bit foobar2000 component therefore takes less CPU time than the plugin for the same rips.

## Licence

Like the rest of GCSF, the plugin is MIT-licensed. It contains xsflib and ntdlib (both MIT) and uses zlib. Ship
`LICENSE` and `THIRD-PARTY-NOTICES.md` (or their text) with a build. `THIRD-PARTY-NOTICES.md` contains the licences of
xsflib and ntdlib. The XMPlay plugin SDK states no licence. The plugin uses only the declarations of the XMPlay
interface in the SDK. It contains no code from the SDK.
