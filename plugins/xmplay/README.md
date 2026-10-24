# xmp-gcsf

XMPlay input plugin for GCSF rips (`.minigcsf`). It uses the same player core as `gcsfplay`. The player core runs the
game's code on an emulated GameCube: the game's OS, its sound library and its DSP program.

XMPlay is a 32-bit program for Windows. The plugin is therefore a 32-bit DLL, `xmp-gcsf.dll`, built with clang-cl. It
requires XMPlay 3.8.2 or later.

## Installing

The Releases page has `xmp-gcsf-X.Y.Z.zip`, with the DLL and the licences. Put `xmp-gcsf.dll` in XMPlay's folder, or
in a folder inside it, and restart XMPlay.

## Features

- Tags become XMPlay's tags: `game` as album and `year` as date. The Message info window lists every tag except
  `_lib`, `length`, `fade`, `volume`, `utf8` and `gcsf_var`.
- Length and fade come from the `length` and `fade` tags. Files without a length tag play for the default length and
  fade. They stop early when the sound ends by itself.
- The plugin's options page, among the decoders in XMPlay's options: default length and fade, endless playback, and
  rendering ahead. XMPlay stores the settings.
- During playback, the plugin renders ahead on a separate thread as fast as the CPU allows. It buffers up to 10 minutes
  and 10 seconds of audio (about 75 MiB). That's enough for the longest track that `gcsfrip`'s length analysis
  produces. Seeking within the buffer is instant. Seeking further ahead waits for rendering to reach the requested
  position. Rendering ahead keeps one CPU core busy until it reaches the end of the track or the buffer limit. Longer
  tracks also save the emulation's state every 2 seconds. Seeking back before the buffered audio restores the latest
  state before the target and renders from there. At most 128 states are kept. The intervals between them grow as the
  track continues. With rendering ahead turned off, every track saves states. Seeking past what has played then renders
  up to the target.
- Output is 16-bit stereo at 32,028 Hz, the GameCube audio interface's rate rounded down. It isn't resampled.

## Building

**On GitHub (no Windows machine needed):** the `XMPlay plugin` workflow (`.github/workflows/xmplay.yml`) builds the DLL
and uploads it with the licences. Start it from the repository's Actions tab (Run workflow). GitHub lists a manual
workflow only once it's on the default branch (`main`). The workflow requires GitHub Actions to be enabled for the
repository. It downloads the XMPlay plugin SDK from un4seen.com (`externals/xsflib/ci/xmplay-sdk`).

**On Windows:** you need Visual Studio 2022 or later with the C++ workload and the "C++ Clang tools for Windows"
component, plus CMake 3.20 or later. Download the XMPlay plugin SDK (<https://www.un4seen.com/files/xmp-sdk.zip>) and
unpack it. Use the folder that holds `xmpin.h` as the SDK path below.

```
cmake -S . -B build-xmplay -A Win32 -T ClangCL -DXMPLAY_SDK_DIR=C:/path/to/xmp-sdk
cmake --build build-xmplay --config Release --target xmp-gcsf
```

This produces `build-xmplay/plugins/xmplay/Release/xmp-gcsf.dll`. Everything is built with the static C runtime
(`/MT`). The plugin then requires no runtime DLLs.

## Performance

Playback runs on one CPU core. Most of the time goes to emulating the DSP and the GameCube's CPU. A 64-bit build is
faster than a 32-bit one. The 64-bit foobar2000 component therefore takes less CPU time than the plugin for the same
rips.

## Licence

Like the rest of GCSF, the plugin is MIT-licensed. It contains xsflib and ntdlib (both MIT) and uses zlib. Ship
`LICENSE` and `THIRD-PARTY-NOTICES.md` (or their text) with a build. `THIRD-PARTY-NOTICES.md` holds the licences of
xsflib and ntdlib. The XMPlay plugin SDK states no licence. The plugin uses only the SDK's declarations of XMPlay's
interface. No code from the SDK goes into it.
