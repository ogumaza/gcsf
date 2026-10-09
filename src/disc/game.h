// SPDX-License-Identifier: MIT

// A GameCube game's files for ripping: the system files and the file system of its disc. They come from a disc image in
// any format that OpenImage reads, or from an extracted disc.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gcsf::disc
{

// A file in the disc's file system.
struct GameFile
{
    std::string path; // without a leading '/' ("AudioRes/JaiInit.baa")
    uint64_t size;
};

struct GameFiles
{
    // The game's six-character ID ("G4SE01") and title, from the disc's header.
    std::string GameId() const;
    std::string Title() const;

    // The source of the files, for messages: the image format ("NKit ISO", "RVZ", ...) or "extracted disc".
    std::string source;

    std::vector<uint8_t> boot;      // sys/boot.bin: the disc's 0x440-byte header
    std::vector<uint8_t> bi2;       // sys/bi2.bin
    std::vector<uint8_t> apploader; // sys/apploader.img
    std::vector<uint8_t> dol;       // sys/main.dol
    std::vector<uint8_t> fst;       // sys/fst.bin

    // Every file in the disc's file system, sorted by path. Names on the disc are Shift-JIS. ASCII and half-width
    // katakana are converted to UTF-8. Any other byte above 0x7F becomes %XX.
    std::vector<GameFile> files;

    // Reads a file by its path in `files`. The read shares the image's caches. Only one thread may call it at a time.
    std::function<bool(const std::string& path, std::vector<uint8_t>& data)> read_file;
};

// Loads a disc image or an extracted disc. An extracted disc is a folder with sys/main.dol and files/, either directly
// or in a DATA folder. Dolphin and Wiimm's tools extract discs in these layouts. Returns an error message on failure
// and nothing on success.
std::optional<std::string> LoadGame(const std::filesystem::path& path, GameFiles& out);

} // namespace gcsf::disc
