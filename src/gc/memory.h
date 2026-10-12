// SPDX-License-Identifier: MIT

// The GameCube's main memory and ARAM, from ntdlib's DSP module.

#pragma once

#include "ntdlib/dsp/memory.h"

namespace gcsf
{

namespace dsp = ntdlib::dsp;

// Main memory (MEM1) and ARAM, and the conversion from the CPU's addresses to physical ones.
using MainMemory = ntdlib::dsp::GameCubeMemory;
using ntdlib::dsp::ToPhysical;

} // namespace gcsf
