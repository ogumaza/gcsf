// SPDX-License-Identifier: MIT

// Builds a small GameCube disc and stores it in every container format that the reader supports. The writers here
// follow the formats' descriptions. The formats are ISO (plain and NKit), GCZ, CISO, WDF versions 1 and 2 (whole and
// split), WIA with each of its compression methods, and RVZ with each of its methods, three chunk sizes and packed
// junk. Checks that LoadGame reads the same system files and files from each image and from extracted discs. Also
// checks that it rejects a Wii disc and a file that isn't a disc.

#include <bzlib.h>
#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <map>
#include <random>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "check.h"
#include "common/bytes.h"
#include "common/sha1.h"
#include "disc/compression.h"
#include "disc/game.h"
#include "disc/junk.h"

namespace
{

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using gcsf::disc::Method;

constexpr uint64_t kSector = 0x8000; // the period of the junk between files

// Fills data with a pseudorandom pattern (xorshift) so that it doesn't compress to nothing.
void Fill(Bytes& data, size_t offset, size_t size, uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 1;
    for (size_t i = 0; i < size; ++i)
    {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        data[offset + i] = static_cast<uint8_t>(x);
    }
}

void WriteFile(const fs::path& path, const Bytes& data)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

// A run of junk on the disc. The junk is generated like a disc's padding.
struct Junk
{
    uint64_t offset;
    uint64_t size;
    std::array<uint8_t, 68> seed;
};

// The test disc: its bytes, its system files and the files in it.
struct TestDisc
{
    Bytes boot;
    Bytes bi2;
    Bytes apploader;
    Bytes dol;
    Bytes fst;
    std::map<std::string, Bytes> files; // by the path the reader gives, decoded from Shift-JIS
    Bytes data;                         // the whole disc
    std::vector<Junk> junk;

    // The disc as an ISO image, with NKit's header if `nkit`.
    Bytes Iso(bool nkit) const;
};

// A file system entry for the table.
struct Entry
{
    std::string name; // Shift-JIS bytes, as on the disc
    bool directory;
    uint32_t parent_or_offset;
    uint32_t size_or_next;
};

TestDisc BuildDisc()
{
    TestDisc disc;
    disc.data.assign(0x3A0000, 0);
    Bytes& data = disc.data;

    // The disc's header (boot.bin), with the ID, the magic, the title, and the offsets of main.dol and the file system
    // table.
    const uint64_t kDol = 0x10000;
    const uint64_t kFst = 0x14000;
    disc.boot.assign(0x440, 0);
    std::copy_n("GCST01", 6, disc.boot.begin());
    gcsf::StoreBe32(disc.boot.data() + 0x1C, 0xC2339F3D);
    std::copy_n("GCSF TEST DISC", 14, disc.boot.begin() + 0x20);
    gcsf::StoreBe32(disc.boot.data() + 0x420, kDol);
    gcsf::StoreBe32(disc.boot.data() + 0x424, kFst);

    disc.bi2.assign(0x2000, 0);
    Fill(disc.bi2, 0, disc.bi2.size(), 1);

    // The apploader: a header with its date, entry point, size and trailer size, then its code.
    disc.apploader.assign(0x20 + 0x1234 + 0x100, 0);
    Fill(disc.apploader, 0x20, disc.apploader.size() - 0x20, 2);
    std::copy_n("2026/10/07", 10, disc.apploader.begin());
    gcsf::StoreBe32(disc.apploader.data() + 0x10, 0x81200000);
    gcsf::StoreBe32(disc.apploader.data() + 0x14, 0x1234);
    gcsf::StoreBe32(disc.apploader.data() + 0x18, 0x100);

    // main.dol: one text and one data section.
    disc.dol.assign(0x2600, 0);
    Fill(disc.dol, 0x100, 0x2500, 3);
    std::fill_n(disc.dol.begin(), 0x100, uint8_t{0});
    gcsf::StoreBe32(disc.dol.data() + 0x00, 0x100);
    gcsf::StoreBe32(disc.dol.data() + 0x48, 0x80004000);
    gcsf::StoreBe32(disc.dol.data() + 0x90, 0x2000);
    gcsf::StoreBe32(disc.dol.data() + 0x1C, 0x2100);
    gcsf::StoreBe32(disc.dol.data() + 0x64, 0x80006000);
    gcsf::StoreBe32(disc.dol.data() + 0xAC, 0x500);
    gcsf::StoreBe32(disc.dol.data() + 0xE0, 0x80004000);

    // The files: one across many sectors and two chunks, a small one in a subfolder, an empty one, and two whose names
    // aren't ASCII (half-width katakana, and two double-byte characters that come out as %XX).
    struct File
    {
        std::string disc_name;
        std::string path;
        uint64_t offset;
        uint32_t size;
    };

    const std::vector<File> kFiles = {
        {"\x83R\x83s.bin", "%83R%83s.bin", 0x18004, 0x1000},
        {"a.baa", "AudioRes/a.baa", 0x20000, 0x30000},
        {"b.aw", "AudioRes/sub/b.aw", 0x50010, 0x10},
        {"big.bin", "big.bin", 0x60000, 0x250000},
        {"empty.bin", "empty.bin", 0x2B0000, 0},
        {"\xB1\xB2.txt", "\xEF\xBD\xB1\xEF\xBD\xB2.txt", 0x2B0000, 0x7C04},
    };
    for (size_t i = 0; i < kFiles.size(); ++i)
    {
        Fill(data, kFiles[i].offset, kFiles[i].size, static_cast<uint32_t>(10 + i));
        disc.files[kFiles[i].path] =
            Bytes(data.begin() + static_cast<std::ptrdiff_t>(kFiles[i].offset),
                  data.begin() + static_cast<std::ptrdiff_t>(kFiles[i].offset + kFiles[i].size));
    }

    // The table: root, then each entry in order, with a directory's last entry + 1 as its size.
    const std::vector<Entry> kEntries = {
        {"", true, 0, 9},
        {kFiles[0].disc_name, false, 0x18004, 0x1000},
        {"AudioRes", true, 0, 6},
        {"a.baa", false, 0x20000, 0x30000},
        {"sub", true, 2, 6},
        {"b.aw", false, 0x50010, 0x10},
        {"big.bin", false, 0x60000, 0x250000},
        {"empty.bin", false, 0x2B0000, 0},
        {kFiles[5].disc_name, false, 0x2B0000, 0x7C04},
    };
    std::string names;
    disc.fst.assign(kEntries.size() * 12, 0);
    for (size_t i = 0; i < kEntries.size(); ++i)
    {
        uint8_t* entry = disc.fst.data() + i * 12;
        gcsf::StoreBe32(entry, static_cast<uint32_t>(names.size()));
        entry[0] = kEntries[i].directory ? 1 : 0;
        gcsf::StoreBe32(entry + 4, kEntries[i].parent_or_offset);
        gcsf::StoreBe32(entry + 8, kEntries[i].size_or_next);
        names += kEntries[i].name;
        names += '\0';
    }

    disc.fst.insert(disc.fst.end(), names.begin(), names.end());
    while (disc.fst.size() % 4)
    {
        disc.fst.push_back(0);
    }

    gcsf::StoreBe32(disc.boot.data() + 0x428, static_cast<uint32_t>(disc.fst.size()));
    gcsf::StoreBe32(disc.boot.data() + 0x42C, static_cast<uint32_t>(disc.fst.size()));

    // Junk between the files, as on a disc: one run inside a sector, one across several.
    for (const auto& [offset, size, seed] : {std::tuple{0x51000u, 0x3000u, 7u}, std::tuple{0x2B8000u, 0x50000u, 8u}})
    {
        Junk junk = {offset, size, {}};
        for (size_t i = 0; i < junk.seed.size(); ++i)
        {
            junk.seed[i] = static_cast<uint8_t>(seed * 31 + i * 17);
        }

        gcsf::disc::JunkGenerator generator(junk.seed.data());
        generator.Skip(offset % kSector);
        generator.Generate(data.data() + offset, size);
        disc.junk.push_back(junk);
    }

    std::copy(disc.boot.begin(), disc.boot.end(), data.begin());
    std::copy(disc.bi2.begin(), disc.bi2.end(), data.begin() + 0x440);
    std::copy(disc.apploader.begin(), disc.apploader.end(), data.begin() + 0x2440);
    std::copy(disc.dol.begin(), disc.dol.end(), data.begin() + kDol);
    std::copy(disc.fst.begin(), disc.fst.end(), data.begin() + kFst);

    return disc;
}

Bytes TestDisc::Iso(bool nkit) const
{
    Bytes iso = data;
    if (nkit)
    {
        std::copy_n("NKIT v01", 8, iso.begin() + 0x200);
    }

    return iso;
}

// GCZ: zlib-compressed blocks. A block that doesn't shrink is stored as it is.
Bytes Gcz(const Bytes& iso)
{
    const uint32_t kBlock = 0x4000;
    const uint32_t count = static_cast<uint32_t>((iso.size() + kBlock - 1) / kBlock);
    Bytes pointers(size_t{count} * 8);
    Bytes checksums(size_t{count} * 4);
    Bytes blocks;
    for (uint32_t i = 0; i < count; ++i)
    {
        const size_t size = std::min<size_t>(kBlock, iso.size() - size_t{i} * kBlock);
        Bytes block(iso.begin() + i * kBlock, iso.begin() + i * kBlock + static_cast<std::ptrdiff_t>(size));
        block.resize(kBlock, 0);
        uLongf compressed_size = compressBound(kBlock);
        Bytes compressed(compressed_size);
        compress2(compressed.data(), &compressed_size, block.data(), kBlock, 9);
        compressed.resize(compressed_size);
        const bool raw = compressed.size() >= kBlock;
        const Bytes& stored = raw ? block : compressed;
        gcsf::StoreLe64(pointers.data() + i * 8, blocks.size() | (raw ? 1ull << 63 : 0));
        gcsf::StoreLe32(checksums.data() + i * 4, gcsf::disc::Adler32(stored));
        blocks.insert(blocks.end(), stored.begin(), stored.end());
    }

    Bytes gcz(32);
    gcsf::StoreLe32(gcz.data(), 0xB10BC001);
    gcsf::StoreLe32(gcz.data() + 4, 1);
    gcsf::StoreLe64(gcz.data() + 8, blocks.size());
    gcsf::StoreLe64(gcz.data() + 16, size_t{count} * kBlock);
    gcsf::StoreLe32(gcz.data() + 24, kBlock);
    gcsf::StoreLe32(gcz.data() + 28, count);
    gcz.insert(gcz.end(), pointers.begin(), pointers.end());
    gcz.insert(gcz.end(), checksums.begin(), checksums.end());
    gcz.insert(gcz.end(), blocks.begin(), blocks.end());

    return gcz;
}

bool AllZero(const Bytes& data, size_t offset, size_t size)
{
    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);

    return std::all_of(begin, begin + static_cast<std::ptrdiff_t>(size), [](uint8_t b) { return b == 0; });
}

// CISO: the map of stored blocks, then the blocks that aren't all zeros.
Bytes Ciso(const Bytes& iso)
{
    const uint32_t kBlock = 0x10000;
    Bytes ciso(0x8000, 0);
    std::copy_n("CISO", 4, ciso.begin());
    gcsf::StoreLe32(ciso.data() + 4, kBlock);
    for (size_t i = 0; i * kBlock < iso.size(); ++i)
    {
        const size_t size = std::min<size_t>(kBlock, iso.size() - i * kBlock);
        if (AllZero(iso, i * kBlock, size))
        {
            continue;
        }

        ciso[8 + i] = 1;
        Bytes block(iso.begin() + static_cast<std::ptrdiff_t>(i * kBlock),
                    iso.begin() + static_cast<std::ptrdiff_t>(i * kBlock + size));
        block.resize(kBlock, 0);
        ciso.insert(ciso.end(), block.begin(), block.end());
    }

    return ciso;
}

// WDF: the extents of 64 KiB that aren't all zeros, then a magic and the table of extents.
Bytes Wdf(const Bytes& iso, int version)
{
    const uint64_t kPiece = 0x10000;
    Bytes wdf(0x38, 0);
    Bytes table;
    uint32_t count = 0;
    for (uint64_t offset = 0; offset < iso.size(); offset += kPiece)
    {
        const uint64_t size = std::min<uint64_t>(kPiece, iso.size() - offset);
        if (AllZero(iso, offset, size))
        {
            continue;
        }

        Bytes entry(version == 1 ? 28 : 24, 0);
        uint8_t* p = entry.data() + (version == 1 ? 4 : 0);
        gcsf::StoreBe64(p, offset);
        gcsf::StoreBe64(p + 8, wdf.size());
        gcsf::StoreBe64(p + 16, size);
        table.insert(table.end(), entry.begin(), entry.end());
        wdf.insert(wdf.end(), iso.begin() + static_cast<std::ptrdiff_t>(offset),
                   iso.begin() + static_cast<std::ptrdiff_t>(offset + size));
        ++count;
    }

    std::copy_n(
        "WII\x01"
        "DISC",
        8, wdf.begin());
    gcsf::StoreBe32(wdf.data() + 8, version);
    gcsf::StoreBe64(wdf.data() + 0x18, iso.size());
    gcsf::StoreBe32(wdf.data() + 0x2C, count);
    gcsf::StoreBe64(wdf.data() + 0x30, wdf.size());
    wdf.insert(wdf.end(), {'W', 'I', 'I', 1, 'D', 'I', 'S', 'C'});
    wdf.insert(wdf.end(), table.begin(), table.end());

    return wdf;
}

// Compresses a WIA or RVZ group's data with `method`. `props` receives the compressor data for the header.
Bytes Compress(Method method, const Bytes& data, Bytes& props)
{
    Bytes out(data.size() * 2 + 0x10000);
    size_t size = out.size();
    switch (method)
    {
    case Method::kBzip2:
        {
            unsigned bzip2_size = static_cast<unsigned>(out.size());
            BZ2_bzBuffToBuffCompress(reinterpret_cast<char*>(out.data()), &bzip2_size,
                                     const_cast<char*>(reinterpret_cast<const char*>(data.data())),
                                     static_cast<unsigned>(data.size()), 9, 0, 30);
            size = bzip2_size;
            break;
        }

    case Method::kLzma:
    case Method::kLzma2:
        {
            lzma_options_lzma options;
            lzma_lzma_preset(&options, 6);
            options.dict_size = 1 << 20;
            options.lc = 3;
            options.lp = 0;
            options.pb = 2;
            const lzma_filter filters[] = {
                {method == Method::kLzma ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2, &options},
                {LZMA_VLI_UNKNOWN, nullptr},
            };
            size_t written = 0;
            lzma_raw_buffer_encode(filters, nullptr, data.data(), data.size(), out.data(), &written, out.size());
            size = written;

            // The 7-Zip SDK's properties: (pb * 5 + lp) * 9 + lc and the dictionary size. For LZMA2 they're a byte for
            // the dictionary size. The dictionary size here is 2 << 19.
            props = method == Method::kLzma ? Bytes{(2 * 5 + 0) * 9 + 3, 0, 0, 0x10, 0} : Bytes{16};
            break;
        }

    case Method::kZstd:
        size = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), 3);
        break;

    default:
        size = 0;
        break;
    }

    out.resize(size);

    return out;
}

// One group's data before compression, and the RVZ packing or WIA PURGE encoding of it.
struct GroupData
{
    Bytes lists; // the hash exception lists (each empty here)
    Bytes data;
    std::vector<Junk> junk; // runs of junk in `data`, offsets relative to it, for RVZ packing
};

// Packs `data` for RVZ: data runs, and junk runs as their seeds.
Bytes Pack(const Bytes& data, const std::vector<Junk>& junk)
{
    Bytes packed;
    const auto run = [&packed](uint32_t header)
    {
        packed.resize(packed.size() + 4);
        gcsf::StoreBe32(packed.data() + packed.size() - 4, header);
    };

    uint64_t position = 0;
    for (const Junk& j : junk)
    {
        if (j.offset > position)
        {
            run(static_cast<uint32_t>(j.offset - position));
            packed.insert(packed.end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                          data.begin() + static_cast<std::ptrdiff_t>(j.offset));
        }

        run(static_cast<uint32_t>(j.size) | 0x80000000);
        packed.insert(packed.end(), j.seed.begin(), j.seed.end());
        position = j.offset + j.size;
    }
    if (position < data.size())
    {
        run(static_cast<uint32_t>(data.size() - position));
        packed.insert(packed.end(), data.begin() + static_cast<std::ptrdiff_t>(position), data.end());
    }

    return packed;
}

// WIA's PURGE: the lists, padding, then the data that isn't zeros as segments, then a SHA-1.
Bytes Purge(const GroupData& group)
{
    Bytes out = group.lists;
    out.resize((out.size() + 3) & ~size_t{3}, 0);
    for (size_t offset = 0; offset < group.data.size();)
    {
        if (!group.data[offset])
        {
            ++offset;
            continue;
        }

        size_t end = offset;
        while (end < group.data.size() && (group.data[end] || (end + 8 < group.data.size() && group.data[end + 8])))
        {
            ++end;
        }

        Bytes header(8);
        gcsf::StoreBe32(header.data(), static_cast<uint32_t>(offset));
        gcsf::StoreBe32(header.data() + 4, static_cast<uint32_t>(end - offset));
        out.insert(out.end(), header.begin(), header.end());
        out.insert(out.end(), group.data.begin() + static_cast<std::ptrdiff_t>(offset),
                   group.data.begin() + static_cast<std::ptrdiff_t>(end));
        offset = end;
    }

    const gcsf::Sha1::Digest hash = gcsf::Sha1::Of(out);
    out.insert(out.end(), hash.begin(), hash.end());

    return out;
}

// A WIA or RVZ file of the disc. A GameCube disc is all raw data in both formats.
Bytes Wia(const TestDisc& disc, bool rvz, Method method, uint32_t chunk_size)
{
    const Bytes iso = disc.Iso(false);
    const uint64_t iso_size = iso.size();
    std::vector<GroupData> groups;
    Bytes raw_table;
    const Bytes partition_table;

    // Raw data: from 0x80 to the end.
    const uint64_t raw_end = iso.size();
    const uint64_t raw_groups = (raw_end + chunk_size - 1) / chunk_size;
    raw_table.resize(24);
    gcsf::StoreBe64(raw_table.data(), 0x80);
    gcsf::StoreBe64(raw_table.data() + 8, raw_end - 0x80);
    gcsf::StoreBe32(raw_table.data() + 16, 0);
    gcsf::StoreBe32(raw_table.data() + 20, static_cast<uint32_t>(raw_groups));
    for (uint64_t g = 0; g < raw_groups; ++g)
    {
        const uint64_t start = g * chunk_size;
        const uint64_t size = std::min<uint64_t>(chunk_size, raw_end - start);
        GroupData group;
        group.data.assign(iso.begin() + static_cast<std::ptrdiff_t>(start),
                          iso.begin() + static_cast<std::ptrdiff_t>(start + size));
        for (const Junk& j : disc.junk)
        {
            if (j.offset >= start && j.offset + j.size <= start + size)
            {
                group.junk.push_back({j.offset - start, j.size, j.seed});
            }
        }

        groups.push_back(group);
    }

    // The tables are compressed like the groups.
    Bytes props;
    const auto table = [&](const Bytes& plain)
    {
        if (method == Method::kNone)
        {
            return plain;
        }
        if (method == Method::kPurge)
        {
            return Purge({{}, plain, {}});
        }

        return Compress(method, plain, props);
    };

    // The layout: the two headers, the partition table, the raw data table, the groups' data and then the group table.
    // The data is aligned to 4 bytes because the group table stores offsets divided by 4.
    Bytes disc_header(0xDC, 0);
    const Bytes raw_stored = table(raw_table);
    const uint64_t partition_offset = 0x48 + disc_header.size();
    const uint64_t raw_offset = partition_offset + partition_table.size();
    const uint64_t data_offset = (raw_offset + raw_stored.size() + 3) & ~uint64_t{3};
    Bytes data;
    Bytes group_table;
    for (const GroupData& group : groups)
    {
        uint32_t stored_size = 0;
        bool compressed = false;
        uint32_t packed_size = 0;
        const uint64_t offset = data_offset + data.size();
        if (!AllZero(group.data, 0, group.data.size()))
        {
            Bytes body = group.data;
            if (rvz && !group.junk.empty())
            {
                body = Pack(group.data, group.junk);
                packed_size = static_cast<uint32_t>(body.size());
            }

            Bytes stored;
            if (method == Method::kPurge)
            {
                stored = Purge(group);
            }
            else if (method == Method::kNone)
            {
                stored = group.lists;
                stored.resize((stored.size() + 3) & ~size_t{3}, 0);
                stored.insert(stored.end(), body.begin(), body.end());
            }
            else
            {
                Bytes plain = group.lists;
                plain.insert(plain.end(), body.begin(), body.end());
                stored = Compress(method, plain, props);
                compressed = true;
            }

            data.insert(data.end(), stored.begin(), stored.end());
            stored_size = static_cast<uint32_t>(stored.size());
            data.resize((data.size() + 3) & ~size_t{3}, 0);
        }

        Bytes entry(rvz ? 12 : 8, 0);
        gcsf::StoreBe32(entry.data(), static_cast<uint32_t>(offset / 4));
        gcsf::StoreBe32(entry.data() + 4, stored_size | (rvz && compressed ? 0x80000000 : 0));
        if (rvz)
        {
            gcsf::StoreBe32(entry.data() + 8, packed_size);
        }

        group_table.insert(group_table.end(), entry.begin(), entry.end());
    }

    const uint64_t group_offset = data_offset + data.size();
    const Bytes groups_stored = table(group_table);

    // wia_disc_t: the disc's type (1 for a GameCube disc), the method, the chunk size, the first 0x80 bytes of the
    // disc, and where the tables are. There's no partition.
    gcsf::StoreBe32(disc_header.data(), 1);
    gcsf::StoreBe32(disc_header.data() + 4, static_cast<uint32_t>(method));
    gcsf::StoreBe32(disc_header.data() + 0x0C, chunk_size);
    std::copy_n(iso.begin(), 0x80, disc_header.begin() + 0x10);
    gcsf::StoreBe32(disc_header.data() + 0x90, 0);
    gcsf::StoreBe32(disc_header.data() + 0x94, 0x30);
    gcsf::StoreBe64(disc_header.data() + 0x98, partition_offset);
    const gcsf::Sha1::Digest partition_hash = gcsf::Sha1::Of(partition_table);
    std::copy(partition_hash.begin(), partition_hash.end(), disc_header.begin() + 0xA0);
    gcsf::StoreBe32(disc_header.data() + 0xB4, 1);
    gcsf::StoreBe64(disc_header.data() + 0xB8, raw_offset);
    gcsf::StoreBe32(disc_header.data() + 0xC0, static_cast<uint32_t>(raw_stored.size()));
    gcsf::StoreBe32(disc_header.data() + 0xC4, static_cast<uint32_t>(groups.size()));
    gcsf::StoreBe64(disc_header.data() + 0xC8, group_offset);
    gcsf::StoreBe32(disc_header.data() + 0xD0, static_cast<uint32_t>(groups_stored.size()));
    disc_header[0xD4] = static_cast<uint8_t>(props.size());
    std::copy(props.begin(), props.end(), disc_header.begin() + 0xD5);

    // wia_file_head_t, with the hashes of wia_disc_t and of itself.
    Bytes file(0x48, 0);
    std::copy_n(rvz ? "RVZ\x01" : "WIA\x01", 4, file.begin());
    gcsf::StoreBe32(file.data() + 4, 0x01000000);
    gcsf::StoreBe32(file.data() + 8, 0x00090000);
    gcsf::StoreBe32(file.data() + 0x0C, static_cast<uint32_t>(disc_header.size()));
    const gcsf::Sha1::Digest disc_hash = gcsf::Sha1::Of(disc_header);
    std::copy(disc_hash.begin(), disc_hash.end(), file.begin() + 0x10);
    gcsf::StoreBe64(file.data() + 0x24, iso_size);
    file.insert(file.end(), disc_header.begin(), disc_header.end());
    file.insert(file.end(), partition_table.begin(), partition_table.end());
    file.insert(file.end(), raw_stored.begin(), raw_stored.end());
    file.resize(data_offset, 0);
    file.insert(file.end(), data.begin(), data.end());
    file.insert(file.end(), groups_stored.begin(), groups_stored.end());
    gcsf::StoreBe64(file.data() + 0x2C, file.size());
    const gcsf::Sha1::Digest head_hash = gcsf::Sha1::Of({file.data(), 0x34});
    std::copy(head_hash.begin(), head_hash.end(), file.begin() + 0x34);

    return file;
}

// Checks that `game` holds the test disc's files.
void CheckGame(const std::string& name, const fs::path& path, const TestDisc& disc)
{
    gcsf::disc::GameFiles game;
    const auto error = gcsf::disc::LoadGame(path, game);
    Check(!error, name + ": " + error.value_or(""));
    if (error)
    {
        return;
    }

    Check(game.GameId() == "GCST01" && game.Title() == "GCSF TEST DISC", name + ": the ID and title");
    Check(game.boot == disc.boot && game.dol == disc.dol && game.fst == disc.fst,
          name + ": boot.bin, main.dol, fst.bin");
    Check(game.bi2 == disc.bi2 && game.apploader == disc.apploader, name + ": bi2.bin and apploader.img");
    Check(game.files.size() == disc.files.size(), name + ": the number of files");
    for (const gcsf::disc::GameFile& file : game.files)
    {
        const auto expected = disc.files.find(file.path);
        Bytes data;
        Check(expected != disc.files.end() && file.size == expected->second.size(), name + ": listing " + file.path);
        Check(game.read_file(file.path, data) && expected != disc.files.end() && data == expected->second,
              name + ": reading " + file.path);
    }
}

// Checks that LoadGame rejects `path` with a message that contains `words`.
void CheckRefused(const std::string& name, const fs::path& path, const std::string& words)
{
    gcsf::disc::GameFiles game;
    const auto error = gcsf::disc::LoadGame(path, game);
    Check(error && error->find(words) != std::string::npos,
          name + ": refused with \"" + words + "\", got \"" + error.value_or("success") + "\"");
}

} // namespace

// usage: disc_test [folder]. With a folder, the images are written there and kept.
int main(int argc, char** argv)
{
    const TestDisc disc = BuildDisc();
    std::error_code error;
    const bool keep = argc > 1;
    const fs::path dir = keep
                             ? fs::path(argv[1])
                             : fs::temp_directory_path() / ("gcsf_disc_test_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);

    // ISO images, plain and with NKit's header, and in the containers that hold an ISO as it is.
    const Bytes iso = disc.Iso(false);
    const Bytes nkit = disc.Iso(true);
    WriteFile(dir / "game.iso", iso);
    WriteFile(dir / "nkit.iso", nkit);
    WriteFile(dir / "game.gcz", Gcz(iso));
    WriteFile(dir / "game.ciso", Ciso(iso));
    WriteFile(dir / "v1.wdf", Wdf(iso, 1));
    WriteFile(dir / "v2.wdf", Wdf(nkit, 2));
    CheckGame("ISO", dir / "game.iso", disc);
    CheckGame("NKit ISO", dir / "nkit.iso", disc);
    CheckGame("GCZ", dir / "game.gcz", disc);
    CheckGame("CISO", dir / "game.ciso", disc);
    CheckGame("WDF v1", dir / "v1.wdf", disc);
    CheckGame("WDF v2", dir / "v2.wdf", disc);

    gcsf::disc::GameFiles nkit_game;
    gcsf::disc::LoadGame(dir / "nkit.iso", nkit_game);
    Check(nkit_game.source == "NKit ISO", "an NKit image's source is \"" + nkit_game.source + "\"");

    // Split images: an ISO and a WDF in pieces. The pieces are named as Wiimm's tools name them.
    const size_t cut = iso.size() / 3;
    WriteFile(dir / "split.iso", Bytes(iso.begin(), iso.begin() + static_cast<std::ptrdiff_t>(cut)));
    WriteFile(dir / "split.iso.1", Bytes(iso.begin() + static_cast<std::ptrdiff_t>(cut), iso.begin() + 2 * cut));
    WriteFile(dir / "split.iso.2", Bytes(iso.begin() + 2 * static_cast<std::ptrdiff_t>(cut), iso.end()));
    CheckGame("split ISO", dir / "split.iso", disc);
    const Bytes wdf = Wdf(iso, 2);
    WriteFile(dir / "split.wdf", Bytes(wdf.begin(), wdf.begin() + 0x40000));
    WriteFile(dir / "split.wdf.01", Bytes(wdf.begin() + 0x40000, wdf.end()));
    CheckGame("split WDF", dir / "split.wdf", disc);

    // WIA and RVZ with every compression method, and RVZ with three chunk sizes and packed junk.
    const std::vector<std::pair<Method, std::string>> kWiaMethods = {
        {Method::kNone, "NONE"}, {Method::kPurge, "PURGE"}, {Method::kBzip2, "BZIP2"},
        {Method::kLzma, "LZMA"}, {Method::kLzma2, "LZMA2"},
    };
    for (const auto& [method, method_name] : kWiaMethods)
    {
        const std::string name = "WIA " + method_name;
        const fs::path path = dir / (name + ".wia");
        WriteFile(path, Wia(disc, false, method, 0x200000));
        CheckGame(name, path, disc);
    }

    const std::vector<std::pair<Method, std::string>> kRvzMethods = {
        {Method::kNone, "NONE"},   {Method::kBzip2, "BZIP2"}, {Method::kLzma, "LZMA"},
        {Method::kLzma2, "LZMA2"}, {Method::kZstd, "ZSTD"},
    };
    for (const auto& [method, method_name] : kRvzMethods)
    {
        for (const uint32_t chunk_size : {0x8000u, 0x20000u, 0x200000u})
        {
            const std::string name = "RVZ " + method_name + " " + std::to_string(chunk_size / 1024) + " KiB";
            const fs::path path = dir / (name + ".rvz");
            WriteFile(path, Wia(disc, true, method, chunk_size));
            CheckGame(name, path, disc);
        }
    }

    // Extracted discs in Dolphin's layout (a DATA folder) and with the disc's folder given directly.
    for (const fs::path& root : {dir / "extracted" / "DATA", dir / "disc"})
    {
        WriteFile(root / "sys" / "boot.bin", disc.boot);
        WriteFile(root / "sys" / "bi2.bin", disc.bi2);
        WriteFile(root / "sys" / "apploader.img", disc.apploader);
        WriteFile(root / "sys" / "main.dol", disc.dol);
        WriteFile(root / "sys" / "fst.bin", disc.fst);
        for (const auto& [path, data] : disc.files)
        {
            WriteFile(root / "files" / fs::path(std::u8string(path.begin(), path.end())), data);
        }
    }

    CheckGame("extracted disc", dir / "extracted", disc);
    CheckGame("extracted disc folder", dir / "disc", disc);

    // What the reader rejects: a Wii disc and something that isn't a disc.
    Bytes wii = iso;
    gcsf::StoreBe32(wii.data() + 0x18, 0x5D1C9EA3);
    gcsf::StoreBe32(wii.data() + 0x1C, 0);
    WriteFile(dir / "wii.iso", wii);
    CheckRefused("Wii disc", dir / "wii.iso", "Wii disc");
    WriteFile(dir / "text.iso", Bytes(0x50000, 'x'));
    CheckRefused("not a disc", dir / "text.iso", "isn't a GameCube disc");

    if (!keep)
    {
        fs::remove_all(dir, error);
    }

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);

        return 1;
    }

    return 0;
}
