// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is HAL's: the effects, the library's sound files, and the length
// analysis of an effect.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/binary.h"

#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// The names that a HAL game's C header defines for its effects' IDs (see GameProfile::sound_names), by ID. A line
// "#define NAME ((GROUP<<16) + NUMBER)" names an effect. The header's comments are Shift-JIS. Its names are ASCII.
std::map<uint32_t, std::string> HalNames(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> names;
    std::vector<uint8_t> header;
    if (!profile.sound_names || !game.read_file || !game.read_file(profile.sound_names, header))
    {
        return names;
    }

    const std::string text(header.begin(), header.end());
    std::size_t line = 0;
    while (line < text.size())
    {
        const std::size_t end = std::min(text.find('\n', line), text.size());
        unsigned group = 0;
        unsigned number = 0;
        char name[64] = {};
        const std::string row = text.substr(line, end - line);
        if (std::sscanf(row.c_str(), "#define %63s ((%u<<16) + %u)", name, &group, &number) == 3 && group <= 0xffff &&
            number <= 0xffff)
        {
            names.emplace(group << 16 | number, name);
        }

        line = end + 1;
    }

    return names;
}

} // namespace

std::vector<Sound> HalEffects(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<uint8_t> scripts = ReadGameFile(game, profile.sound_archive);
    const xsflib::Reader r(scripts, true);
    uint32_t at = 0;
    for (int table = 0; table < 2; table++)
    {
        at += 4 + 4 * r.U32(at);
    }

    const uint32_t groups = r.U32(at);
    std::vector<uint32_t> firsts;
    for (uint32_t group = 0; group < groups; group++)
    {
        firsts.push_back(r.U32(at + 4 + 4 * group));
    }

    const uint32_t effects = r.U32(at + 4 + 4 * groups);
    const std::map<uint32_t, std::string> names = HalNames(game, profile);
    std::vector<Sound> sounds;
    for (uint32_t group = 0; group < groups; group++)
    {
        const uint32_t end = group + 1 < groups ? firsts[group + 1] : effects;
        if (end < firsts[group] || end > effects || end - firsts[group] > 0x10000)
        {
            throw xsflib::FormatError(std::string(profile.sound_archive) + "'s table of groups is damaged");
        }

        for (uint32_t place = 0; place < end - firsts[group]; place++)
        {
            const uint32_t id = group << 16 | place;
            const auto name = names.find(id);
            sounds.push_back(
                {id, name != names.end() ? name->second : "se" + std::to_string(group) + "-" + ZeroPadded(place, 3),
                 false});
        }
    }

    return sounds;
}

std::optional<std::string> AddHalFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    std::vector<std::string> paths = {profile.sound_archive};
    paths.insert(paths.end(), profile.other_sound_files.begin(), profile.other_sound_files.end());

    return AddDiscFiles(program, game, paths);
}

namespace
{

// An effect of a HAL game (see MeasuredSound). It plays while the driver's handle holds the effect's entry in the sound
// library's table. The driver clears the handle once the sound library has freed the entry. The sound library frees
// it once the effect's script has ended and its voices have stopped. The output rests at 0. The analysis follows no
// loops of the scripts. The Bonus Disc's effects have none.
class HalEffect : public MeasuredSound
{
public:
    explicit HalEffect(Player& player) : MeasuredSound(player, true)
    {
    }

private:
    bool Playing() override
    {
        return Word(DriverBlock::kAddress + DriverBlock::kHandleOffset) != 0;
    }

    bool Looped() override
    {
        return false;
    }
};

} // namespace

std::optional<Timing> MeasureHalEffect(Player& player)
{
    HalEffect effect(player);
    return MeasureSound(effect);
}

} // namespace gcsf::rip
