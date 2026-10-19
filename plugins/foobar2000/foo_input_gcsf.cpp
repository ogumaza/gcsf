// SPDX-License-Identifier: MIT

// foo_input_gcsf: foobar2000 input component for GCSF (.minigcsf and .gcsf), from xsflib's input template
// (xsflib/foobar2000/input.h). The template's comment says what the component does. This file gives GCSF's traits: its
// player, names, extensions, GUIDs and limits.
//
// The player core (src/gcsf) runs the game's sound code on an emulated GameCube, and the game's DSP program on an
// emulated DSP.
//
// Build: see plugins/foobar2000/README.md (clang-cl on Windows, or Apple Clang on macOS, with the foobar2000 SDK).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "xsflib/foobar2000/input.h"
#include "xsflib/psf/psf.h"
#include <SDK/foobar2000.h>

#include "gcsf/format.h"
#include "gcsf/player.h"

namespace
{

struct Traits
{
    using Player = gcsf::Player;
    using Set = gcsf::LoadedSet;

    static std::optional<std::string> LoadSet(const std::string& path, const xsflib::psf::FileReader& reader, Set& out,
                                              std::string_view separators)
    {
        return gcsf::LoadSet(path, reader, out, separators);
    }

    static constexpr const char* kName = "GCSF";
    static constexpr const char* kDecoderName = "GCSF decoder";
    static constexpr std::array<const char*, 2> kExtensions = {"minigcsf", "gcsf"};
    static constexpr uint8_t kPsfVersion = gcsf::kPsfVersionGcsf;
    static constexpr t_filesize kMaxFileSize = 256u << 20; // largest file read into memory

    // No GCSF tag sets how a file plays.
    static constexpr std::array<const char*, 0> kPlaybackTags = {};

    // {A028B073-EC7A-4407-8A1E-E022D8D56419}
    static constexpr GUID kInputGuid = {0xa028b073, 0xec7a, 0x4407, {0x8a, 0x1e, 0xe0, 0x22, 0xd8, 0xd5, 0x64, 0x19}};
    // Advanced preferences entries, under Decoding > GCSF decoder.
    static constexpr GUID kBranchGuid = {0x61415291, 0x2158, 0x4027, {0xbd, 0x3e, 0x6e, 0x94, 0x3c, 0x0d, 0x94, 0xb9}};
    static constexpr GUID kLengthGuid = {0xbcce6ad2, 0xaddf, 0x4298, {0x97, 0x6f, 0xbb, 0xf2, 0x68, 0x6a, 0x1d, 0xf4}};
    static constexpr GUID kFadeGuid = {0x81682426, 0x695f, 0x4d82, {0x8d, 0xb9, 0xa7, 0x7c, 0x6c, 0x1f, 0xd8, 0x5e}};
    static constexpr GUID kEndlessGuid = {0xc40506c6, 0xc1b5, 0x4fdb, {0x8a, 0x88, 0x50, 0x08, 0xb2, 0x75, 0x28, 0x12}};
    static constexpr GUID kRenderAheadGuid = {
        0xae2ef02a, 0xa4af, 0x4cbd, {0xa9, 0xb4, 0xc0, 0xd7, 0x87, 0x10, 0x05, 0x12}};
};

xsflib::foobar2000::Settings<Traits> g_settings;
input_singletrack_factory_t<xsflib::foobar2000::Input<Traits>> g_input_gcsf_factory;

} // namespace

DECLARE_FILE_TYPE("GCSF files", "*.MINIGCSF;*.GCSF");

DECLARE_COMPONENT_VERSION("GCSF Decoder", GCSF_VERSION,
                          "Plays GCSF (Nintendo GameCube sequenced music) rips. The game's sound code runs on an "
                          "emulated GameCube, and the game's DSP program runs on an emulated DSP.\n\n"
                          "MIT licence. Uses zlib.");

VALIDATE_COMPONENT_FILENAME("foo_input_gcsf.dll");
