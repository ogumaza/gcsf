// SPDX-License-Identifier: MIT

// Loads a GameCube game for ripping (see game.h). YAGCD ("Yet Another GameCube Documentation") documents the disc's
// layout: the header, the apploader, main.dol and the file system.

#include "disc/game.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "common/bytes.h"
#include "disc/image.h"

namespace gcsf::disc
{
namespace
{

namespace fs = std::filesystem;

constexpr uint32_t kWiiMagic = 0x5D1C9EA3;
constexpr uint32_t kGameCubeMagic = 0xC2339F3D;

// The largest system file or file read into memory.
constexpr uint64_t kMaxFileSize = 1ull << 30;

// The message for a Wii disc.
constexpr char kWiiDisc[] = "it's a Wii disc, and GCSF is for GameCube discs";

bool IsAscii(uint8_t c)
{
    return c < 0x80;
}

// Converts a Shift-JIS name from the disc to UTF-8. Bytes that aren't ASCII or half-width katakana become %XX.
std::string DecodeName(const uint8_t* name, size_t size)
{
    std::string out;
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t c = name[i];
        if (IsAscii(c))
        {
            out += static_cast<char>(c);
        }
        else if (c >= 0xA1 && c <= 0xDF)
        {
            // U+FF61 to U+FF9F, three bytes in UTF-8.
            const uint32_t code = 0xFF61 + (c - 0xA1u);
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            static const char kDigits[] = "0123456789ABCDEF";
            out += '%';
            out += kDigits[c >> 4];
            out += kDigits[c & 15];
        }
    }

    return out;
}

// A file in the disc's file system.
struct FstEntry
{
    uint64_t offset; // on the disc
    uint32_t size;
};

// Parses a GameCube file system table (fst.bin) into its files. The table holds 12-byte entries, then their names. Each
// entry starts with a type byte and a 24-bit name offset. A file's entry then gives the file's offset and its size. A
// directory's entry gives its parent and the index after its last entry.
std::map<std::string, FstEntry> ParseFst(const std::vector<uint8_t>& fst)
{
    if (fst.size() < 12 || fst[0] != 1)
    {
        throw Error("the disc's file system is damaged");
    }

    const uint32_t count = LoadBe32(fst.data() + 8);
    if (!count || count > fst.size() / 12)
    {
        throw Error("the disc's file system is damaged");
    }

    const size_t names = size_t{count} * 12;
    const auto name = [&](uint32_t index)
    {
        const size_t start = names + (LoadBe32(fst.data() + index * 12) & 0xFFFFFF);
        const auto end =
            std::find(fst.begin() + static_cast<std::ptrdiff_t>(std::min(start, fst.size())), fst.end(), 0);
        if (start >= fst.size() || end == fst.end())
        {
            throw Error("the disc's file system is damaged");
        }

        return DecodeName(fst.data() + start, static_cast<size_t>(end - fst.begin()) - start);
    };

    // Each directory ends at the index in its entry. The stack holds the open directories and their paths.
    std::map<std::string, FstEntry> files;
    std::vector<std::pair<uint32_t, std::string>> open = {{count, ""}};
    for (uint32_t i = 1; i < count; ++i)
    {
        while (i >= open.back().first)
        {
            open.pop_back();
        }

        const uint8_t* entry = fst.data() + i * 12;
        if (entry[0])
        {
            const uint32_t end = LoadBe32(entry + 8);
            if (end <= i || end > open.back().first)
            {
                throw Error("the disc's file system is damaged");
            }

            open.push_back({end, open.back().second + name(i) + "/"});
        }
        else
        {
            files[open.back().second + name(i)] = {LoadBe32(entry + 4), LoadBe32(entry + 8)};
        }
    }

    return files;
}

// A GameCube disc image in the form that the game reads.
class GameDisc
{
public:
    explicit GameDisc(std::unique_ptr<Image> image) : image_(std::move(image))
    {
        uint8_t header[0x208];
        image_->Read(0, header, sizeof(header));
        if (LoadBe32(header + 0x1C) != kGameCubeMagic)
        {
            if (LoadBe32(header + 0x18) == kWiiMagic)
            {
                throw Error(kWiiDisc);
            }

            throw Error("it isn't a GameCube disc image");
        }

        nkit_ = std::equal(header + 0x200, header + 0x208, "NKIT v01");
    }

    std::string Source() const
    {
        return (nkit_ ? "NKit " : "") + image_->Format();
    }

    std::vector<uint8_t> Read(uint64_t offset, uint64_t size)
    {
        if (size > kMaxFileSize)
        {
            throw Error("a file on the disc is too large");
        }

        std::vector<uint8_t> data(static_cast<size_t>(size));
        image_->Read(offset, data.data(), data.size());

        // NKit keeps its header at 0x200-0x21F, in the unused end of the disc header's title. The original disc has
        // zeros there.
        if (nkit_ && offset < kNkitHeaderEnd && offset + size > kNkitHeader)
        {
            const uint64_t from = std::max(offset, kNkitHeader);
            const uint64_t to = std::min(offset + size, kNkitHeaderEnd);
            std::fill(data.begin() + static_cast<std::ptrdiff_t>(from - offset),
                      data.begin() + static_cast<std::ptrdiff_t>(to - offset), uint8_t{0});
        }

        return data;
    }

private:
    static constexpr uint64_t kNkitHeader = 0x200;
    static constexpr uint64_t kNkitHeaderEnd = 0x220;

    std::unique_ptr<Image> image_;
    bool nkit_ = false;
};

// The size of a DOL: the end of the section that ends last in the file. The header lists 7 text and 11 data sections as
// offsets, then addresses, then sizes.
uint64_t DolSize(const uint8_t* header)
{
    uint64_t size = 0x100;
    for (int i = 0; i < 18; ++i)
    {
        size = std::max<uint64_t>(size, uint64_t{LoadBe32(header + i * 4)} + LoadBe32(header + 0x90 + i * 4));
    }

    return size;
}

void LoadImage(const fs::path& path, GameFiles& out)
{
    auto disc = std::make_shared<GameDisc>(OpenImage(path));
    out.source = disc->Source();

    // The system files: the header, bi2, the apploader, main.dol and the file system table. The apploader starts with a
    // 0x20-byte header that gives its size and the size of its trailer. The disc header gives the offsets of main.dol
    // and the file system table, and the table's size.
    out.boot = disc->Read(0, 0x440);
    out.bi2 = disc->Read(0x440, 0x2000);
    const std::vector<uint8_t> apploader = disc->Read(0x2440, 0x20);
    out.apploader =
        disc->Read(0x2440, 0x20 + uint64_t{LoadBe32(apploader.data() + 0x14)} + LoadBe32(apploader.data() + 0x18));
    const uint64_t dol_offset = LoadBe32(out.boot.data() + 0x420);
    const std::vector<uint8_t> dol_header = disc->Read(dol_offset, 0x100);
    out.dol = disc->Read(dol_offset, DolSize(dol_header.data()));
    out.fst = disc->Read(LoadBe32(out.boot.data() + 0x424), LoadBe32(out.boot.data() + 0x428));

    auto files = std::make_shared<std::map<std::string, FstEntry>>(ParseFst(out.fst));
    out.files.clear();
    for (const auto& [name, entry] : *files)
    {
        out.files.push_back({name, entry.size});
    }

    out.read_file = [disc, files](const std::string& name, std::vector<uint8_t>& data)
    {
        const auto file = files->find(name);
        if (file == files->end())
        {
            return false;
        }

        data = disc->Read(file->second.offset, file->second.size);

        return true;
    };
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& data)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    file.seekg(0, std::ios::end);
    const auto size = static_cast<uint64_t>(file.tellg());
    if (size > kMaxFileSize)
    {
        return false;
    }

    file.seekg(0);
    data.resize(static_cast<size_t>(size));

    return !size ||
           static_cast<bool>(file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)));
}

void LoadFolder(const fs::path& root, GameFiles& out)
{
    out.source = "extracted disc";
    const fs::path sys = root / "sys";
    if (!ReadHostFile(sys / "boot.bin", out.boot) || out.boot.size() < 0x440 ||
        !ReadHostFile(sys / "main.dol", out.dol) || !ReadHostFile(sys / "fst.bin", out.fst))
    {
        throw Error("the extracted disc must contain sys/boot.bin, sys/main.dol and sys/fst.bin");
    }

    if (LoadBe32(out.boot.data() + 0x1C) != kGameCubeMagic)
    {
        throw Error(LoadBe32(out.boot.data() + 0x18) == kWiiMagic ? kWiiDisc
                                                                  : "sys/boot.bin isn't a GameCube disc's header");
    }

    // The ripper doesn't use these files. A folder may leave them out.
    ReadHostFile(sys / "bi2.bin", out.bi2);
    ReadHostFile(sys / "apploader.img", out.apploader);

    // The files are those in the folder. There may be fewer than the file system table lists.
    const fs::path files = root / "files";
    std::error_code error;
    out.files.clear();
    for (auto it = fs::recursive_directory_iterator(files, error); !error && it != fs::recursive_directory_iterator();
         it.increment(error))
    {
        if (it->is_regular_file(error))
        {
            const std::u8string name = it->path().lexically_relative(files).generic_u8string();
            out.files.push_back({std::string(name.begin(), name.end()), it->file_size(error)});
        }
    }

    std::sort(out.files.begin(), out.files.end(), [](const GameFile& a, const GameFile& b) { return a.path < b.path; });
    out.read_file = [files](const std::string& name, std::vector<uint8_t>& data)
    {
        return ReadHostFile(files / fs::path(std::u8string(name.begin(), name.end())), data);
    };
}

} // namespace

std::string GameFiles::GameId() const
{
    return boot.size() >= 6 ? std::string(boot.begin(), boot.begin() + 6) : std::string();
}

std::string GameFiles::Title() const
{
    if (boot.size() < 0x60)
    {
        return std::string();
    }

    const auto end = std::find(boot.begin() + 0x20, boot.begin() + 0x60, 0);

    return DecodeName(boot.data() + 0x20, static_cast<size_t>(end - (boot.begin() + 0x20)));
}

std::optional<std::string> LoadGame(const std::filesystem::path& path, GameFiles& out)
{
    try
    {
        std::error_code error;
        if (fs::is_directory(path, error))
        {
            const fs::path data = path / "DATA";
            LoadFolder(fs::is_regular_file(path / "sys" / "main.dol", error) ? path : data, out);
        }
        else
        {
            LoadImage(path, out);
        }
    }
    catch (const std::exception& e)
    {
        return e.what();
    }

    return std::nullopt;
}

} // namespace gcsf::disc
