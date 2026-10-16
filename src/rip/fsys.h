// SPDX-License-Identifier: MIT

// FSYS archives and their LZSS compression. Pokémon Colosseum keeps most of its files in FSYS archives, its songs and
// MusyX's projects, pools and sample directories among them.
//
// An archive starts with "FSYS". Its words are big-endian. The word at 0xC gives the number of files, and the word at
// 0x18 gives the offset of the archive's information block. The block's first word gives the offset of a table of the
// entries' offsets. An entry gives the file's ID and type in its first word, the file's offset at +4, its size at +8,
// its flags at +0xC (0x80000000 for LZSS), the size that the archive stores at +0x14 and the offset of its name at
// +0x24. A name ends with a zero byte.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gcsf::rip
{

// A file of an FSYS archive: the first word of its entry (the file's ID and type), its name, and its data.
struct FsysFile
{
    uint32_t id = 0;
    std::string name;
    std::vector<uint8_t> data;
};

// Decompresses LZSS data: a header of four big-endian words ("LZSS", the size of the data, the size of the compressed
// data with the header, and 0), then the compressed stream. Throws xsflib::FormatError if the data is damaged.
std::vector<uint8_t> DecompressLzss(std::span<const uint8_t> data);

// The files of an FSYS archive in the order of its table, decompressed. Throws xsflib::FormatError if the archive is
// damaged.
std::vector<FsysFile> ReadFsys(std::span<const uint8_t> archive);

} // namespace gcsf::rip
