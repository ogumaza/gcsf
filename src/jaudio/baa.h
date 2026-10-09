// SPDX-License-Identifier: MIT

// JAudio2's start-up archive, the BAA ("AA_<" ... ">_AA"): a list of commands that each name a block of the archive.
// The ripper reads three kinds of block. The sound table ("bst ") lists the sounds by section, group and index. The
// name table ("bstn") names them. Each wave system ("ws  ") names the wave archives (.aw) that hold its samples.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gcsf::jaudio
{

// A damaged or unsupported archive.
class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// A sound in the sound table. Its ID is what the game passes to the sound library: the section in the top byte, the
// group in the next, and the index in the group in the low 16 bits.
struct Sound
{
    uint32_t id;
    uint8_t type;      // the table's type byte for the sound
    uint8_t volume;    // the sound's volume, over 127
    std::string label; // from the name table, or empty
    std::string section;
    std::string group;
};

// The archive's sounds in the table's order. Throws Error if the archive or its sound table is damaged, or if it has
// none.
std::vector<Sound> ReadSounds(const std::vector<uint8_t>& baa);

// The file names of the wave archives that the archive's wave systems name, in the order the systems give them. Throws
// Error if a wave system is damaged.
std::vector<std::string> WaveArchiveNames(const std::vector<uint8_t>& baa);

} // namespace gcsf::jaudio
