// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is Red: the pieces and the effects, the library's sound files, and
// the length analysis of a piece or an effect.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"

#include "gc/system.h"
#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// A Red game's names of its files: a prefix, the number in `digits` digits or more, and the extension. The game writes
// them with sprintf.
struct RedName
{
    std::string_view prefix;
    std::size_t digits;
    std::string_view extension;
};

constexpr RedName kRedPiece = {"music", 3, ".bgm"};
constexpr RedName kRedBank = {"wave", 4, ".wd"};
constexpr RedName kRedBlock = {"se", 3, ".seb"};
constexpr RedName kRedEffect = {"se", 6, ".sep"};

// A Red effect's sound ID is this plus the game's number of the effect (see RedSounds). A piece's is its number.
constexpr uint32_t kRedEffectId = 0x1000000;

// The game numbers a block's effect by the block's number times this, plus the effect's place in the block.
constexpr uint32_t kRedBlockStride = 1000;

// A Red game's files in `folder` that have the form of `name`, by their numbers. The game's disc library matches paths
// in any case. The match therefore ignores case. A file whose number has other digits than the game's sprintf writes
// is left out. A further leading zero is one example. The game never reads such a file.
std::map<uint32_t, std::string> RedFiles(const disc::GameFiles& game, std::string_view folder, const RedName& name)
{
    constexpr std::size_t kMaxDigits = 8;
    std::map<uint32_t, std::string> found;
    for (const disc::GameFile& file : game.files)
    {
        const std::string path = xsflib::AsciiLower(file.path);
        if (!path.starts_with(folder) || !path.ends_with(name.extension))
        {
            continue;
        }

        const std::string_view rest = std::string_view(path).substr(folder.size());
        if (!rest.starts_with(name.prefix) || rest.size() < name.prefix.size() + name.extension.size())
        {
            continue;
        }

        const std::string_view digits =
            rest.substr(name.prefix.size(), rest.size() - name.prefix.size() - name.extension.size());
        const auto is_digit = [](char c)
        {
            return c >= '0' && c <= '9';
        };
        if (digits.empty() || digits.size() > kMaxDigits || !std::all_of(digits.begin(), digits.end(), is_digit))
        {
            continue;
        }

        const uint32_t number = static_cast<uint32_t>(std::stoul(std::string(digits)));
        if (ZeroPadded(number, name.digits) == digits)
        {
            found.emplace(number, file.path);
        }
    }

    return found;
}

// The name of a Red game's file without its folder and extension.
std::string RedLabel(const std::string& path, const RedName& name)
{
    std::string label = path.substr(path.rfind('/') + 1);
    label.resize(label.size() - name.extension.size());
    return label;
}

// The wave bank of a Red sound: a little-endian half at `offset` in its information.
uint32_t RedInfoBank(std::span<const uint8_t> info, std::size_t offset)
{
    return info[offset] | static_cast<uint32_t>(info[offset + 1]) << 8;
}

// An effect of a Red game's block: its place in the block and its wave bank.
struct RedBlockEffect
{
    uint32_t place;
    uint32_t bank;
};

// The effects of a Red game's block, by place. A block's file starts with "SeBlock" and a zero byte. A big-endian half
// at 10 gives the number of places. A table of big-endian words follows at 16, one for each place. A free place's word
// is -1. Another place's word gives the offset of its effect's information from the table's end in its low 31 bits.
// The information's second and third bytes give its wave bank's number, the low byte first. Returns nothing if the
// file isn't a block.
std::optional<std::vector<RedBlockEffect>> RedBlockEffects(std::span<const uint8_t> block)
{
    constexpr std::size_t kTable = 16;
    constexpr std::size_t kBank = 1;
    constexpr uint32_t kFree = 0xffffffff;
    constexpr uint32_t kOffsetMask = 0x7fffffff;
    if (block.size() < kTable ||
        !std::equal(block.begin(), block.begin() + 8, std::string_view("SeBlock\0", 8).begin()))
    {
        return std::nullopt;
    }

    const std::size_t places = xsflib::LoadBe16(block.data() + 10);
    const std::size_t end = kTable + 4 * places;
    if (block.size() < end)
    {
        return std::nullopt;
    }

    std::vector<RedBlockEffect> effects;
    for (std::size_t place = 0; place < places; place++)
    {
        const uint32_t entry = xsflib::LoadBe32(block.data() + kTable + 4 * place);
        if (entry == kFree)
        {
            continue;
        }

        const std::size_t info = end + (entry & kOffsetMask);
        if (info + kBank + 2 > block.size())
        {
            return std::nullopt;
        }

        effects.push_back({static_cast<uint32_t>(place), RedInfoBank(block.subspan(info), kBank)});
    }

    return effects;
}

// A Red game's blocks of effects that the game loads, by number.
std::map<uint32_t, std::string> RedBlocks(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> blocks = RedFiles(game, profile.block_folder, kRedBlock);
    std::erase_if(blocks, [&profile](const auto& block) { return block.first >= profile.red_blocks; });
    return blocks;
}

// A Red game's effects in SEP files that the game can play, by number.
std::map<uint32_t, std::string> RedSeps(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> seps = RedFiles(game, profile.effect_folder, kRedEffect);
    std::erase_if(seps, [](const auto& sep) { return sep.first < kRedFirstSep; });
    return seps;
}

} // namespace

std::vector<Sound> RedSounds(const disc::GameFiles& game, const GameProfile& profile)
{
    std::vector<Sound> sounds;
    for (const auto& [number, path] : RedFiles(game, profile.music_folder, kRedPiece))
    {
        sounds.push_back({number, RedLabel(path, kRedPiece), true});
    }

    if (!profile.effect_folder)
    {
        return sounds;
    }

    for (const auto& [number, path] : RedBlocks(game, profile))
    {
        const std::vector<uint8_t> bytes = ReadGameFile(game, path);
        const std::optional<std::vector<RedBlockEffect>> effects = RedBlockEffects(bytes);
        if (!effects)
        {
            throw xsflib::FormatError(path + " isn't a block of effects");
        }

        for (const RedBlockEffect& effect : *effects)
        {
            sounds.push_back({kRedEffectId + number * kRedBlockStride + effect.place,
                              RedLabel(path, kRedBlock) + "-" + ZeroPadded(effect.place, 3), false});
        }
    }

    for (const auto& [number, path] : RedSeps(game, profile))
    {
        sounds.push_back({kRedEffectId + number, RedLabel(path, kRedEffect), false});
    }

    return sounds;
}

std::optional<std::string> AddRedFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kBankOffset = 6;
    constexpr std::size_t kSepBankOffset = 0x11;
    const std::map<uint32_t, std::string> banks = RedFiles(game, profile.wave_folder, kRedBank);
    std::set<uint32_t> named;

    // Reads the file at `path` into `bytes` and adds it to the library. Returns the error, or nothing.
    const auto add = [&](const std::string& path, std::vector<uint8_t>& bytes) -> std::optional<std::string>
    {
        if (!game.read_file || !game.read_file(path, bytes))
        {
            return "can't read " + path;
        }

        program.AddFile(path, bytes);

        return std::nullopt;
    };

    // Notes that the file at `path` names wave bank `bank`. Returns the error if the disc lacks the bank, or nothing.
    const auto name = [&](const std::string& path, uint32_t bank) -> std::optional<std::string>
    {
        if (!banks.contains(bank))
        {
            return path + " names wave bank " + std::to_string(bank) + ". The disc doesn't have that bank";
        }

        named.insert(bank);

        return std::nullopt;
    };

    for (const auto& [number, path] : RedFiles(game, profile.music_folder, kRedPiece))
    {
        std::vector<uint8_t> bytes;
        if (const std::optional<std::string> error = add(path, bytes))
        {
            return error;
        }

        if (bytes.size() < kBankOffset + 2 || !std::equal(bytes.begin(), bytes.begin() + 4, "BGM "))
        {
            return path + " isn't a piece of music";
        }

        if (const std::optional<std::string> error = name(path, xsflib::LoadBe16(bytes.data() + kBankOffset)))
        {
            return error;
        }
    }

    if (profile.effect_folder)
    {
        for (const auto& [number, path] : RedBlocks(game, profile))
        {
            std::vector<uint8_t> bytes;
            if (const std::optional<std::string> error = add(path, bytes))
            {
                return error;
            }

            const std::optional<std::vector<RedBlockEffect>> effects = RedBlockEffects(bytes);
            if (!effects)
            {
                return path + " isn't a block of effects";
            }

            for (const RedBlockEffect& effect : *effects)
            {
                if (const std::optional<std::string> error = name(path, effect.bank))
                {
                    return error;
                }
            }
        }

        for (const auto& [number, path] : RedSeps(game, profile))
        {
            std::vector<uint8_t> bytes;
            if (const std::optional<std::string> error = add(path, bytes))
            {
                return error;
            }

            if (bytes.size() < kSepBankOffset + 2 || !std::equal(bytes.begin(), bytes.begin() + 5, "SeSep"))
            {
                return path + " isn't an effect";
            }

            if (const std::optional<std::string> error = name(path, RedInfoBank(bytes, kSepBankOffset)))
            {
                return error;
            }
        }
    }

    std::vector<std::string> bank_paths;
    for (const uint32_t bank : named)
    {
        bank_paths.push_back(banks.at(bank));
    }

    return AddDiscFiles(program, game, bank_paths);
}

namespace
{

// A piece of a Red game (see MeasuredSound). It plays while one of Red's music controls plays a piece. It loops in a
// frame in which the CPU reaches the profile's `red_loop`. There, Red takes the piece's tracks back to their state at
// the start of the loop.
class RedPiece : public MeasuredSound
{
public:
    RedPiece(Player& player, const GameProfile& profile)
        : MeasuredSound(player),
          profile_(profile),
          watch_(player.System()->Cpu(), {profile.red_loop}, [this](uint32_t) { looped_ = true; })
    {
    }

private:
    bool Playing() override
    {
        const uint32_t controls = Word(profile_.red_controls);
        if (controls == 0)
        {
            return false;
        }

        for (uint32_t i = 0; i < profile_.red_music_controls; i++)
        {
            const uint32_t piece = Word(controls + i * profile_.red_control_size + profile_.red_music_offset);
            if (static_cast<int32_t>(piece) >= 0)
            {
                return true;
            }
        }

        return false;
    }

    bool Looped() override
    {
        return std::exchange(looped_, false);
    }

    const GameProfile& profile_;
    bool looped_ = false;
    ScopedWatch watch_; // the piece's loop
};

// An effect of a Red game (see MeasuredSound). It plays while one of the tracks of Red's effect control is busy. Each
// track loops on its own. The profile's `red_track_loop` takes a track back to the start of a loop that never ends.
// The tracks' loops give the effect's loops (see PartLoops). A track that has stopped holds nothing back.
class RedEffect : public MeasuredSound
{
public:
    RedEffect(Player& player, const GameProfile& profile)
        : MeasuredSound(player),
          profile_(profile),
          cpu_(player.System()->Cpu()),
          loops_(profile.red_effect_tracks),
          watch_(cpu_, {profile.red_track_loop}, [this](uint32_t) { TrackLooped(); })
    {
    }

private:
    // The effect control, or 0 before Red has set up its controls.
    uint32_t Control() const
    {
        const uint32_t controls = Word(profile_.red_controls);
        return controls != 0 ? controls + profile_.red_effect_control * profile_.red_control_size : 0;
    }

    // The effect control's first track, or 0.
    uint32_t Tracks() const
    {
        const uint32_t control = Control();
        return control != 0 ? Word(control) : 0;
    }

    // Whether track `i` is busy.
    bool TrackBusy(uint32_t i) const
    {
        return Word(Tracks() + i * profile_.red_track_size) != 0;
    }

    bool Playing() override
    {
        if (Tracks() == 0)
        {
            return false;
        }

        for (uint32_t i = 0; i < profile_.red_effect_tracks; i++)
        {
            if (TrackBusy(i))
            {
                return true;
            }
        }

        return false;
    }

    // A track has gone back to the start of its loop. r3 holds its control and r5 the track.
    void TrackLooped()
    {
        const uint32_t control = Control();
        const uint32_t tracks = Tracks();
        const uint32_t track = cpu_.state_.gpr[5];
        if (tracks == 0 || cpu_.state_.gpr[3] != control || track < tracks)
        {
            return;
        }

        const uint32_t i = (track - tracks) / profile_.red_track_size;
        if (i >= profile_.red_effect_tracks)
        {
            return;
        }

        loops_.Count(i);
    }

    bool Looped() override
    {
        return loops_.Looped();
    }

    bool OthersDone() override
    {
        for (uint32_t i = 0; i < profile_.red_effect_tracks; i++)
        {
            if (loops_.HoldsBack(i) && TrackBusy(i))
            {
                return false;
            }
        }

        return true;
    }

    const GameProfile& profile_;
    ppc::Cpu& cpu_;
    PartLoops loops_;   // the tracks' loops
    ScopedWatch watch_; // the tracks' jumps back to their loops
};

} // namespace

std::optional<Timing> MeasureRedSound(Player& player, const GameProfile& profile, uint32_t id)
{
    if (id >= kRedEffectId)
    {
        RedEffect effect(player, profile);
        return MeasureSound(effect);
    }

    RedPiece piece(player, profile);
    return MeasureSound(piece);
}

} // namespace gcsf::rip
