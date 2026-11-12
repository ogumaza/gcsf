// SPDX-License-Identifier: MIT

// The ripper's parts for games whose MusyX is under Hudson's sound manager: the songs of the game's sound test, with
// their names from the game's messages, the effects of the sound manager's file, and the library's sound files. The
// length analysis is MusyX's (MeasureMusyXSound).

#include "ntdlib/musyx/msm.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "xsflib/binary.h"

#include "gcsf/format.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{
namespace
{

// A stream's sound ID: this bit and the stream's number.
constexpr uint32_t kStreamBit = 0x80000000;

// The offset in the stream file's header of the offset at which the streams' samples start.
constexpr std::size_t kSampleOffset = 0x1c;

// An effect's sound ID: the group to load in the top 16 bits, with kOneGroup, or kNoGroupSet for none, then kEffectBit
// and the effect's number (driver/mario_party.c).
constexpr uint32_t kNoGroupSet = 0xffff, kOneGroup = 0x8000, kEffectBit = 0x8000;

// The effects of the sound manager's file `msm` at `path`, in the order of its table of effects (see MsmSounds).
// `names` gives the names of some effects by their numbers. An effect plays from the first group of the table with its
// group's MusyX ID. An effect whose group the table doesn't have is left out. Throws xsflib::FormatError if a table is
// outside the file.
std::vector<Sound> MsmEffects(std::span<const uint8_t> msm, const std::string& path,
                              const std::map<uint32_t, std::string>& names)
{
    const ntdlib::musyx::MsmFile file(msm, path);
    const std::vector<std::size_t> base = file.BaseGroupIndexes();
    std::vector<Sound> sounds;
    for (const ntdlib::musyx::MsmEffect& effect : file.Effects())
    {
        const std::optional<std::size_t> group = file.GroupIndex(effect.group_id);
        if (!group)
        {
            continue;
        }

        const bool in_base = std::find(base.begin(), base.end(), *group) != base.end();
        const uint32_t load = in_base ? kNoGroupSet : kOneGroup | static_cast<uint32_t>(*group);
        const auto name = names.find(effect.number);
        std::string label = "se-" + ZeroPadded(effect.number, 4);
        if (name != names.end() && !name->second.empty())
        {
            label += " " + name->second;
        }

        sounds.push_back({load << 16 | kEffectBit | effect.number, std::move(label), false});
    }

    return sounds;
}

} // namespace

std::vector<Sound> MsmSounds(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<uint8_t> module = DiscFile(game, profile.sound_test_module);
    const std::vector<uint8_t> messages = DiscFile(game, profile.messages);
    const bool japanese = profile.japanese_messages;

    std::vector<Sound> sounds;
    for (ntdlib::musyx::SoundTestEntry& entry :
         ntdlib::musyx::ReadSoundTest(module, profile.sound_test_module, profile.sound_test, profile.sound_test_count,
                                      messages, profile.messages, japanese))
    {
        if (entry.kind == ntdlib::musyx::kSoundTestStream)
        {
            sounds.push_back({kStreamBit | entry.number, std::move(entry.name), true, true});
        }
        else
        {
            sounds.push_back({uint32_t{entry.group_set} << 16 | (entry.number & 0xffff), std::move(entry.name), true});
        }
    }

    // The list of voices names effects by their numbers.
    const std::map<uint32_t, std::string> names =
        ntdlib::musyx::ReadSoundTestEffects(module, profile.sound_test_module, profile.sound_test_voices,
                                            profile.sound_test_voice_count, messages, profile.messages, japanese);
    const std::string msm = profile.bank_files[0];
    std::vector<Sound> effects = MsmEffects(DiscFile(game, msm), msm, names);
    sounds.insert(sounds.end(), std::make_move_iterator(effects.begin()), std::make_move_iterator(effects.end()));

    return sounds;
}

std::optional<std::string> AddMsmFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<std::string> banks(profile.bank_files.begin(), profile.bank_files.end());
    if (const std::optional<std::string> problem = AddDiscFiles(program, game, banks))
    {
        return problem;
    }

    try
    {
        std::vector<uint8_t> streams = DiscFile(game, profile.stream_file);
        const xsflib::Reader r(streams, true);
        const uint32_t samples = r.U32(kSampleOffset);
        if (samples > streams.size())
        {
            return std::string(profile.stream_file) + "'s samples start past the file's end";
        }

        streams.resize(samples);
        program.AddFile(profile.stream_file, streams);
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.stream_file) + ": " + e.what();
    }

    return std::nullopt;
}

} // namespace gcsf::rip
