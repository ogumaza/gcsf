// SPDX-License-Identifier: MIT

// Unit tests for the GCSF file format code (src/gcsf/format.*): the version byte, program chunks, the process
// descriptor, the driver block's check, _lib loading, the loading order and the port list. xsflib's psf_test tests the
// PSF container, its tags and its times.

#include "gcsf/format.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

using namespace gcsf;
using xsflib::testing::LoadError;

static void TestVersionByte()
{
    // GCSF's PSF version byte is 0x2A (docs/gcsf.md, "Version byte").
    const auto file = WritePsf(kPsfVersionGcsf, {}, {}, {});

    XSFLIB_CHECK(file.size() >= 16 && std::memcmp(file.data(), "PSF", 3) == 0 && file[3] == 0x2a);
}

static void TestDescriptor()
{
    ProcessDescriptor d;
    d.version = 2;
    d.entry = 0x80004000;
    d.status_address = 0x81700018;
    d.flags = 3;

    const auto bytes = d.Serialize();
    const auto back = ProcessDescriptor::Parse(bytes);

    XSFLIB_CHECK(bytes.size() == 20);
    XSFLIB_CHECK(std::memcmp(bytes.data(), "GCSP", 4) == 0);
    XSFLIB_CHECK(back.has_value());
    XSFLIB_CHECK(back->version == d.version && back->entry == d.entry && back->status_address == d.status_address &&
                 back->flags == d.flags);

    // Data without the magic, or too short, isn't a descriptor.
    XSFLIB_CHECK(!ProcessDescriptor::Parse(std::vector<uint8_t>(20, 0)).has_value());
    XSFLIB_CHECK(!ProcessDescriptor::Parse(std::vector<uint8_t>(8, 0)).has_value());
}

static void TestChunks()
{
    ProgramBuilder b;
    constexpr uint8_t kMemoryBytes[] = {1, 2, 3};
    b.AddMemory(0x80004000, kMemoryBytes, sizeof(kMemoryBytes));
    b.AddFile("AudioRes/a.baa", std::vector<uint8_t>{9, 8, 7, 6, 5});
    b.AddMemory(0x8170001c, std::vector<uint8_t>{0, 0, 0, 1});

    ProgramChunks chunks;
    const auto err = ParseProgram(b.Data(), chunks);
    const auto& mem = chunks.memory;
    const auto& files = chunks.files;

    XSFLIB_CHECK(b.Data().size() % 4 == 0);
    XSFLIB_CHECK(!err);
    XSFLIB_CHECK(mem.size() == 2 && files.size() == 1);
    XSFLIB_CHECK(mem[0].address == 0x80004000 && mem[0].data == std::vector<uint8_t>(kMemoryBytes, kMemoryBytes + 3));
    XSFLIB_CHECK(mem[1].address == 0x8170001c && mem[1].data.size() == 4);
    XSFLIB_CHECK(files[0].path == "AudioRes/a.baa" && files[0].data == (std::vector<uint8_t>{9, 8, 7, 6, 5}));

    // Unknown chunks are skipped.
    std::vector<uint8_t> prog = {'X', 'X', 'X', 'X', 4, 0, 0, 0, 1, 2, 3, 4};
    prog.insert(prog.end(), b.Data().begin(), b.Data().end());
    ProgramChunks skipped;

    XSFLIB_CHECK(!ParseProgram(prog, skipped));
    XSFLIB_CHECK(skipped.memory.size() == 2 && skipped.files.size() == 1);

    // A truncated chunk is an error. That holds even when its size is so large that adding it to the position wraps
    // around on a 32-bit host.
    std::vector<uint8_t> trunc = {'M', 'E', 'M', ' ', 100, 0, 0, 0, 1, 2};
    std::vector<uint8_t> huge = {'M', 'E', 'M', ' ', 0xf8, 0xff, 0xff, 0xff, 1, 2, 3, 4};
    ProgramChunks truncated;

    XSFLIB_CHECK(ParseProgram(trunc, truncated).has_value());
    XSFLIB_CHECK(ParseProgram(huge, truncated).has_value());
}

static void TestLoadSet()
{
    // A .minigcsf and its .gcsflib, with the library's process descriptor. xsflib's psf_set_test covers the loader's
    // other rules: the order and paths of a set's files, its tags, a set in an archive, and the errors of a missing
    // library, a _lib cycle and a set of too many files.
    std::map<std::string, std::vector<uint8_t>> fs;
    const FileReader reader = xsflib::testing::MapReader(fs);
    ProcessDescriptor d;
    d.status_address = 0x1234;
    fs["dir/lib.gcsflib"] = WritePsf(kPsfVersionGcsf, d.Serialize(), {}, {});
    fs["dir/x.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"_lib", "lib.gcsflib"}});
    LoadedSet set;

    XSFLIB_CHECK(!LoadSet("dir/x.minigcsf", reader, set));
    XSFLIB_CHECK(set.descriptor && set.descriptor->status_address == 0x1234);

    // Another format's file and a set without a process descriptor are errors.
    fs["dir/z.minincsf"] = WritePsf(0x25, {}, {}, {{"_lib", "lib.gcsflib"}});
    fs["dir/w.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"title", "w"}});

    XSFLIB_CHECK(LoadError<SetFormat>("dir/z.minincsf", reader).find("not a GCSF file") != std::string::npos);
    XSFLIB_CHECK(LoadError<SetFormat>("dir/w.minigcsf", reader).starts_with("no process descriptor"));

    // So is a descriptor with an unsupported version.
    ProcessDescriptor v2;
    v2.version = 2;
    fs["dir/v2.gcsflib"] = WritePsf(kPsfVersionGcsf, v2.Serialize(), {}, {});

    XSFLIB_CHECK(LoadSet("dir/v2.gcsflib", reader, set).has_value());

    // Or flags other than bit 0 alone.
    for (const uint32_t flags : {0u, 3u, 0x80000001u})
    {
        ProcessDescriptor odd;
        odd.flags = flags;
        fs["dir/flags.gcsflib"] = WritePsf(kPsfVersionGcsf, odd.Serialize(), {}, {});
        XSFLIB_CHECK(LoadSet("dir/flags.gcsflib", reader, set).has_value());
    }

    // A driver block of version 1 plays. A block of another version doesn't. Neither does a block whose version a
    // .minigcsf patches to another. Like the rest of the GameCube's memory, the block's words are big-endian.
    const auto driver_block = [](uint32_t version)
    {
        std::vector<uint8_t> block(0x20, 0);
        xsflib::StoreBe32(&block[0], DriverBlock::kMagic);
        xsflib::StoreBe32(&block[DriverBlock::kVersionOffset], version);
        ProgramBuilder b;
        b.AddMemory(DriverBlock::kAddress, block);

        return b.Data();
    };

    fs["dir/driver1.gcsflib"] = WritePsf(kPsfVersionGcsf, d.Serialize(), driver_block(1), {});
    fs["dir/driver2.gcsflib"] = WritePsf(kPsfVersionGcsf, d.Serialize(), driver_block(2), {});
    {
        ProgramBuilder b;
        b.AddMemory(DriverBlock::kAddress + DriverBlock::kVersionOffset, std::vector<uint8_t>{0, 0, 0, 2});
        fs["dir/patched.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, b.Data(), {{"_lib", "driver1.gcsflib"}});
    }

    XSFLIB_CHECK(!LoadSet("dir/driver1.gcsflib", reader, set));
    XSFLIB_CHECK(LoadSet("dir/driver2.gcsflib", reader, set).has_value());
    XSFLIB_CHECK(LoadSet("dir/patched.minigcsf", reader, set).has_value());
}

static void TestLoadingOrder()
{
    // A set's chunks apply in loading order: a file's _lib, then the file, then its _lib2, _lib3 and so on. An empty
    // _libN names no file (docs/gcsf.md, "Loading order"). xsflib's psf_set_test checks these rules with its test
    // formats. This case checks them with GCSF's. dir/lib.gcsflib holds the descriptor and the base memory.
    // dir/sub/x.minigcsf loads it with _lib=../lib.gcsflib, and loads extra.minigcsf from its directory with _lib2.
    std::map<std::string, std::vector<uint8_t>> fs;
    const FileReader reader = xsflib::testing::MapReader(fs);
    ProcessDescriptor d;
    d.status_address = 0x1234;
    {
        ProgramBuilder b;
        b.AddMemory(0x80004000, std::vector<uint8_t>{1, 1, 1, 1});
        b.AddFile("f", std::vector<uint8_t>{1});
        fs["dir/lib.gcsflib"] = WritePsf(kPsfVersionGcsf, d.Serialize(), b.Data(), {{"game", "G"}});
    }
    {
        ProgramBuilder b;
        b.AddMemory(0x80004000, std::vector<uint8_t>{2});
        fs["dir/sub/x.minigcsf"] =
            WritePsf(kPsfVersionGcsf, {}, b.Data(),
                     {{"_lib", "../lib.gcsflib"}, {"_lib2", "extra.minigcsf"}, {"title", "X"}, {"length", "1:00"}});
    }
    {
        ProgramBuilder b;
        b.AddMemory(0x80004000, std::vector<uint8_t>{3});
        fs["dir/sub/extra.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, b.Data(), {{"title", "extra"}});
    }

    LoadedSet set;
    const auto err = LoadSet("dir/sub/x.minigcsf", reader, set);

    XSFLIB_CHECK(!err);
    XSFLIB_CHECK(set.sources ==
                 std::vector<std::string>({"dir/sub/../lib.gcsflib", "dir/sub/x.minigcsf", "dir/sub/extra.minigcsf"}));
    XSFLIB_CHECK(set.memory.size() == 3 && set.memory[0].data[0] == 1 && set.memory[1].data[0] == 2 &&
                 set.memory[2].data[0] == 3);
    XSFLIB_CHECK(set.files.size() == 1);
    XSFLIB_CHECK(set.descriptor && set.descriptor->status_address == 0x1234);
    XSFLIB_CHECK(set.tags["title"] == "X" && set.tags["length"] == "1:00" && set.tags.count("game") == 0);

    // An empty _lib2 names no file, like an empty _lib, and _lib3 still loads.
    {
        ProgramBuilder b;
        b.AddMemory(0x80004000, std::vector<uint8_t>{4});
        fs["dir/sub/v.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, b.Data(),
                                            {{"_lib", "../lib.gcsflib"}, {"_lib2", ""}, {"_lib3", "extra.minigcsf"}});
    }
    LoadedSet gap;

    XSFLIB_CHECK(!LoadSet("dir/sub/v.minigcsf", reader, gap));
    XSFLIB_CHECK(gap.sources.size() == 3 && gap.sources[2] == "dir/sub/extra.minigcsf");
}

static void TestPortList()
{
    // The gcsf_var tag takes the form of 3SF's 3sf_var, with ports 0 to 15 (docs/gcsf.md, "Tags").
    const auto ports = PortList::Parse("9=0x7f, 6=4\n6=-1");

    XSFLIB_CHECK(ports && *ports == xsflib::psf::Variables({{6, -1}, {9, 127}}));
    XSFLIB_CHECK(PortList::Parse("15=1") && !PortList::Parse("16=1") && !PortList::Parse("6") && !PortList::Parse("") &&
                 !PortList::Parse("6=32768"));

    // The list holds the number of ports, then an entry for each: the port in the high half and its value in the low
    // half. The rest of the list's 16 entries stays 0.
    const std::vector<uint8_t> list = PortList::Data(*ports);

    XSFLIB_CHECK(list.size() == PortList::kSize && PortList::kSize == 68);
    XSFLIB_CHECK(xsflib::LoadBe32(&list[0]) == 2 && xsflib::LoadBe32(&list[4]) == 0x0006ffff &&
                 xsflib::LoadBe32(&list[8]) == 0x0009007f && xsflib::LoadBe32(&list[12]) == 0);
}

int main()
{
    TestVersionByte();
    TestDescriptor();
    TestChunks();
    TestLoadSet();
    TestLoadingOrder();
    TestPortList();

    return xsflib::testing::Summary("format");
}
