# SPDX-License-Identifier: MIT

# xmp-gcsf: the XMPlay input plugin, a 32-bit DLL built with clang-cl. XMPlay is a 32-bit program. Added by the
# top-level CMakeLists.txt when XMPLAY_SDK_DIR is set. See README.md. xsflib builds it against the SDK's headers
# (xsflib_add_xmplay_plugin).

xsflib_add_xmplay_plugin(xmp-gcsf
    SDK ${XMPLAY_SDK_DIR}
    SOURCES xmp_gcsf.cpp
    LIBRARIES gcsf_player
    OPTIONS ${GCSF_WARNINGS})
xsflib_add_version(xmp-gcsf VERSION ${gcsf_VERSION} DEFINE GCSF_VERSION DESCRIPTION "GCSF plugin for XMPlay"
                   PRODUCT GCSF COPYRIGHT "Copyright (c) 2026 the GCSF authors. MIT licence.")
