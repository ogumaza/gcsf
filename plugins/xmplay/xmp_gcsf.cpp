// SPDX-License-Identifier: MIT

// xmp-gcsf: XMPlay input plugin for GCSF (.minigcsf and .gcsf), from xsflib's plugin template (xsflib/xmplay/input.h).
// The template's comment says what the plugin does. This file passes GCSF's traits to the template: its player, names,
// extensions and limits. plugins/plugin_traits.h contains them. The foobar2000 component uses them too.
//
// The player core (src/gcsf) runs the game's sound code on an emulated GameCube, and the game's DSP program on an
// emulated DSP.
//
// Build: see plugins/xmplay/README.md (clang-cl for 32-bit Windows, with the XMPlay plugin SDK).

#include <windows.h>
#include <xmpin.h>

#include "xsflib/xmplay/input.h"

#include "plugin_traits.h"

// XMPlay looks the plugin up by this name (the SDK's xmpin.def exports it).
XMPIN* WINAPI XMPIN_GetInterface(DWORD face, InterfaceProc faceproc)
{
    return xsflib::xmplay::Plugin<gcsf::PluginTraits>::GetInterface(face, faceproc);
}
