// SPDX-License-Identifier: MIT

// Tests of the reader of FSYS archives and of their LZSS compression (rip::ReadFsys, rip::DecompressLzss), with data
// that each test builds: literals, references into the window, a window that wraps, damaged data, and an archive of two
// files, one compressed and one stored.

#include "rip/fsys.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

using namespace gcsf;
using xsflib::testing::Check;
using xsflib::testing::ErrorOf;

namespace
{

// LZSS data: the header ("LZSS", `size`, the stream's size plus the header's 16 bytes, 0), then `stream`.
std::vector<uint8_t> Lzss(uint32_t size, const std::vector<uint8_t>& stream)
{
    std::vector<uint8_t> data = {'L', 'Z', 'S', 'S'};
    data.resize(16);
    xsflib::StoreBe32(data.data() + 4, size);
    xsflib::StoreBe32(data.data() + 8, static_cast<uint32_t>(16 + stream.size()));
    data.insert(data.end(), stream.begin(), stream.end());

    return data;
}

// Two literals, then a reference to the window's place 0xFEE for 4 bytes (0xEE, 0xF1), then one to place 0 for 3 bytes
// (0x00, 0x00). The flag byte 0x03 marks the first two items as literals.
const std::vector<uint8_t> kAbStream = {0x03, 'a', 'b', 0xee, 0xf1, 0x00, 0x00};

void TestLiterals()
{
    const std::vector<uint8_t> data = Lzss(8, {0xff, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'});

    const std::vector<uint8_t> out = rip::DecompressLzss(data);

    Check(out == std::vector<uint8_t>{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}, "a flag byte of 0xFF gives 8 literals");
}

void TestReferences()
{
    const std::vector<uint8_t> out = rip::DecompressLzss(Lzss(9, kAbStream));

    // The literals go to the window's places 0xFEE and 0xFEF. The first reference copies them twice, reading the bytes
    // that it writes. The window's place 0 hasn't been written, so the second reference copies zeros.
    Check(out == std::vector<uint8_t>{'a', 'b', 'a', 'b', 'a', 'b', 0, 0, 0},
          "a reference copies bytes that it has just written, and the window starts as zeros");

    Check(rip::DecompressLzss(Lzss(4, kAbStream)) == std::vector<uint8_t>{'a', 'b', 'a', 'b'},
          "a reference stops at the data's size");
}

void TestWrap()
{
    // Literals 1 to 20 go to the window's places 0xFEE to 0xFFF, then 0 and 1. A reference to place 0 for 3 bytes
    // copies 19 and 20, then the 19 that it wrote at place 2. The flag byte 0x0F marks 4 literals, then a reference.
    std::vector<uint8_t> stream;
    uint8_t value = 1;
    for (const uint8_t flags : std::vector<uint8_t>{0xff, 0xff, 0x0f})
    {
        stream.push_back(flags);
        for (int k = 0; k < (flags == 0xff ? 8 : 4); k++)
        {
            stream.push_back(value++);
        }
    }

    stream.insert(stream.end(), {0x00, 0x00});

    const std::vector<uint8_t> out = rip::DecompressLzss(Lzss(23, stream));

    std::vector<uint8_t> expected;
    for (uint8_t k = 1; k <= 20; k++)
    {
        expected.push_back(k);
    }

    expected.insert(expected.end(), {19, 20, 19});
    Check(out == expected, "the window wraps from place 0xFFF to place 0");
}

void TestDamagedLzss()
{
    std::vector<uint8_t> no_magic = Lzss(9, kAbStream);
    no_magic[3] = 'X';
    Check(!ErrorOf([&no_magic] { rip::DecompressLzss(no_magic); }).empty(), "data without \"LZSS\" is refused");

    const std::vector<uint8_t> short_stream = Lzss(10, {0xff, 'a', 'b', 'c'});
    Check(!ErrorOf([&short_stream] { rip::DecompressLzss(short_stream); }).empty(),
          "a stream that ends before the data's size is refused");

    std::vector<uint8_t> small_stored = Lzss(9, kAbStream);
    xsflib::StoreBe32(small_stored.data() + 8, 8);
    Check(!ErrorOf([&small_stored] { rip::DecompressLzss(small_stored); }).empty(),
          "a stored size smaller than the header is refused");

    const std::vector<uint8_t> too_big = Lzss(1000, {0xff, 'a'});
    Check(!ErrorOf([&too_big] { rip::DecompressLzss(too_big); }).empty(),
          "a size that 2 bytes of stream can't hold is refused");
}

// An archive of two files. The header gives 2 files at 0xC and the information block's offset, 0x40, at 0x18. The block
// gives the table's offset, 0x60. The table gives the entries' offsets, 0x80 and 0xA8. The names are at 0xD0 and 0xDB.
// File 0 is kAbStream compressed, 23 bytes at 0xE8, 9 decompressed. File 1 is "RAW!", stored at 0x100.
std::vector<uint8_t> MakeArchive()
{
    std::vector<uint8_t> archive(0x104);
    const std::string magic = "FSYS", names = std::string("first_song") + '\0' + "second" + '\0';
    std::copy(magic.begin(), magic.end(), archive.begin());
    xsflib::StoreBe32(archive.data() + 0x04, 0x104);
    xsflib::StoreBe32(archive.data() + 0x0c, 2);
    xsflib::StoreBe32(archive.data() + 0x18, 0x40);
    xsflib::StoreBe32(archive.data() + 0x40, 0x60);
    xsflib::StoreBe32(archive.data() + 0x60, 0x80);
    xsflib::StoreBe32(archive.data() + 0x64, 0xa8);
    std::copy(names.begin(), names.end(), archive.begin() + 0xd0);

    const std::vector<uint8_t> compressed = Lzss(9, kAbStream);
    std::copy(compressed.begin(), compressed.end(), archive.begin() + 0xe8);
    xsflib::StoreBe32(archive.data() + 0x80, 0x01e50800);
    xsflib::StoreBe32(archive.data() + 0x84, 0xe8);
    xsflib::StoreBe32(archive.data() + 0x88, 9);
    xsflib::StoreBe32(archive.data() + 0x8c, 0x80000000);
    xsflib::StoreBe32(archive.data() + 0x94, static_cast<uint32_t>(compressed.size()));
    xsflib::StoreBe32(archive.data() + 0xa4, 0xd0);

    const std::string raw = "RAW!";
    std::copy(raw.begin(), raw.end(), archive.begin() + 0x100);
    xsflib::StoreBe32(archive.data() + 0xa8, 0x02ee0800);
    xsflib::StoreBe32(archive.data() + 0xac, 0x100);
    xsflib::StoreBe32(archive.data() + 0xb0, 4);
    xsflib::StoreBe32(archive.data() + 0xbc, 4);
    xsflib::StoreBe32(archive.data() + 0xcc, 0xdb);

    return archive;
}

void TestArchive()
{
    const std::vector<rip::FsysFile> files = rip::ReadFsys(MakeArchive());

    Check(files.size() == 2, "the archive holds 2 files");
    if (files.size() != 2)
    {
        return;
    }

    Check(files[0].id == 0x01e50800 && files[0].name == "first_song", "file 0 has its entry's ID and name");
    Check(files[0].data == std::vector<uint8_t>{'a', 'b', 'a', 'b', 'a', 'b', 0, 0, 0}, "file 0 is decompressed");
    Check(files[1].id == 0x02ee0800 && files[1].name == "second", "file 1 has its entry's ID and name");
    Check(files[1].data == std::vector<uint8_t>{'R', 'A', 'W', '!'}, "file 1 is read as the archive stores it");
}

void TestDamagedArchive()
{
    std::vector<uint8_t> no_magic = MakeArchive();
    no_magic[0] = 'X';
    Check(!ErrorOf([&no_magic] { rip::ReadFsys(no_magic); }).empty(), "an archive without \"FSYS\" is refused");

    std::vector<uint8_t> lost_entry = MakeArchive();
    xsflib::StoreBe32(lost_entry.data() + 0x64, 0x200);
    Check(!ErrorOf([&lost_entry] { rip::ReadFsys(lost_entry); }).empty(), "an entry past the end is refused");

    std::vector<uint8_t> endless_name = MakeArchive();
    xsflib::StoreBe32(endless_name.data() + 0xcc, 0x103);
    Check(!ErrorOf([&endless_name] { rip::ReadFsys(endless_name); }).empty(), "a name without its end is refused");

    std::vector<uint8_t> wrong_size = MakeArchive();
    xsflib::StoreBe32(wrong_size.data() + 0x88, 10);
    Check(!ErrorOf([&wrong_size] { rip::ReadFsys(wrong_size); }).empty(),
          "a compressed file whose size differs from its entry's is refused");
}

} // namespace

int main()
{
    TestLiterals();
    TestReferences();
    TestWrap();
    TestDamagedLzss();
    TestArchive();
    TestDamagedArchive();

    return xsflib::testing::Summary("fsys");
}
