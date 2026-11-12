// SPDX-License-Identifier: MIT

// Unit tests for the .minigcsf's program (rip::BuildMini): the sound's ID, the wave list for a driver that names a
// sound's waves, a file that replaces one of the library's files, the ports of a variant of an effect, the states of
// pieces of music that the profiles list, and the children of track 0 that Twilight Princess's driver mutes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ntdlib/flipper/z2_params.h"
#include "ntdlib/jaudio/jungle_beat_states.h"
#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"
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
    const std::vector<uint32_t> kWaves = {0x00010002, 0x00300000};
    const Mini mini = Parse(rip::BuildMini(0x80000004, "game.gcsflib", {}, nullptr, &kWaves));

    XSFLIB_CHECK(mini.chunks.memory.size() == 2);
    XSFLIB_CHECK(mini.chunks.memory[1].address == DriverBlock::kAddress + 0x14);
    XSFLIB_CHECK(Words(mini.chunks.memory[1]) == std::vector<uint32_t>({2, 0x00010002, 0x00300000}));

    // An empty list still holds its count, 0.
    const std::vector<uint32_t> kNone;
    const Mini empty = Parse(rip::BuildMini(0x80000004, "game.gcsflib", {}, nullptr, &kNone));

    XSFLIB_CHECK(empty.chunks.memory.size() == 2 && Words(empty.chunks.memory[1]) == std::vector<uint32_t>({0}));

    // A mini names at most DriverBlock::kMaxWaves waves.
    const std::vector<uint32_t> kMost(DriverBlock::kMaxWaves, 7);

    XSFLIB_CHECK(Words(Parse(rip::BuildMini(1, "game.gcsflib", {}, nullptr, &kMost)).chunks.memory[1]).size() ==
                 DriverBlock::kMaxWaves + 1);

    const std::vector<uint32_t> kTooMany(DriverBlock::kMaxWaves + 1, 7);
    bool refused = false;

    try
    {
        rip::BuildMini(1, "game.gcsflib", {}, nullptr, &kTooMany);
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
    const FileChunk kFile = {"Audio/JaiInit.baa", {1, 2, 3}};
    const std::vector<uint32_t> kWaves = {5};
    const Mini mini = Parse(rip::BuildMini(9, "game.gcsflib", {}, &kFile, &kWaves));

    XSFLIB_CHECK(mini.chunks.memory.size() == 2 && mini.chunks.files.size() == 1);
    XSFLIB_CHECK(mini.chunks.files[0].path == kFile.path && mini.chunks.files[0].data == kFile.data);
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

void TestPorts()
{
    // A variant of an effect that reads a port gets its ports in the gcsf_var tag. Its program is the effect's.
    const xsflib::psf::Variables kPorts = {{9, 40}, {6, 4}};
    const std::vector<uint32_t> kWaves = {5};
    const Mini mini = Parse(rip::BuildMini(0x3800 | 0x800, "game.gcsflib", {}, nullptr, &kWaves, &kPorts));

    XSFLIB_CHECK(mini.tags.at(kVariablesTag) == "6=4, 9=40");
    XSFLIB_CHECK(mini.chunks.memory.size() == 2 && Words(mini.chunks.memory[1]) == std::vector<uint32_t>({1, 5}));

    // Without ports, or with none, a mini has no gcsf_var tag.
    const xsflib::psf::Variables kNone;

    XSFLIB_CHECK(Parse(rip::BuildMini(1, "game.gcsflib", {}, nullptr, &kWaves, &kNone)).tags.count(kVariablesTag) == 0);
    XSFLIB_CHECK(Parse(rip::BuildMini(1, "game.gcsflib", {})).tags.count(kVariablesTag) == 0);
}

void TestPortVariants()
{
    // The Wind Waker's and Pikmin's profiles list the effects whose variants a rip holds: each a port and the lowest
    // value of each variant, in increasing order.
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        for (const rip::PortEffect& effect : profile.port_effects)
        {
            Check(profile.driver == rip::Driver::kWindWaker || profile.driver == rip::Driver::kPikmin,
                  std::string(profile.name) + ": a driver with a port list");
            Check(effect.port >= 0 && effect.port < static_cast<int>(PortList::kMaxPorts) && !effect.values.empty() &&
                      std::is_sorted(effect.values.begin(), effect.values.end()) &&
                      std::adjacent_find(effect.values.begin(), effect.values.end()) == effect.values.end(),
                  std::string(profile.name) + ": the port and the values of effect " + std::to_string(effect.id));
        }
    }
}

void TestMusicStates()
{
    // Only The Wind Waker's profile lists states of pieces of music. Each state is of a piece, 0x80000000 plus an index
    // below 128. Its value is the piece's ID with state bits from 16 to 19, or another of the game's IDs from
    // 0x80000105 to 0x80000152 without them. Bits 20 to 29 stay clear. A piece's states have different names.
    constexpr uint32_t kStateBits = rip::kSailing | rip::kOnBoard | rip::kSwordOut | rip::kEnemyNear;
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        if (!profile.music_states.empty())
        {
            Check(profile.driver == rip::Driver::kWindWaker, std::string(profile.name) + ": a driver with starters");
        }

        for (std::size_t i = 0; i < profile.music_states.size(); i++)
        {
            const rip::MusicState& state = profile.music_states[i];
            const uint32_t id = state.id & ~kStateBits;
            const bool by_state = id == state.piece && (state.id & kStateBits) != 0;
            const bool by_id = state.id == id && id >= 0x80000105 && id <= 0x80000152;
            Check(state.piece >= 0x80000000 && state.piece < 0x80000080 && (by_state || by_id) &&
                      (state.id & 0x3ff00000) == 0 && state.name && *state.name,
                  std::string(profile.name) + ": state " + std::to_string(i));
            for (std::size_t j = 0; j < i; j++)
            {
                Check(profile.music_states[j].piece != state.piece ||
                          std::string(profile.music_states[j].name) != state.name,
                      std::string(profile.name) + ": the name of state " + std::to_string(i));
            }
        }
    }
}

void TestLayerStates()
{
    // Only Donkey Kong Jungle Beat's profile lists layer states. Each belongs to a piece of music, 0x01000000 plus an
    // index below 0x100. Each sets only the state's bits: a combo's layer of 1 or 2, a state of the second table from
    // 1 to 7, or a flight with a Helibird. A piece's states have different bits and different suffixes.
    constexpr uint32_t kStateBits =
        3u << ntdlib::jaudio::kComboLayerShift | 7u << ntdlib::jaudio::kTableStateShift | ntdlib::jaudio::kHelibird;
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        if (!profile.layer_states.empty())
        {
            Check(profile.driver == rip::Driver::kJungleBeat, std::string(profile.name) + ": the driver of layers");
        }

        for (std::size_t i = 0; i < profile.layer_states.size(); i++)
        {
            const ntdlib::jaudio::JungleBeatState& state = profile.layer_states[i];
            const uint32_t layer = state.bits >> ntdlib::jaudio::kComboLayerShift & 3;
            Check((state.piece & 0xffffff00) == 0x01000000 && state.bits != 0 && (state.bits & ~kStateBits) == 0 &&
                      layer != 3 && state.suffix && *state.suffix,
                  std::string(profile.name) + ": layer state " + std::to_string(i));
            for (std::size_t j = 0; j < i; j++)
            {
                Check(profile.layer_states[j].piece != state.piece ||
                          (profile.layer_states[j].bits != state.bits &&
                           std::string(profile.layer_states[j].suffix) != state.suffix),
                      std::string(profile.name) + ": the bits and the suffix of layer state " + std::to_string(i));
            }
        }
    }
}

void TestChordLayers()
{
    // Only Donkey Kong Jungle Beat's profile lists chord layers, at most as many as the driver's table holds. Each
    // belongs to a piece of music, 0x01000000 plus an index below 0x100, and names a stage and one or two records of the
    // chord database below 0x100. A piece's layers have different suffixes, and none is the suffix of a layer state.
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        if (profile.chord_layers.empty())
        {
            continue;
        }

        Check(profile.driver == rip::Driver::kJungleBeat, std::string(profile.name) + ": the driver of chord layers");
        Check(profile.chord_layers.size() <= ntdlib::jaudio::kMaxChordLayers,
              std::string(profile.name) + ": the number of chord layers");
        for (std::size_t i = 0; i < profile.chord_layers.size(); i++)
        {
            const ntdlib::jaudio::JungleBeatChordLayer& layer = profile.chord_layers[i];
            const std::string name = std::string(profile.name) + ": chord layer " + std::to_string(i);
            Check((layer.piece & 0xffffff00) == 0x01000000 && layer.stage != 0 && layer.stage < 0x100 &&
                      layer.record != 0 && layer.record < 0x100 && layer.second < 0x100 && layer.suffix &&
                      *layer.suffix,
                  name);

            // The sound ID gives the index back, in bits 16 to 22 and bit 15, and the piece's ID.
            const uint32_t id = ntdlib::jaudio::ChordLayerId(layer.piece, static_cast<uint32_t>(i));
            Check(
                ((id >> 16 & 0x7f) | (id >> 8 & 0x80)) == i &&
                    (id & ~(ntdlib::jaudio::kChordLayer | 0x7f0000u | ntdlib::jaudio::kChordLayerHigh)) == layer.piece,
                name + ": its sound ID");
            for (std::size_t j = 0; j < i; j++)
            {
                Check(profile.chord_layers[j].piece != layer.piece ||
                          std::string(profile.chord_layers[j].suffix) != layer.suffix,
                      name + ": its suffix");
            }

            for (const ntdlib::jaudio::JungleBeatState& state : profile.layer_states)
            {
                Check(state.piece != layer.piece || std::string(state.suffix) != layer.suffix,
                      name + ": its suffix and the states'");
            }
        }
    }
}

void TestMutedChildren()
{
    // A piece whose tracks Twilight Princess mutes as it starts it gets the children after the profile, at 0x1FC of the
    // block. A mini with no state has no such chunk.
    const std::vector<uint32_t> kWaves = {0x00010000};
    const ntdlib::flipper::z2::PieceState kMuted = {.muted_children = 0x0fe0};
    const Mini mini = Parse(rip::BuildMini(0x01000000, "game.gcsflib", {}, nullptr, &kWaves, nullptr, &kMuted));

    XSFLIB_CHECK(mini.chunks.memory.size() == 3);
    XSFLIB_CHECK(mini.chunks.memory[2].address == DriverBlock::kAddress + 0x1fc);
    XSFLIB_CHECK(Words(mini.chunks.memory[2]) == std::vector<uint32_t>({0x0fe0}));
    XSFLIB_CHECK(Parse(rip::BuildMini(0x01000000, "game.gcsflib", {}, nullptr, &kWaves)).chunks.memory.size() == 2);
    const ntdlib::flipper::z2::PieceState kNone;
    XSFLIB_CHECK(
        Parse(rip::BuildMini(0x01000000, "game.gcsflib", {}, nullptr, &kWaves, nullptr, &kNone)).chunks.memory.size() ==
        2);

    // A state with children at a volume between 0 and 1 and a status: the muted children, the partial children, the
    // 16 volumes as floats' bits, then the status with its flag.
    const ntdlib::flipper::z2::PieceState kState = {.muted_children = 0x0100,
                                                    .partial_children = 0x0003,
                                                    .partial_volumes = {0.5f, 0.25f},
                                                    .music_status = 0x10001};
    const Mini state = Parse(rip::BuildMini(0x01000005, "game.gcsflib", {}, nullptr, &kWaves, nullptr, &kState));
    std::vector<uint32_t> state_words(19, 0);
    state_words[0] = 0x0100;
    state_words[1] = 0x0003;
    state_words[2] = 0x3f000000;
    state_words[3] = 0x3e800000;
    state_words[18] = 0x10001;
    XSFLIB_CHECK(state.chunks.memory.size() == 3);
    XSFLIB_CHECK(state.chunks.memory[2].address == DriverBlock::kAddress + 0x1fc);
    XSFLIB_CHECK(Words(state.chunks.memory[2]) == state_words);

    // The profiles that set tracks run Twilight Princess's driver, and name the music manager, setChildTrackVolume
    // and setPortData. The profiles with states also mute tracks.
    for (const rip::GameProfile& profile : rip::KnownProfiles())
    {
        Check(profile.piece_states.empty() || !profile.muted_starts.empty(),
              std::string(profile.name) + ": the states and the start mutes");
        if (profile.muted_starts.empty())
        {
            continue;
        }

        // The four are the profile's last words.
        const std::size_t kWords = sizeof(ntdlib::flipper::z2::Profile) / 4;
        Check(profile.driver == rip::Driver::kTwilightPrincess && profile.params.size() == kWords &&
                  profile.params[kWords - 4] != 0 && profile.params[kWords - 3] != 0 &&
                  profile.params[kWords - 2] != 0 && profile.params[kWords - 1] != 0,
              std::string(profile.name) + ": the music manager, setChildTrackVolume, setPortData and getPortData");
    }
}

} // namespace

int main()
{
    TestSoundOnly();
    TestWaves();
    TestFile();
    TestNamesWaves();
    TestPorts();
    TestPortVariants();
    TestMusicStates();
    TestLayerStates();
    TestChordLayers();
    TestMutedChildren();

    return xsflib::testing::Summary("mini");
}
