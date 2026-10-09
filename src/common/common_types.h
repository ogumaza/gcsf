// SPDX-License-Identifier: MIT

// Address types for the emulated GameCube.

#pragma once

#include <cstdint>

namespace gcsf
{

// Physical address: main memory is at 0, and the hardware registers at 0x0C000000.
using PAddr = uint32_t;

} // namespace gcsf
