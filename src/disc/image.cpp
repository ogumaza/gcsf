// SPDX-License-Identifier: MIT

// The image formats. Dolphin's docs/WiaAndRvz.md describes WIA and RVZ. The pages on WDF and CISO at
// https://wit.wiimm.de describe those two formats. GCZ is Dolphin's format of zlib-compressed blocks.

#include "disc/image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "common/bytes.h"
#include "common/sha1.h"
#include "disc/compression.h"
#include "disc/junk.h"

namespace gcsf::disc
{
namespace
{

namespace fs = std::filesystem;

constexpr uint64_t kSector = 0x8000; // the unit in which WIA and RVZ store a disc

// The largest block that any of the formats may use. The limit stops a damaged header from requesting absurd amounts of
// memory.
constexpr uint64_t kMaxBlockSize = 1ull << 30;

// The files of a possibly split image: the first piece, then any continuation files.
std::vector<fs::path> Pieces(const fs::path& path)
{
    std::vector<fs::path> pieces = {path};
    std::error_code error;

    // Wiimm's tools number the continuations with one, two or three digits at least.
    for (int digits = 1; digits <= 3; ++digits)
    {
        const auto continuation = [&](int index)
        {
            std::string number = std::to_string(index);
            number.insert(0, std::max<size_t>(number.size(), static_cast<size_t>(digits)) - number.size(), '0');
            fs::path piece = path;
            piece += "." + number;

            return piece;
        };

        for (int i = 1; fs::is_regular_file(continuation(i), error); ++i)
        {
            pieces.push_back(continuation(i));
        }
        if (pieces.size() > 1)
        {
            break;
        }
    }

    return pieces;
}

// Random access to an image file, joined with the continuation files of a split image.
class Source
{
public:
    explicit Source(const fs::path& path)
    {
        for (const fs::path& piece_path : Pieces(path))
        {
            Piece piece;
            piece.file.open(piece_path, std::ios::binary);
            if (!piece.file)
            {
                throw Error("can't open " + piece_path.filename().string());
            }

            piece.file.seekg(0, std::ios::end);
            piece.start = size_;
            piece.size = static_cast<uint64_t>(piece.file.tellg());
            size_ += piece.size;
            pieces_.push_back(std::move(piece));
        }
    }

    uint64_t Size() const
    {
        return size_;
    }

    // Reads exactly `size` bytes. Reading past the end is an error because the formats give the positions of their
    // data.
    void Read(uint64_t offset, uint8_t* out, size_t size)
    {
        if (offset > size_ || size > size_ - offset)
        {
            throw Error("the image is truncated");
        }

        for (Piece& piece : pieces_)
        {
            if (!size || offset >= piece.start + piece.size)
            {
                continue;
            }

            const uint64_t within = offset - piece.start;
            const size_t n = static_cast<size_t>(std::min<uint64_t>(size, piece.size - within));
            piece.file.clear();
            piece.file.seekg(static_cast<std::streamoff>(within));
            if (!piece.file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n)))
            {
                throw Error("reading the image failed");
            }

            out += n;
            offset += n;
            size -= n;
        }
    }

    std::vector<uint8_t> Read(uint64_t offset, size_t size)
    {
        std::vector<uint8_t> data(size);
        Read(offset, data.data(), size);

        return data;
    }

private:
    struct Piece
    {
        std::ifstream file;
        uint64_t start = 0;
        uint64_t size = 0;
    };

    std::vector<Piece> pieces_;
    uint64_t size_ = 0;
};

// An ISO image, including NKit's: the disc byte for byte. NKit and scrubbed images may end early. The missing part
// reads as zeros.
class PlainImage : public Image
{
public:
    explicit PlainImage(std::unique_ptr<Source> source) : source_(std::move(source))
    {
    }

    std::string Format() const override
    {
        return "ISO";
    }

    void Read(uint64_t offset, uint8_t* out, size_t size) override
    {
        const uint64_t end = source_->Size();
        const size_t stored = offset < end ? static_cast<size_t>(std::min<uint64_t>(size, end - offset)) : 0;
        if (stored)
        {
            source_->Read(offset, out, stored);
        }

        std::fill(out + stored, out + size, uint8_t{0});
    }

private:
    std::unique_ptr<Source> source_;
};

// An image stored in blocks of one size. FetchBlock fetches each block. The last block read is cached.
class BlockImage : public Image
{
public:
    void Read(uint64_t offset, uint8_t* out, size_t size) override
    {
        while (size)
        {
            const uint64_t index = offset / block_size_;
            const size_t within = static_cast<size_t>(offset % block_size_);
            const size_t n = static_cast<size_t>(std::min<uint64_t>(size, block_size_ - within));
            if (index != cached_index_)
            {
                cached_index_ = std::numeric_limits<uint64_t>::max();
                cached_present_ = FetchBlock(index, cached_);
                cached_index_ = index;
            }

            if (cached_present_)
            {
                std::copy_n(cached_.begin() + static_cast<std::ptrdiff_t>(within), n, out);
            }
            else
            {
                std::fill_n(out, n, uint8_t{0});
            }

            out += n;
            offset += n;
            size -= n;
        }
    }

protected:
    // Fills `out` with block `index` and resizes it to the block size. Returns false if the block is all zeros.
    virtual bool FetchBlock(uint64_t index, std::vector<uint8_t>& out) = 0;

    uint64_t block_size_ = 0;

private:
    uint64_t cached_index_ = std::numeric_limits<uint64_t>::max();
    bool cached_present_ = false;
    std::vector<uint8_t> cached_;
};

// Dolphin's GCZ: a little-endian header, a table of block offsets and a table of their Adler-32 checksums, then the
// zlib-compressed blocks. Bit 63 of an offset marks a block stored uncompressed.
class GczImage : public BlockImage
{
public:
    explicit GczImage(std::unique_ptr<Source> source) : source_(std::move(source))
    {
        uint8_t header[32];
        source_->Read(0, header, sizeof(header));
        compressed_size_ = LoadLe64(header + 8);
        const uint64_t data_size = LoadLe64(header + 16);
        block_size_ = LoadLe32(header + 24);
        const uint32_t count = LoadLe32(header + 28);
        if (!block_size_ || block_size_ > kMaxBlockSize || count != (data_size + block_size_ - 1) / block_size_ ||
            32 + uint64_t{count} * 12 > source_->Size())
        {
            throw Error("the GCZ header is invalid");
        }

        const std::vector<uint8_t> tables = source_->Read(32, size_t{count} * 12);
        for (uint32_t i = 0; i < count; ++i)
        {
            pointers_.push_back(LoadLe64(tables.data() + i * 8));
            checksums_.push_back(LoadLe32(tables.data() + uint64_t{count} * 8 + i * 4));
        }

        data_start_ = 32 + uint64_t{count} * 12;
    }

    std::string Format() const override
    {
        return "GCZ";
    }

protected:
    bool FetchBlock(uint64_t index, std::vector<uint8_t>& out) override
    {
        if (index >= pointers_.size())
        {
            return false;
        }

        const uint64_t kRaw = 1ull << 63;
        const uint64_t start = pointers_[index] & ~kRaw;
        const uint64_t end = index + 1 < pointers_.size() ? pointers_[index + 1] & ~kRaw : compressed_size_;
        if (end < start || end - start > kMaxBlockSize)
        {
            throw Error("the GCZ block table is invalid");
        }

        const std::vector<uint8_t> stored = source_->Read(data_start_ + start, static_cast<size_t>(end - start));
        if (Adler32(stored) != checksums_[index])
        {
            throw Error("GCZ block " + std::to_string(index) + " doesn't match its checksum");
        }

        out.assign(static_cast<size_t>(block_size_), 0);
        if (pointers_[index] & kRaw)
        {
            std::copy_n(stored.begin(), std::min(stored.size(), out.size()), out.begin());
        }
        else
        {
            Inflate(stored, out);
        }

        return true;
    }

private:
    std::unique_ptr<Source> source_;
    std::vector<uint64_t> pointers_;
    std::vector<uint32_t> checksums_;
    uint64_t data_start_ = 0;
    uint64_t compressed_size_ = 0;
};

// CISO, also called WBI: "CISO", a little-endian block size and a byte for each block, then the stored blocks in order
// from offset 0x8000. A block's byte is 1 if the file stores the block.
class CisoImage : public BlockImage
{
public:
    explicit CisoImage(std::unique_ptr<Source> source) : source_(std::move(source))
    {
        const std::vector<uint8_t> header = source_->Read(0, kHeaderSize);
        block_size_ = LoadLe32(header.data() + 4);
        if (!block_size_ || block_size_ > kMaxBlockSize)
        {
            throw Error("the CISO header is invalid");
        }

        int64_t stored = 0;
        for (size_t i = 8; i < kHeaderSize; ++i)
        {
            slots_.push_back(header[i] ? stored++ : -1);
        }
    }

    std::string Format() const override
    {
        return "CISO";
    }

protected:
    bool FetchBlock(uint64_t index, std::vector<uint8_t>& out) override
    {
        if (index >= slots_.size() || slots_[index] < 0)
        {
            return false;
        }

        out = source_->Read(kHeaderSize + static_cast<uint64_t>(slots_[index]) * block_size_,
                            static_cast<size_t>(block_size_));

        return true;
    }

private:
    static constexpr size_t kHeaderSize = 0x8000;

    std::unique_ptr<Source> source_;
    std::vector<int64_t> slots_; // each block's index among the stored blocks, or -1
};

// Wiimm's WDF, versions 1 and 2: a big-endian header and a table of extents. Each extent gives a disc offset, a file
// offset and a length. The rest of the disc is zeros.
class WdfImage : public Image
{
public:
    explicit WdfImage(std::unique_ptr<Source> source) : source_(std::move(source))
    {
        uint8_t header[0x38];
        source_->Read(0, header, sizeof(header));
        const uint32_t version = LoadBe32(header + 8);
        if (version != 1 && version != 2)
        {
            throw Error("WDF version " + std::to_string(version) + " isn't supported");
        }

        // The table follows an 8-byte magic. In version 1, each entry starts with an unused split-file index.
        const uint32_t count = LoadBe32(header + 0x2C);
        const uint64_t table = LoadBe64(header + 0x30) + 8;
        const size_t entry = version == 1 ? 28 : 24;
        if (table > source_->Size() || count > (source_->Size() - table) / entry)
        {
            throw Error("the WDF chunk table is invalid");
        }

        const std::vector<uint8_t> raw = source_->Read(table, size_t{count} * entry);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* p = raw.data() + i * entry + (version == 1 ? 4 : 0);
            extents_.push_back({LoadBe64(p), LoadBe64(p + 8), LoadBe64(p + 16)});
        }

        std::sort(extents_.begin(), extents_.end(),
                  [](const Extent& a, const Extent& b) { return a.disc_offset < b.disc_offset; });
    }

    std::string Format() const override
    {
        return "WDF";
    }

    void Read(uint64_t offset, uint8_t* out, size_t size) override
    {
        std::fill_n(out, size, uint8_t{0});
        for (const Extent& extent : extents_)
        {
            const uint64_t start = std::max(offset, extent.disc_offset);
            const uint64_t end = std::min(offset + size, extent.disc_offset + extent.size);
            if (start < end)
            {
                source_->Read(extent.file_offset + start - extent.disc_offset, out + (start - offset),
                              static_cast<size_t>(end - start));
            }
        }
    }

private:
    struct Extent
    {
        uint64_t disc_offset;
        uint64_t file_offset;
        uint64_t size;
    };

    std::unique_ptr<Source> source_;
    std::vector<Extent> extents_;
};

// WIA and RVZ (Dolphin's format based on WIA). The disc is divided into chunks. Each group entry stores one compressed
// chunk. Raw data entries store the disc as it is. A GameCube disc is all raw data. A Wii disc's partitions are stored
// apart from the raw data, and a GameCube player has no use for them.
class WiaImage : public Image
{
public:
    explicit WiaImage(std::unique_ptr<Source> source);

    std::string Format() const override
    {
        return rvz_ ? "RVZ" : "WIA";
    }

    void Read(uint64_t offset, uint8_t* out, size_t size) override;

    // Returns the data of a group that holds `size` bytes. In RVZ, the junk starts `junk_offset` bytes into the disc.
    // The result stays valid until the next call. Only a Wii partition's groups start with lists of hash exceptions.
    // A GameCube disc's raw data has none.
    const std::vector<uint8_t>& Group(uint32_t index, size_t size, uint64_t junk_offset);

private:
    struct Raw
    {
        uint64_t offset;
        uint64_t size;
        uint32_t group;
        uint32_t groups;
    };

    struct GroupEntry
    {
        uint64_t offset;
        uint32_t stored_size;
        bool compressed;
        uint32_t packed_size; // RVZ: the size before unpacking, or 0 if the group isn't packed
    };

    // The newest version of the formats that this reader supports: 1.00 for both (encoded as 0xAABBCCDD for A.BB.CC).
    static constexpr uint32_t kVersion = 0x01000000;

    // Reads one of the tables. Tables are compressed like the groups. `size` is the size after decompression.
    std::vector<uint8_t> Table(uint64_t offset, uint32_t stored_size, size_t size);

    // Decodes WIA's PURGE method. The data holds segments of data over zeros, then a SHA-1. Each segment gives an
    // offset, a size and the bytes.
    static void Purge(std::span<const uint8_t> stored, std::span<uint8_t> out);

    // Decodes RVZ's packing: runs of data, and runs of junk given as seeds.
    static void Unpack(std::span<const uint8_t> packed, std::span<uint8_t> out, uint64_t junk_offset);

    std::unique_ptr<Source> source_;
    bool rvz_ = false;
    Method method_ = Method::kNone;
    std::vector<uint8_t> props_;
    uint64_t chunk_size_ = 0;
    std::array<uint8_t, 0x80> disc_head_{};
    std::vector<Raw> raw_;
    std::vector<GroupEntry> groups_;
    uint32_t cached_group_ = std::numeric_limits<uint32_t>::max();
    std::vector<uint8_t> cached_;
};

// Whether `data` hashes to the SHA-1 at `hash`.
bool MatchesHash(std::span<const uint8_t> data, const uint8_t* hash)
{
    const Sha1::Digest digest = Sha1::Of(data);

    return std::equal(digest.begin(), digest.end(), hash);
}

WiaImage::WiaImage(std::unique_ptr<Source> source) : source_(std::move(source))
{
    uint8_t head[0x48];
    source_->Read(0, head, sizeof(head));
    rvz_ = head[0] == 'R';
    const std::string name = Format();
    if (!MatchesHash({head, 0x34}, head + 0x34))
    {
        throw Error("the " + name + " header doesn't match its hash");
    }
    if (LoadBe32(head + 8) > kVersion)
    {
        throw Error("this " + name + " file requires a newer reader");
    }

    // wia_disc_t. It may be shorter or longer than the version this reader supports.
    const uint32_t disc_size = LoadBe32(head + 0x0C);
    if (disc_size > 0x1000)
    {
        throw Error("the " + name + " header is invalid");
    }

    std::vector<uint8_t> disc = source_->Read(0x48, disc_size);
    if (!MatchesHash(disc, head + 0x10))
    {
        throw Error("the " + name + " disc header doesn't match its hash");
    }

    disc.resize(std::max<size_t>(disc.size(), 0xDC));
    const uint32_t method = LoadBe32(disc.data() + 4);
    chunk_size_ = LoadBe32(disc.data() + 0x0C);
    std::copy_n(disc.begin() + 0x10, disc_head_.size(), disc_head_.begin());
    if (method > (rvz_ ? 5u : 4u) || (rvz_ && method == 1))
    {
        throw Error(name + " compression method " + std::to_string(method) + " isn't supported");
    }

    method_ = static_cast<Method>(method);
    props_.assign(disc.begin() + 0xD5, disc.begin() + 0xD5 + std::min<uint8_t>(disc[0xD4], 7));
    const bool power_of_two = (chunk_size_ & (chunk_size_ - 1)) == 0;
    if (!chunk_size_ || chunk_size_ > kMaxBlockSize ||
        (chunk_size_ % 0x200000 && !(rvz_ && chunk_size_ >= kSector && power_of_two)))
    {
        throw Error("the " + name + " chunk size is invalid");
    }

    // The raw data and group tables. The first raw data entry starts at 0x80, after the header stored in wia_disc_t.
    // Its data starts at 0, though. Each entry is therefore rounded down to a sector.
    const uint32_t raw_count = LoadBe32(disc.data() + 0xB4);
    const uint32_t group_count = LoadBe32(disc.data() + 0xC4);
    const size_t group_entry = rvz_ ? 12 : 8;
    if (raw_count > (1u << 20) || group_count > (1u << 24))
    {
        throw Error("the " + name + " tables are invalid");
    }

    const std::vector<uint8_t> raw =
        Table(LoadBe64(disc.data() + 0xB8), LoadBe32(disc.data() + 0xC0), size_t{raw_count} * 24);
    for (uint32_t i = 0; i < raw_count; ++i)
    {
        const uint8_t* p = raw.data() + i * 24;
        const uint64_t offset = LoadBe64(p);
        const uint64_t aligned = offset - offset % kSector;
        raw_.push_back({aligned, LoadBe64(p + 8) + offset - aligned, LoadBe32(p + 16), LoadBe32(p + 20)});
    }

    const std::vector<uint8_t> groups =
        Table(LoadBe64(disc.data() + 0xC8), LoadBe32(disc.data() + 0xD0), group_count * group_entry);
    for (uint32_t i = 0; i < group_count; ++i)
    {
        const uint8_t* p = groups.data() + i * group_entry;
        const uint32_t stored = LoadBe32(p + 4);
        if (rvz_)
        {
            // The top bit marks a group as compressed with the file's method. The method NONE leaves the data
            // unchanged.
            const bool compressed = (stored >> 31) != 0 && method_ != Method::kNone;
            groups_.push_back({uint64_t{LoadBe32(p)} * 4, stored & 0x7FFFFFFF, compressed, LoadBe32(p + 8)});
        }
        else
        {
            groups_.push_back({uint64_t{LoadBe32(p)} * 4, stored, method_ > Method::kPurge, 0});
        }
    }
}

std::vector<uint8_t> WiaImage::Table(uint64_t offset, uint32_t stored_size, size_t size)
{
    std::vector<uint8_t> stored = source_->Read(offset, stored_size);
    std::vector<uint8_t> table;
    if (method_ == Method::kNone)
    {
        table = std::move(stored);
    }
    else if (method_ == Method::kPurge)
    {
        table.resize(size);
        Purge(stored, table);
    }
    else
    {
        table = Decompress(method_, props_, stored, size);
    }

    if (table.size() < size)
    {
        throw Error("the " + Format() + " tables are truncated");
    }

    return table;
}

void WiaImage::Purge(std::span<const uint8_t> stored, std::span<uint8_t> out)
{
    size_t position = 0;
    if (stored.size() < 20)
    {
        throw Error("a PURGE group is truncated");
    }

    const size_t end = stored.size() - 20;
    std::fill(out.begin(), out.end(), uint8_t{0});
    while (position < end)
    {
        if (end - position < 8)
        {
            throw Error("a PURGE group is damaged");
        }

        const uint32_t offset = LoadBe32(stored.data() + position);
        const uint32_t size = LoadBe32(stored.data() + position + 4);
        position += 8;
        if (size > end - position || offset > out.size() || size > out.size() - offset)
        {
            throw Error("a PURGE group is damaged");
        }

        std::copy_n(stored.begin() + static_cast<std::ptrdiff_t>(position), size, out.begin() + offset);
        position += size;
    }
}

void WiaImage::Unpack(std::span<const uint8_t> packed, std::span<uint8_t> out, uint64_t junk_offset)
{
    size_t position = 0;
    size_t written = 0;
    while (written < out.size())
    {
        if (packed.size() - position < 4)
        {
            throw Error("an RVZ group's packed data is truncated");
        }

        uint32_t run = LoadBe32(packed.data() + position);
        position += 4;
        const bool junk = (run & 0x80000000) != 0;
        run &= 0x7FFFFFFF;
        const size_t n = std::min<size_t>(run, out.size() - written);
        if (junk)
        {
            if (packed.size() - position < 68)
            {
                throw Error("an RVZ group's packed data is truncated");
            }

            JunkGenerator generator(packed.data() + position);
            position += 68;
            generator.Skip(static_cast<size_t>((junk_offset + written) % kSector));
            generator.Generate(out.data() + written, n);
        }
        else
        {
            if (packed.size() - position < run)
            {
                throw Error("an RVZ group's packed data is truncated");
            }

            std::copy_n(packed.begin() + static_cast<std::ptrdiff_t>(position), n, out.begin() + written);
            position += run;
        }

        written += n;
    }
}

const std::vector<uint8_t>& WiaImage::Group(uint32_t index, size_t size, uint64_t junk_offset)
{
    if (index == cached_group_ && cached_.size() == size)
    {
        return cached_;
    }
    if (index >= groups_.size())
    {
        throw Error("the " + Format() + " file is missing a group");
    }

    cached_group_ = std::numeric_limits<uint32_t>::max();
    cached_.assign(size, 0);
    const GroupEntry& group = groups_[index];
    if (group.stored_size)
    {
        std::vector<uint8_t> stored = source_->Read(group.offset, group.stored_size);
        if (method_ == Method::kPurge)
        {
            Purge(stored, cached_);
        }
        else
        {
            const size_t limit = std::max<size_t>(size, group.packed_size);
            const std::vector<uint8_t> data =
                group.compressed ? Decompress(method_, props_, stored, limit) : std::move(stored);
            if (group.packed_size)
            {
                Unpack(data, cached_, junk_offset);
            }
            else if (data.size() < size)
            {
                throw Error("a group in the " + Format() + " file holds too little data");
            }
            else
            {
                std::copy_n(data.begin(), size, cached_.begin());
            }
        }
    }

    cached_group_ = index;

    return cached_;
}

void WiaImage::Read(uint64_t offset, uint8_t* out, size_t size)
{
    std::fill_n(out, size, uint8_t{0});
    if (offset < disc_head_.size())
    {
        std::copy_n(disc_head_.begin() + static_cast<std::ptrdiff_t>(offset),
                    std::min<uint64_t>(size, disc_head_.size() - offset), out);
    }

    for (const Raw& raw : raw_)
    {
        if (offset + size <= raw.offset || offset >= raw.offset + raw.size)
        {
            continue;
        }

        // Each group holds one chunk of the entry's data. The last group may hold less.
        const uint64_t first = (std::max(offset, raw.offset) - raw.offset) / chunk_size_;
        for (uint64_t g = first; g < raw.groups; ++g)
        {
            const uint64_t group_start = raw.offset + g * chunk_size_;
            if (group_start >= std::min(raw.offset + raw.size, offset + size))
            {
                break;
            }

            const uint64_t group_size = std::min(chunk_size_, raw.offset + raw.size - group_start);
            const uint64_t start = std::max({offset, group_start, uint64_t{disc_head_.size()}});
            const uint64_t end = std::min(offset + size, group_start + group_size);
            if (start < end)
            {
                const std::vector<uint8_t>& data =
                    Group(static_cast<uint32_t>(raw.group + g), static_cast<size_t>(group_size), group_start);
                std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(start - group_start), end - start,
                            out + (start - offset));
            }
        }
    }
}

} // namespace

std::unique_ptr<Image> OpenImage(const std::filesystem::path& path)
{
    auto source = std::make_unique<Source>(path);
    uint8_t magic[8] = {};
    source->Read(0, magic, static_cast<size_t>(std::min<uint64_t>(sizeof(magic), source->Size())));
    const std::string text(reinterpret_cast<const char*>(magic), sizeof(magic));
    if (LoadLe32(magic) == 0xB10BC001)
    {
        return std::make_unique<GczImage>(std::move(source));
    }
    if (text.starts_with(std::string("WIA\x01", 4)) || text.starts_with(std::string("RVZ\x01", 4)))
    {
        return std::make_unique<WiaImage>(std::move(source));
    }
    if (text.starts_with("CISO"))
    {
        return std::make_unique<CisoImage>(std::move(source));
    }
    if (text == std::string("WII\x01"
                            "DISC",
                            8))
    {
        return std::make_unique<WdfImage>(std::move(source));
    }

    return std::make_unique<PlainImage>(std::move(source));
}

} // namespace gcsf::disc
