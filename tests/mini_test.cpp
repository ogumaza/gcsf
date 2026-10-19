// SPDX-License-Identifier: MIT

// Unit tests for the .minigcsf's program (rip::BuildMini): the sound's ID, the wave list for a driver that names a
// sound's waves, and a file that replaces one of the library's files.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

#include "gcsf/format.h"
#include "rip/rip.h"

using namespace gcsf;

using xsflib::testing::Check;

namespace
{

// The program chunks of a .minigcsf, and its tags.
struct Mini
{
    ProgramChunks chunks;
    Tags tags;
};

Mini Parse(const std::vector<uint8_t>& file)
{
    Mini mini;
    PsfFile psf;
    XSFLIB_CHECK(!ParsePsf(file, psf));
    XSFLIB_CHECK(!ParseProgram(psf.program, mini.chunks));
    mini.tags = psf.tags;

    return mini;
}

// The words of a MEM chunk's data.
std::vector<uint32_t> Words(const MemChunk& chunk)
{
    std::vector<uint32_t> words;
    for (std::size_t at = 0; at + 4 <= chunk.data.size(); at += 4)
    {
        words.push_back(xsflib::LoadBe32(chunk.data.data() + at));
    }

    return words;
}

void TestSoundOnly()
{
    // A mini for Four Swords Adventures' driver patches the sound's ID alone.
    const Mini mini = Parse(rip::BuildMini(0x01020003, "game.gcsflib", {{"title", "x"}}));

    XSFLIB_CHECK(mini.chunks.memory.size() == 1 && mini.chunks.files.empty());
    XSFLIB_CHECK(mini.chunks.memory[0].address == DriverBlock::kAddress + DriverBlock::kSoundOffset);
    XSFLIB_CHECK(Words(mini.chunks.memory[0]) == std::vector<uint32_t>({0x01020003}));
    XSFLIB_CHECK(mini.tags.at("_lib") == "game.gcsflib" && mini.tags.at("title") == "x");
}

void TestWaves()
{
    // The wave list goes after the sound's handle: its count, then its entries.
    const std::vector<uint32_t> waves = {0x00010002, 0x00300000};
    const Mini mini = Parse(rip::BuildMini(0x80000004, "game.gcsflib", {}, nullptr, &waves));

    XSFLIB_CHECK(mini.chunks.memory.size() == 2);
    XSFLIB_CHECK(mini.chunks.memory[1].address == DriverBlock::kAddress + 0x14);
    XSFLIB_CHECK(Words(mini.chunks.memory[1]) == std::vector<uint32_t>({2, 0x00010002, 0x00300000}));

    // An empty list still holds its count, 0.
    const std::vector<uint32_t> none;
    const Mini empty = Parse(rip::BuildMini(0x80000004, "game.gcsflib", {}, nullptr, &none));

    XSFLIB_CHECK(empty.chunks.memory.size() == 2 && Words(empty.chunks.memory[1]) == std::vector<uint32_t>({0}));

    // A mini names at most DriverBlock::kMaxWaves waves.
    const std::vector<uint32_t> most(DriverBlock::kMaxWaves, 7);

    XSFLIB_CHECK(Words(Parse(rip::BuildMini(1, "game.gcsflib", {}, nullptr, &most)).chunks.memory[1]).size() ==
                 DriverBlock::kMaxWaves + 1);

    const std::vector<uint32_t> too_many(DriverBlock::kMaxWaves + 1, 7);
    bool refused = false;

    try
    {
        rip::BuildMini(1, "game.gcsflib", {}, nullptr, &too_many);
    }
    catch (const std::invalid_argument&)
    {
        refused = true;
    }

    XSFLIB_CHECK(refused);
}

void TestFile()
{
    // A file goes after the sound and the waves, and keeps its path and bytes.
    const FileChunk file = {"Audio/JaiInit.baa", {1, 2, 3}};
    const std::vector<uint32_t> waves = {5};
    const Mini mini = Parse(rip::BuildMini(9, "game.gcsflib", {}, &file, &waves));

    XSFLIB_CHECK(mini.chunks.memory.size() == 2 && mini.chunks.files.size() == 1);
    XSFLIB_CHECK(mini.chunks.files[0].path == file.path && mini.chunks.files[0].data == file.data);
}

void TestNamesWaves()
{
    // Four Swords Adventures' driver loads every wave bank, and Mario Golf's loads whole MusyX groups. The drivers of
    // The Wind Waker, Twilight Princess, Super Mario Sunshine, Donkey Kong Jungle Beat and Pikmin 2 load the waves
    // that a mini names.
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        const bool lists = profile.driver == rip::Driver::kWindWaker ||
                           profile.driver == rip::Driver::kTwilightPrincess ||
                           profile.driver == rip::Driver::kSuperMarioSunshine ||
                           profile.driver == rip::Driver::kJungleBeat || profile.driver == rip::Driver::kPikmin2;
        Check(rip::NamesWaves(profile) == lists, std::string(profile.name) + ": whether its driver names waves");
    }

    rip::GameProfile listed = rip::KnownProfiles().front();
    XSFLIB_CHECK(!rip::NamesWaves(listed));

    listed.driver = rip::Driver::kTwilightPrincess;
    XSFLIB_CHECK(rip::NamesWaves(listed));

    listed.driver = rip::Driver::kMarioGolf;
    XSFLIB_CHECK(!rip::NamesWaves(listed));
}

} // namespace

int main()
{
    TestSoundOnly();
    TestWaves();
    TestFile();
    TestNamesWaves();

    return xsflib::testing::Summary("mini");
}
