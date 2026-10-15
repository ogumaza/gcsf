# SPDX-License-Identifier: MIT

# foo_input_gcsf: the foobar2000 component. It's a DLL built with clang-cl on Windows. On macOS it's a .component
# bundle. The top-level CMakeLists.txt adds it when FOOBAR2000_SDK_DIR is set (see README.md). xsflib's
# xsflib_add_foobar2000_component compiles the parts of the SDK that a component requires.

# On macOS, the SDK's shared part includes Objective-C++ files. CMake enables a language only at file scope.
if (APPLE)
    enable_language(OBJCXX)
endif()

xsflib_add_foobar2000_component(foo_input_gcsf SDK ${FOOBAR2000_SDK_DIR} SOURCES foo_input_gcsf.cpp
                                LIBRARIES gcsf_player VERSION ${gcsf_VERSION} BUNDLE_ID com.foobar2000.foo-input-gcsf
                                OPTIONS ${GCSF_WARNINGS})
xsflib_add_version(foo_input_gcsf VERSION ${gcsf_VERSION} DEFINE GCSF_VERSION DESCRIPTION "GCSF decoder for foobar2000"
                   PRODUCT GCSF COPYRIGHT "Copyright (c) 2026 the GCSF authors. MIT licence.")
