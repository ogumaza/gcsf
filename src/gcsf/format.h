// SPDX-License-Identifier: MIT

// The GCSF file format (docs/gcsf.md): PSF container, program chunks (MEM and FILE), the process descriptor, tags.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gcsf
{

// PSF version byte of GCSF. See docs/gcsf.md ("Version byte") before changing it.
constexpr uint8_t kPsfVersionGcsf = 0x2a;

// The rate that players report: the audio interface's DMA rate, 108 MHz / 3,372 = 32,028.47 Hz, rounded down. The
// `length` and `fade` tags count at it (docs/gcsf.md, "Tags").
constexpr int kPlayerSampleRate = 32'028;

// Descriptor flags. Version 1 requires bit 0 (DSP required) and rejects all other bits.
constexpr uint32_t kFlagDspRequired = 1;

// Tags from a PSF file's [TAG] section. Names are lower-cased. Multi-line values are joined with '\n'.
using Tags = std::map<std::string, std::string>;

// One parsed PSF file.
struct PsfFile
{
    uint8_t version = 0;
    std::vector<uint8_t> reserved;
    std::vector<uint8_t> program; // decompressed
    Tags tags;
};

// Parses a PSF file. Returns an error message on failure. Without `with_program`, the program isn't decompressed or
// checked. That's enough to read the version, the reserved area and the tags.
std::optional<std::string> ParsePsf(const std::vector<uint8_t>& data, PsfFile& out, bool with_program = true);

// Offset of the tag area (the end of the program) of a PSF file, or nullopt if the header is invalid.
std::optional<std::size_t> TagAreaOffset(const std::vector<uint8_t>& data);

// The tag area for `tags`: "[TAG]", then name=value lines. A multi-line value becomes one line for each of its lines,
// all with the same name. Empty when there are no tags.
std::string FormatTagArea(const Tags& tags);

// Writes a PSF file. The program is compressed by zlib at its highest level. Throws std::runtime_error if zlib fails.
// Only running out of memory can make zlib fail.
std::vector<uint8_t> WritePsf(uint8_t version, const std::vector<uint8_t>& reserved,
                              const std::vector<uint8_t>& program, const Tags& tags);

// A number read from the start of a tag's value, and the number of characters it took.
struct DecimalNumber
{
    double value;
    std::size_t length;
};

// Reads a decimal number from the start of `text` the same way in every locale: an optional sign, digits with an
// optional '.' or ',' before decimals, and an optional exponent ("1.5e3"). Returns nullopt if `text` doesn't start with
// one. Unlike std::strtod, it reads no white space, hex, infinity or NaN.
std::optional<DecimalNumber> ReadDecimal(std::string_view text);

// Parses a PSF length or fade tag ("seconds", "m:s" or "h:m:s", with '.' or ',' before decimals) into milliseconds.
// Returns -1 if the tag is empty or malformed.
long long ParseTime(const std::string& text);

// Formats milliseconds as a PSF time ("m:ss.fff").
std::string FormatTime(long long ms);

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

// A MEM chunk: data to load at `address`. `address` is a cached address in the game's view of main memory:
// 0x80000000-0x817FFFFF.
struct MemChunk
{
    uint32_t address;
    std::vector<uint8_t> data;
};

// A FILE chunk: a file from the game's disc, named by its path on the disc.
struct FileChunk
{
    std::string path;
    std::vector<uint8_t> data;
};

// The parameter block of GCSF's game-mode driver (docs/gcsf.md, "Driver"). A game-mode set loads it at kAddress. Like
// the rest of the GameCube's memory, its words are big-endian.
struct DriverBlock
{
    static constexpr uint32_t kAddress = 0x81700010;
    static constexpr uint32_t kMagic = 0x47435344; // "GCSD"
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kVersionOffset = 0x04;
    static constexpr uint32_t kStatusOffset = 0x08;
    static constexpr uint32_t kSoundOffset = 0x0c;
};

// Builds a program (docs/gcsf.md, "Program: chunks"): the chunks in the order they're added. Each is padded to a
// multiple of four bytes.
class ProgramBuilder
{
public:
    void AddMemory(uint32_t address, const void* data, std::size_t size);

    void AddMemory(uint32_t address, const std::vector<uint8_t>& data)
    {
        AddMemory(address, data.data(), data.size());
    }

    void AddFile(const std::string& path, const std::vector<uint8_t>& data);

    const std::vector<uint8_t>& Data() const
    {
        return program_;
    }

private:
    void Chunk(const char type[4], const std::vector<uint8_t>& payload);

    std::vector<uint8_t> program_;
};

// A decompressed program split into chunks.
struct ProgramChunks
{
    std::vector<MemChunk> memory;
    std::vector<FileChunk> files;
};

// Splits a decompressed program into chunks (unknown types are skipped).
std::optional<std::string> ParseProgram(const std::vector<uint8_t>& program, ProgramChunks& out);

// A complete set: the chunks of a file and its libraries in loading order, the effective descriptor and the tags of the
// top-level file.
struct LoadedSet
{
    std::vector<MemChunk> memory;
    std::vector<FileChunk> files;
    std::optional<ProcessDescriptor> descriptor;
    Tags tags;
    std::vector<std::string> sources; // files loaded, in order (for diagnostics)
};

// Reads a file by path. Returns false if the file can't be read.
using FileReader = std::function<bool(const std::string& path, std::vector<uint8_t>& data)>;

// Loads a GCSF/miniGCSF file with its _lib/_libN chain. Paths in _lib tags are relative to the directory of the file
// that names them: that file's path up to the last of `separators`. Returns an error for a set that a version 1 player
// can't play. That's a set whose descriptor fails its Check(), or a game-mode set whose driver block has another
// version.
std::optional<std::string> LoadSet(const std::string& path, const FileReader& reader, LoadedSet& out,
                                   std::string_view separators = "/\\");

// Reads a whole file from disk.
bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& data);

} // namespace gcsf
