// SPDX-License-Identifier: MIT

// Unit tests for the GCSF file format code (src/gcsf/format.*): PSF container, tags, times, program chunks, the process
// descriptor, the driver block's check and _lib loading.

#include "gcsf/format.h"

#include <zlib.h>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "common/bytes.h"

using namespace gcsf;

static void TestTimes()
{
    GCSF_CHECK(ParseTime("") == -1);
    GCSF_CHECK(ParseTime("abc") == -1);
    GCSF_CHECK(ParseTime("5") == 5000);
    GCSF_CHECK(ParseTime("1.5") == 1500);
    GCSF_CHECK(ParseTime("1,25") == 1250);
    GCSF_CHECK(ParseTime("2:03") == 123000);
    GCSF_CHECK(ParseTime("2:03.5") == 123500);
    GCSF_CHECK(ParseTime("1:00:00") == 3600000);
    GCSF_CHECK(ParseTime(" 0:10.000 ") == 10000);
    GCSF_CHECK(ParseTime("1:2:3:4") == -1);
    GCSF_CHECK(ParseTime("-1") == -1);
    GCSF_CHECK(ParseTime("nan") == -1);
    GCSF_CHECK(ParseTime("inf") == -1);
    GCSF_CHECK(ParseTime("1e300") == -1);
    GCSF_CHECK(ParseTime("99999999999999999999:00") == -1);

    GCSF_CHECK(FormatTime(0) == "0:00.000");
    GCSF_CHECK(FormatTime(65998) == "1:05.998");
    GCSF_CHECK(FormatTime(600000) == "10:00.000");

    for (long long ms : {0LL, 1LL, 999LL, 59999LL, 61001LL, 3599999LL})
    {
        GCSF_CHECK(ParseTime(FormatTime(ms)) == ms);
    }
}

// Numbers in tags: an optional sign, digits with '.' or ',' before decimals, and an optional exponent, with the values
// that std::strtod gives in the C locale. Text after the number is left unread.
static void TestDecimals()
{
    for (const char* text : {"0", "0.5", "1,25", "-1.5", "+2", ".5", "5.", "123.456", "0.000123", "2.718281828459045",
                             "1e3", "1.5E-3", "1e22", "1e-22", "9007199254740992", "0.1", "0.3", "62.226"})
    {
        std::string c_text = text;
        std::replace(c_text.begin(), c_text.end(), ',', '.');

        const auto number = ReadDecimal(text);

        Check(number && number->length == std::strlen(text) && number->value == std::strtod(c_text.c_str(), nullptr),
              std::string(text) + " reads as std::strtod reads it");
    }

    const auto with_unit = ReadDecimal("0.5dB");
    const auto bare_exponent = ReadDecimal("1e+x");
    const auto hex = ReadDecimal("0x10");
    const auto huge = ReadDecimal("1e999");
    const auto tiny = ReadDecimal("1e-999");

    GCSF_CHECK(with_unit && with_unit->value == 0.5 && with_unit->length == 3);
    GCSF_CHECK(bare_exponent && bare_exponent->value == 1 && bare_exponent->length == 1);
    GCSF_CHECK(hex && hex->value == 0 && hex->length == 1);
    GCSF_CHECK(huge && std::isinf(huge->value));
    GCSF_CHECK(tiny && tiny->value == 0);
    for (const char* text : {"", ".", "-", "+.", "e5", " 1", "nan", "inf", "x1"})
    {
        Check(!ReadDecimal(text), std::string("\"") + text + "\" isn't a number");
    }
}

// Under a locale that writes decimals with a comma, std::strtod stops at '.'. Tags still read the same. The check runs
// only where such a locale is installed.
static void TestDecimalsUnderCommaLocale()
{
    const char* comma_locale = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "de-DE", "German_Germany.1252"})
    {
        if (std::setlocale(LC_NUMERIC, name) && std::strtod("0.5", nullptr) == 0)
        {
            comma_locale = name;
            break;
        }
    }

    if (!comma_locale)
    {
        std::setlocale(LC_NUMERIC, "C");
        std::printf("no locale with decimal commas is installed. Tags weren't read under one.\n");
        return;
    }

    const auto volume = ReadDecimal("0.5");
    const long long length = ParseTime("1:02.226");
    std::setlocale(LC_NUMERIC, "C");

    Check(volume && volume->value == 0.5, std::string("a volume of 0.5 reads as 0.5 under ") + comma_locale);
    Check(length == 62226, std::string("a length of 1:02.226 reads as 62,226 ms under ") + comma_locale);
}

static void TestPsfRoundTrip()
{
    std::vector<uint8_t> reserved = {1, 2, 3, 4, 5};
    Tags tags = {{"title", "Test"}, {"comment", "line one\nline two"}, {"_lib", "x.gcsflib"}};
    std::vector<uint8_t> program(100000);
    for (std::size_t i = 0; i < program.size(); i++)
    {
        program[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
    }

    const auto file = WritePsf(kPsfVersionGcsf, reserved, program, tags);
    PsfFile psf;
    const auto err = ParsePsf(file, psf);

    GCSF_CHECK(file.size() > 16 && std::memcmp(file.data(), "PSF", 3) == 0 && file[3] == 0x2a);
    GCSF_CHECK(!err);
    GCSF_CHECK(psf.version == kPsfVersionGcsf);
    GCSF_CHECK(psf.reserved == reserved);
    GCSF_CHECK(psf.program == program);
    GCSF_CHECK(psf.tags == tags);

    // Corruption of the compressed program is detected by the CRC.
    auto bad = file;
    bad[16 + reserved.size() + 10] ^= 0x55;

    GCSF_CHECK(ParsePsf(bad, psf) == "program CRC mismatch");

    // Truncation.
    auto cut = file;
    cut.resize(20);

    GCSF_CHECK(ParsePsf(cut, psf) == "truncated PSF file");
    GCSF_CHECK(ParsePsf(std::vector<uint8_t>{'P', 'S'}, psf) == "not a PSF file");

    // A program whose zlib stream stops short, with a size and CRC that match what's left of it.
    const uint32_t half = LoadLe32(&file[8]) / 2;
    std::vector<uint8_t> short_stream(file.begin(), file.begin() + 16 + reserved.size() + half);
    StoreLe32(&short_stream[8], half);
    StoreLe32(&short_stream[12], static_cast<uint32_t>(crc32(0, &short_stream[16 + reserved.size()], half)));

    GCSF_CHECK(ParsePsf(short_stream, psf) == "truncated program data");
}

static void TestTagParsing()
{
    const std::string kHeader = std::string("PSF") + static_cast<char>(kPsfVersionGcsf) + std::string(12, '\0');
    const std::string kTaggedPsf = kHeader +
                                   "[TAG]Title = Hello \nTITLE2=x\ncomment=a\ncomment=b\nartist=me\ncomment=c\n"
                                   "no equals sign\n=empty name\nlength=1:00\r\n";
    std::vector<uint8_t> data(kTaggedPsf.begin(), kTaggedPsf.end());
    PsfFile psf;

    GCSF_CHECK(!ParsePsf(data, psf));
    GCSF_CHECK(psf.tags["title"] == "Hello");
    GCSF_CHECK(psf.tags["title2"] == "x");
    GCSF_CHECK(psf.tags["comment"] == "c"); // a and b join, and the later c replaces them
    GCSF_CHECK(psf.tags["artist"] == "me");
    GCSF_CHECK(psf.tags["length"] == "1:00");
    GCSF_CHECK(psf.tags.count("") == 0);

    // Consecutive lines with the same name join.
    const std::string kRepeatedTagPsf = kHeader + "[TAG]comment=a\ncomment=b\n";
    std::vector<uint8_t> data2(kRepeatedTagPsf.begin(), kRepeatedTagPsf.end());

    GCSF_CHECK(!ParsePsf(data2, psf));
    GCSF_CHECK(psf.tags["comment"] == "a\nb");
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

    GCSF_CHECK(bytes.size() == 20);
    GCSF_CHECK(std::memcmp(bytes.data(), "GCSP", 4) == 0);
    GCSF_CHECK(back.has_value());
    GCSF_CHECK(back->version == d.version && back->entry == d.entry && back->status_address == d.status_address &&
               back->flags == d.flags);

    // Data without the magic, or too short, isn't a descriptor.
    GCSF_CHECK(!ProcessDescriptor::Parse(std::vector<uint8_t>(20, 0)).has_value());
    GCSF_CHECK(!ProcessDescriptor::Parse(std::vector<uint8_t>(8, 0)).has_value());
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

    GCSF_CHECK(b.Data().size() % 4 == 0);
    GCSF_CHECK(!err);
    GCSF_CHECK(mem.size() == 2 && files.size() == 1);
    GCSF_CHECK(mem[0].address == 0x80004000 && mem[0].data == std::vector<uint8_t>(kMemoryBytes, kMemoryBytes + 3));
    GCSF_CHECK(mem[1].address == 0x8170001c && mem[1].data.size() == 4);
    GCSF_CHECK(files[0].path == "AudioRes/a.baa" && files[0].data == (std::vector<uint8_t>{9, 8, 7, 6, 5}));

    // Unknown chunks are skipped.
    std::vector<uint8_t> prog = {'X', 'X', 'X', 'X', 4, 0, 0, 0, 1, 2, 3, 4};
    prog.insert(prog.end(), b.Data().begin(), b.Data().end());
    ProgramChunks skipped;

    GCSF_CHECK(!ParseProgram(prog, skipped));
    GCSF_CHECK(skipped.memory.size() == 2 && skipped.files.size() == 1);

    // A truncated chunk is an error. That holds even when its size is so large that adding it to the position wraps
    // around on a 32-bit host.
    std::vector<uint8_t> trunc = {'M', 'E', 'M', ' ', 100, 0, 0, 0, 1, 2};
    std::vector<uint8_t> huge = {'M', 'E', 'M', ' ', 0xf8, 0xff, 0xff, 0xff, 1, 2, 3, 4};
    ProgramChunks truncated;

    GCSF_CHECK(ParseProgram(trunc, truncated).has_value());
    GCSF_CHECK(ParseProgram(huge, truncated).has_value());
}

// The error LoadSet gives for `path`, or an empty string if the set loads.
static std::string LoadError(const std::string& path, const FileReader& reader)
{
    LoadedSet set;
    const auto err = LoadSet(path, reader, set);

    return err ? *err : std::string();
}

static void TestLoadSet()
{
    // dir/lib.gcsflib holds the descriptor and the base memory. dir/sub/x.minigcsf loads it with _lib=../lib.gcsflib,
    // and loads extra.minigcsf from the same directory with _lib2.
    std::map<std::string, std::vector<uint8_t>> fs;
    ProcessDescriptor d;
    d.status_address = 0x1234;

    {
        ProgramBuilder b;
        b.AddMemory(0x1000, std::vector<uint8_t>{1, 1, 1, 1});
        b.AddFile("f", std::vector<uint8_t>{1});
        fs["dir/lib.gcsflib"] = WritePsf(kPsfVersionGcsf, d.Serialize(), b.Data(), {{"game", "G"}});
    }

    {
        ProgramBuilder b;
        b.AddMemory(0x1000, std::vector<uint8_t>{2});
        fs["dir/sub/x.minigcsf"] =
            WritePsf(kPsfVersionGcsf, {}, b.Data(),
                     {{"_lib", "../lib.gcsflib"}, {"_lib2", "extra.minigcsf"}, {"title", "X"}, {"length", "1:00"}});
    }

    {
        ProgramBuilder b;
        b.AddMemory(0x1000, std::vector<uint8_t>{3});
        fs["dir/sub/extra.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, b.Data(), {{"title", "extra"}});
    }

    const FileReader reader = [&](const std::string& path, std::vector<uint8_t>& out)
    {
        auto it = fs.find(std::filesystem::path(path).lexically_normal().generic_string());
        if (it == fs.end())
        {
            return false;
        }

        out = it->second;

        return true;
    };

    LoadedSet set;
    const auto err = LoadSet("dir/sub/x.minigcsf", reader, set);

    GCSF_CHECK(!err);
    if (err || set.sources.size() != 3 || set.memory.size() != 3)
    {
        std::fprintf(stderr, "LoadSet: %s\n", err ? err->c_str() : "wrong result");
        failures++;
        return;
    }

    GCSF_CHECK(set.sources[0] == "dir/sub/../lib.gcsflib");
    GCSF_CHECK(set.sources[1] == "dir/sub/x.minigcsf");
    GCSF_CHECK(set.sources[2] == "dir/sub/extra.minigcsf");
    GCSF_CHECK(set.memory[0].data[0] == 1 && set.memory[1].data[0] == 2 && set.memory[2].data[0] == 3);
    GCSF_CHECK(set.files.size() == 1);
    GCSF_CHECK(set.descriptor && set.descriptor->status_address == 0x1234);
    GCSF_CHECK(set.tags["title"] == "X" && set.tags["length"] == "1:00" && set.tags.count("game") == 0);

    // An empty _lib2 names no library, like an empty _lib, and _lib3 still loads.
    {
        ProgramBuilder b;
        b.AddMemory(0x1000, std::vector<uint8_t>{4});
        fs["dir/sub/v.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, b.Data(),
                                            {{"_lib", "../lib.gcsflib"}, {"_lib2", ""}, {"_lib3", "extra.minigcsf"}});
    }

    LoadedSet gap;
    GCSF_CHECK(!LoadSet("dir/sub/v.minigcsf", reader, gap));
    GCSF_CHECK(gap.sources.size() == 3 && gap.sources[2] == "dir/sub/extra.minigcsf");

    // A missing library, a wrong PSF version and a set without a descriptor are errors.
    fs["dir/y.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"_lib", "missing.gcsflib"}});
    fs["dir/z.minincsf"] = WritePsf(0x25, {}, {}, {{"_lib", "lib.gcsflib"}});
    fs["dir/w.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"title", "w"}});

    GCSF_CHECK(LoadError("dir/y.minigcsf", reader) == "can't read dir/missing.gcsflib");
    GCSF_CHECK(LoadError("dir/z.minincsf", reader).find("not a GCSF file") != std::string::npos);
    GCSF_CHECK(LoadError("dir/w.minigcsf", reader).starts_with("no process descriptor"));

    // So is a descriptor with an unsupported version.
    ProcessDescriptor v2;
    v2.version = 2;
    fs["dir/v2.gcsflib"] = WritePsf(kPsfVersionGcsf, v2.Serialize(), {}, {});

    GCSF_CHECK(LoadSet("dir/v2.gcsflib", reader, set).has_value());

    // Or flags other than bit 0 alone.
    for (const uint32_t flags : {0u, 3u, 0x80000001u})
    {
        ProcessDescriptor odd;
        odd.flags = flags;
        fs["dir/flags.gcsflib"] = WritePsf(kPsfVersionGcsf, odd.Serialize(), {}, {});
        GCSF_CHECK(LoadSet("dir/flags.gcsflib", reader, set).has_value());
    }

    // A driver block of version 1 plays. A block of another version doesn't. Neither does a block whose version a
    // .minigcsf patches to another. Like the rest of the GameCube's memory, the block's words are big-endian.
    const auto driver_block = [](uint32_t version)
    {
        std::vector<uint8_t> block(0x20, 0);
        StoreBe32(&block[0], DriverBlock::kMagic);
        StoreBe32(&block[DriverBlock::kVersionOffset], version);
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

    GCSF_CHECK(!LoadSet("dir/driver1.gcsflib", reader, set));
    GCSF_CHECK(LoadSet("dir/driver2.gcsflib", reader, set).has_value());
    GCSF_CHECK(LoadSet("dir/patched.minigcsf", reader, set).has_value());

    // A _lib cycle ends with an error instead of recursing forever.
    fs["dir/c1.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"_lib", "c2.minigcsf"}});
    fs["dir/c2.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"_lib", "c1.minigcsf"}});

    GCSF_CHECK(LoadError("dir/c1.minigcsf", reader) == "_lib nesting too deep");

    // So does a set that names more files than a set can have. Each of these names the next one ten times. Loading them
    // all would take 10^10 loads.
    for (int level = 0; level < 10; level++)
    {
        Tags tags;
        for (int n = 2; n <= 11; n++)
        {
            tags["_lib" + std::to_string(n)] = "f" + std::to_string(level + 1) + ".minigcsf";
        }

        fs["dir/f" + std::to_string(level) + ".minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, tags);
    }

    fs["dir/f10.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {});

    GCSF_CHECK(LoadError("dir/f0.minigcsf", reader) == "more than 64 files in the set");
}

static void TestLoadSetInArchive()
{
    // foobar2000's path for a file in an archive: unpack://zip|<length>|<the archive's path>|<its path in the archive>.
    const std::string archive = "unpack://zip|23|file://C:\\music\\set.zip|";
    std::map<std::string, std::vector<uint8_t>> fs;
    fs[archive + "set.gcsflib"] = WritePsf(kPsfVersionGcsf, ProcessDescriptor{}.Serialize(), {}, {});
    fs[archive + "x.minigcsf"] = WritePsf(kPsfVersionGcsf, {}, {}, {{"_lib", "set.gcsflib"}});

    const FileReader reader = [&](const std::string& path, std::vector<uint8_t>& out)
    {
        auto it = fs.find(path);
        if (it == fs.end())
        {
            return false;
        }

        out = it->second;

        return true;
    };

    LoadedSet set;
    const auto err = LoadSet(archive + "x.minigcsf", reader, set, "/\\|");
    const std::string without = LoadError(archive + "x.minigcsf", reader);

    // With | as a separator, the library is found in the archive. Without it, the library is found beside the archive.
    GCSF_CHECK(!err && set.sources.size() == 2 && set.sources[0] == archive + "set.gcsflib");
    GCSF_CHECK(without == "can't read unpack://zip|23|file://C:\\music\\set.gcsflib");
}

int main()
{
    TestTimes();
    TestDecimals();
    TestDecimalsUnderCommaLocale();
    TestPsfRoundTrip();
    TestTagParsing();
    TestDescriptor();
    TestChunks();
    TestLoadSet();
    TestLoadSetInArchive();

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    std::printf("format tests passed\n");

    return 0;
}
