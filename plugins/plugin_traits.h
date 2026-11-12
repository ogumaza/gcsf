// SPDX-License-Identifier: MIT

// The traits that the foobar2000 component and the XMPlay plugin share (xsflib/foobar2000/input.h and
// xsflib/xmplay/input.h): the player, the format's name, the extensions, the limits, the version and the about text.
// The build defines GCSF_VERSION for both plugins.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "xsflib/psf/psf.h"

#include "gcsf/format.h"
#include "gcsf/player.h"

namespace gcsf
{

struct PluginTraits
{
    using Player = gcsf::Player;
    using Set = LoadedSet;

    static constexpr const char* kName = "GCSF";
    static constexpr std::array<const char*, 2> kExtensions = {"minigcsf", "gcsf"};
    static constexpr uint8_t kPsfVersion = kPsfVersionGcsf;
    static constexpr uint64_t kMaxFileSize = 256u << 20; // largest file read into memory

    // An effect that waits for a port that the game sets would go silent if removing the tags dropped gcsf_var.
    static constexpr std::array<const char*, 1> kPlaybackTags = {kVariablesTag};

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

} // namespace gcsf
