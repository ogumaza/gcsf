// SPDX-License-Identifier: MIT

// The sound files that a rip of a JAudio game reads in place of the disc's: the start-up archive unpacked from its
// packed archive (UnpackSoundArchive), and another release's sound files from a folder (UseSoundFiles).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/jaudio/rarc.h"
#include "ntdlib/yaz0.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/files.h"
#include "xsflib/text.h"

#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// Reads a file of the host's file system. A file larger than 1 GiB, the largest that GCSF reads into memory, can't be
// read.
bool ReadHostFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && xsflib::ReadFile(path, data, std::size_t{1} << 30);
}

// Reads another release's sequence table as the sequence archive at `path`. `read_disc` reads the disc's archive. Each
// file's data then comes from the table (see jaudio::RepackSequenceTable). `folder` must hold one table, a `.bdt` file.
// The Wind Waker HD keeps its sequences that way. AppleDouble files don't count. macOS writes one, named "._" and the
// file's name, beside each file that it copies to a FAT or exFAT drive.
bool ReadSequenceTable(const std::filesystem::path& folder,
                       const std::function<bool(const std::string&, std::vector<uint8_t>&)>& read_disc,
                       const std::string& path, std::vector<uint8_t>& data)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> tables;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code type_error;
        if (it->path().extension() == ".bdt" && !xsflib::Utf8(it->path().filename()).starts_with("._") &&
            it->is_regular_file(type_error))
        {
            tables.push_back(it->path());
        }
    }

    std::vector<uint8_t> table;
    std::vector<uint8_t> archive;
    if (ec || tables.size() != 1 || !ReadHostFile(tables.front(), table) || !read_disc || !read_disc(path, archive))
    {
        return false;
    }

    try
    {
        data = jaudio::RepackSequenceTable(archive, table);
    }
    catch (const xsflib::FormatError&)
    {
        return false;
    }

    return true;
}

// The folders in `folder` and below that hold a file named `file`, sorted. Sets `error` if the search fails.
std::vector<std::filesystem::path> FoldersHolding(const std::filesystem::path& folder,
                                                  const std::filesystem::path& file, std::error_code& error)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> found;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end;
         !error && it != end; it.increment(error))
    {
        std::error_code type_error;
        if (it->path().filename() == file && it->is_regular_file(type_error))
        {
            found.push_back(it->path().parent_path());
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}

} // namespace

void UnpackSoundArchive(disc::GameFiles& game, const GameProfile& profile)
{
    if (!profile.packed_archive)
    {
        return;
    }

    const auto from_game = game.read_file;
    const std::string sound_archive = profile.sound_archive;
    const std::string packed_archive = profile.packed_archive;
    const std::string name = xsflib::AsciiLower(profile.packed_sound_archive);
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        if (path != sound_archive)
        {
            return from_game && from_game(path, data);
        }

        std::vector<uint8_t> packed;
        if (!from_game || !from_game(packed_archive, packed))
        {
            return false;
        }

        try
        {
            std::vector<uint8_t> decoded;
            for (const jaudio::ArchiveFile& file : jaudio::ReadRarc(ntdlib::DecodeIfYaz0(packed, decoded)))
            {
                if (xsflib::AsciiLower(file.name) == name)
                {
                    data.assign(file.data.begin(), file.data.end());
                    return true;
                }
            }
        }
        catch (const xsflib::FormatError&)
        {
        }

        return false;
    };
}

std::optional<std::filesystem::path> UseSoundFiles(disc::GameFiles& game, const GameProfile& profile,
                                                   const std::filesystem::path& folder, std::string* error)
{
    namespace fs = std::filesystem;
    const auto fail = [error](const std::string& message) -> std::optional<fs::path>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    // The start-up archive's folder on the disc ("Audiores/"), and its file name.
    const std::string sound_archive = profile.sound_archive;
    const std::size_t slash = sound_archive.rfind('/');
    const std::string sound_folder = slash == std::string::npos ? "" : sound_archive.substr(0, slash + 1);
    const std::string file_name = sound_archive.substr(sound_folder.size());
    std::error_code ec;
    const std::vector<fs::path> found = FoldersHolding(folder, xsflib::PathFromUtf8(file_name), ec);
    if (ec)
    {
        return fail(xsflib::Utf8(folder) + ": " + ec.message());
    }

    if (found.empty())
    {
        return fail(xsflib::Utf8(folder) + " holds no " + file_name);
    }

    if (found.size() > 1)
    {
        std::string folders;
        for (const fs::path& path : found)
        {
            folders += (folders.empty() ? "" : ", ") + xsflib::Utf8(path);
        }

        return fail(xsflib::Utf8(folder) + " holds more than one " + file_name + " (in " + folders +
                    "). Give the folder of the one to use.");
    }

    // The sound files come from the folder, and the rest from the game. A sound file that the folder lacks belongs to
    // the other release, and isn't read from the game. A sequence table in place of the sequence archive is read into
    // the game's archive.
    const fs::path root = found.front();
    const auto from_game = game.read_file;
    const std::string sequence_archive = profile.sequence_archive ? profile.sequence_archive : "";
    const std::string wave_folder = profile.wave_folder;
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        const bool sound_file = path == sound_archive || (!sequence_archive.empty() && path == sequence_archive) ||
                                path.starts_with(wave_folder);
        if (!sound_file || !path.starts_with(sound_folder))
        {
            return from_game && from_game(path, data);
        }

        const fs::path host = root / xsflib::PathFromUtf8(path.substr(sound_folder.size()));
        if (ReadHostFile(host, data))
        {
            return true;
        }

        return path == sequence_archive && ReadSequenceTable(host.parent_path(), from_game, path, data);
    };

    return root;
}

} // namespace gcsf::rip
