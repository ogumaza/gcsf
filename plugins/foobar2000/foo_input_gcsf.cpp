// SPDX-License-Identifier: MIT

// foo_input_gcsf: foobar2000 input component for GCSF (.minigcsf and .gcsf), from xsflib's input template
// (xsflib/foobar2000/input.h). The template's comment says what the component does. This file gives the GUIDs and the
// decoder's name. plugins/plugin_traits.h contains the traits that the component shares with the XMPlay plugin.
//
// The player core (src/gcsf) runs the game's sound code on an emulated GameCube, and the game's DSP program on an
// emulated DSP.
//
// Build: see plugins/foobar2000/README.md (clang-cl on Windows, or Apple Clang on macOS, with the foobar2000 SDK).

#include "xsflib/foobar2000/input.h"
#include <SDK/foobar2000.h>

#include "plugin_traits.h"

namespace
{

struct Traits : gcsf::PluginTraits
{
    static constexpr const char* kDecoderName = "GCSF decoder";

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

DECLARE_COMPONENT_VERSION("GCSF Decoder", Traits::kVersion, Traits::kAbout);

VALIDATE_COMPONENT_FILENAME("foo_input_gcsf.dll");
