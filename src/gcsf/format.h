// SPDX-License-Identifier: MIT

// The GCSF file format (docs/gcsf.md): PSF container, program chunks (MEM and FILE), the process descriptor, tags.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/set.h"

namespace gcsf
{

// PSF version byte of GCSF. See docs/gcsf.md ("Version byte") before changing it.
constexpr uint8_t kPsfVersionGcsf = 0x2a;

// The rate that players report: the audio interface's DMA rate, 108 MHz / 3,372 = 32,028.47 Hz, rounded down. The
// `length` and `fade` tags count at it (docs/gcsf.md, "Tags").
constexpr int kPlayerSampleRate = 32'028;

// Descriptor flags. Version 1 requires bit 0 (DSP required) and rejects all other bits.
constexpr uint32_t kFlagDspRequired = 1;

// The PSF container's parts that GCSF's code uses. xsflib::psf holds them.
using xsflib::psf::FileReader;
using xsflib::psf::ParsePsf;
using xsflib::psf::PsfFile;
using xsflib::psf::Tags;
using xsflib::psf::WritePsf;

// The process descriptor: the reserved area of a game-mode set (docs/gcsf.md, "Process descriptor").
struct ProcessDescriptor
{
    static constexpr uint32_t kMagic = 0x50534347; // "GCSP"
    static constexpr uint32_t kVersion = 1;

    static std::optional<ProcessDescriptor> Parse(const std::vector<uint8_t>& data);

    // Returns an error message for an unsupported descriptor, or no value if it is supported.
    std::optional<std::string> Check() const;

    std::vector<uint8_t> Serialize() const;

    uint32_t version = 1;
    uint32_t entry = 0x80004000; // main.dol's entry point
    uint32_t status_address = 0; // the driver's status word, or 0
    uint32_t flags = kFlagDspRequired;
};

// The program's chunks (docs/gcsf.md, "Program: chunks"), and the program builder and parser. xsflib::psf holds them.
// A MEM chunk's address is a cached address in the game's view of main memory: 0x80000000-0x817FFFFF. A FILE chunk
// holds a file from the game's disc, named by its path on the disc.
using xsflib::psf::FileChunk;
using xsflib::psf::MemChunk;
using xsflib::psf::ParseProgram;
using xsflib::psf::ProgramBuilder;
using xsflib::psf::ProgramChunks;

// The path of the FILE chunk that holds the disc's 0x440-byte header. The emulated disc starts with it (docs/gcsf.md,
// "The emulated system").
constexpr char kDiscHeaderPath[] = "sys/boot.bin";

// The parameter block of GCSF's game-mode driver (docs/gcsf.md, "Driver"). A game-mode set loads it at kAddress. Like
// the rest of the GameCube's memory, its words are big-endian. The game writes the sound's handle at kHandleOffset. The
// game profile follows the handle. A driver with a wave list holds the list there instead: the number of waves, then
// kMaxWaves entries. Its game profile follows the list.
struct DriverBlock
{
    static constexpr uint32_t kAddress = 0x81700010;
    static constexpr uint32_t kMagic = 0x47435344; // "GCSD"
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kVersionOffset = 0x04;
    static constexpr uint32_t kStatusOffset = 0x08;
    static constexpr uint32_t kSoundOffset = 0x0c;
    static constexpr uint32_t kHandleOffset = 0x10;
    static constexpr uint32_t kProfileOffset = 0x14;
    static constexpr uint32_t kWaveListOffset = 0x14;
    static constexpr uint32_t kMaxWaves = 64;
    static constexpr uint32_t kListedProfileOffset = kWaveListOffset + 4 + 4 * kMaxWaves;
};

// What xsflib::psf's set loader requires of GCSF: its descriptor, version byte, name and driver block.
struct SetFormat
{
    using ProcessDescriptor = gcsf::ProcessDescriptor;
    using ArchiveDescriptor = void; // GCSF has no archive mode
    static constexpr uint8_t kPsfVersion = kPsfVersionGcsf;
    static constexpr const char* kName = "GCSF";
    static constexpr xsflib::psf::DriverBlockLayout kDriverBlock{.address = DriverBlock::kAddress,
                                                                 .magic = DriverBlock::kMagic,
                                                                 .version = DriverBlock::kVersion,
                                                                 .version_offset = DriverBlock::kVersionOffset,
                                                                 .big_endian = true};
};

// A complete set: the chunks of a file and its libraries in loading order, the effective descriptor and the tags of the
// top-level file. LoadSet loads a GCSF or miniGCSF file with its _lib and _libN chain. Paths in _lib tags are relative
// to the directory of the file that names them: that file's path up to the last of `separators`. LoadSet returns an
// error for a set that a version 1 player can't play. That's a set without a process descriptor, a set whose
// descriptor fails its Check(), or a game-mode set whose driver block has another version.
using LoadedSet = xsflib::psf::LoadedSet<SetFormat>;
using xsflib::psf::LoadSet;

} // namespace gcsf
