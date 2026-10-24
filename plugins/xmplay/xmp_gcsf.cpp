// SPDX-License-Identifier: MIT

// xmp-gcsf: XMPlay input plugin for GCSF (.minigcsf and .gcsf), from xsflib's plugin template (xsflib/xmplay/input.h).
// The template's comment says what the plugin does. This file gives GCSF's traits: its player, names, extensions and
// limits.
//
// The player core (src/gcsf) runs the game's sound code on an emulated GameCube, and the game's DSP program on an
// emulated DSP.
//
// Build: see plugins/xmplay/README.md (clang-cl for 32-bit Windows, with the XMPlay plugin SDK).

#include <windows.h>
#include <xmpin.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "xsflib/psf/psf.h"
#include "xsflib/xmplay/input.h"

#include "gcsf/format.h"
#include "gcsf/player.h"

namespace
{

struct Traits
{
    using Player = gcsf::Player;
    using Set = gcsf::LoadedSet;

    static constexpr const char* kName = "GCSF";
    static constexpr std::array<const char*, 2> kExtensions = {"minigcsf", "gcsf"};
    static constexpr uint8_t kPsfVersion = gcsf::kPsfVersionGcsf;
    static constexpr uint64_t kMaxFileSize = 256u << 20; // largest file read into memory

    // An effect that waits for a port that the game sets would go silent if removing the tags dropped gcsf_var.
    static constexpr std::array<const char*, 1> kPlaybackTags = {gcsf::kVariablesTag};

    static constexpr const char* kVersion = GCSF_VERSION;
    static constexpr const char* kAbout =
        "Plays GCSF (Nintendo GameCube sequenced music) rips. The game's sound code runs on an emulated GameCube, and "
        "the game's DSP program runs on an emulated DSP.\n\n"
        "MIT licence. Uses zlib.";

    static std::optional<std::string> LoadSet(const std::string& path, const xsflib::psf::FileReader& reader, Set& out,
                                              std::string_view separators)
    {
        return gcsf::LoadSet(path, reader, out, separators);
    }
};

} // namespace

// XMPlay looks the plugin up by this name (the SDK's xmpin.def exports it).
XMPIN* WINAPI XMPIN_GetInterface(DWORD face, InterfaceProc faceproc)
{
    return xsflib::xmplay::Plugin<Traits>::GetInterface(face, faceproc);
}
