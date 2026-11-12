// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is JAudio2, JAudio1 or Jac: the sound table and its names, the
// sequences that no sound plays, Pikmin 2's pieces and the library's sound files. jaudio_sound_files.cpp reads another
// release's sound files, and jaudio_analysis.cpp holds the length analysis.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/jaudio/aaf.h"
#include "ntdlib/jaudio/archive.h"
#include "ntdlib/jaudio/jac_effects.h"
#include "ntdlib/jaudio/pikmin2_pieces.h"
#include "ntdlib/jaudio/rarc.h"
#include "ntdlib/jaudio/sequence_commands.h"
#include "ntdlib/jaudio/wave_system.h"
#include "ntdlib/yaz0.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/text.h"

#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{
namespace
{

// The sounds of Pikmin 2's pieces. A piece is named by its file in the sequence archive, without the extension, and a
// variant by its label. The archive may be compressed with Yaz0. Throws xsflib::FormatError if the
// archive is damaged or lacks a piece's file.
std::vector<jaudio::SoundEntry> PieceSounds(const std::vector<uint8_t>& sequence_archive)
{
    std::vector<uint8_t> decoded;
    const std::vector<jaudio::ArchiveFile> files = jaudio::ReadRarc(ntdlib::DecodeIfYaz0(sequence_archive, decoded));
    std::vector<jaudio::SoundEntry> sounds;
    for (const jaudio::Pikmin2Piece& piece : jaudio::kPikmin2Pieces)
    {
        const std::string_view name = piece.sequence;
        const auto is_piece = [name](const jaudio::ArchiveFile& file)
        {
            return file.name == name;
        };
        const auto file = std::find_if(files.begin(), files.end(), is_piece);
        if (file == files.end())
        {
            throw xsflib::FormatError("the sequence archive lacks " + std::string(name));
        }

        jaudio::SoundEntry sound;
        sound.name = piece.Name();
        sound.section = 0x80; // JAudio1's section of sequences
        sound.index = static_cast<uint16_t>(sounds.size());
        sound.section_name = "BGM";
        sound.source = jaudio::SoundEntry::Source::kArchive;
        sound.resource = file->id;
        sounds.push_back(std::move(sound));
    }

    return sounds;
}

// How GCSF reads a start-up archive: its messages, and its rules for the sound table and the wave systems.
const jaudio::BaaOptions kBaaOptions{
    .not_an_archive = "it isn't a BAA archive",
    .unknown_command = "the BAA archive has a command that isn't known",
    .past_end = "the BAA archive is damaged",
    .table_damaged = "the BAA archive's sound table is damaged",
    .no_table = "the BAA archive has no sound table",
    .no_sounds = "the BAA archive's sound table lists no sounds",
    .wave_system_damaged = "a wave system in the BAA archive is damaged",
    .min_size = 0,
    .tables_reach_end = true,
    .check_magic = true,
    .skip_empty = true,
    .cap_counts = true,
    .check_name_counts = true,
    .names_inside = true,
    .sources = true,
    .section_names = true,
};

// The files of a sequence archive, the archive decoded first if it's compressed with Yaz0. `decoded` then holds the
// decoded archive, and the files' spans point into it. An earlier JAudio1's archive isn't a RARC. The sequence index in
// its start-up archive divides it (Luigi's Mansion's `JaiArcS.arc`). Throws xsflib::FormatError if the archive or the
// index is damaged.
std::vector<jaudio::ArchiveFile> SequenceFiles(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                               const std::vector<uint8_t>& sequence_archive,
                                               std::vector<uint8_t>& decoded)
{
    const std::span<const uint8_t> archive = ntdlib::DecodeIfYaz0(sequence_archive, decoded);
    if (profile.library == SoundLibrary::kJAudio1)
    {
        if (auto files = jaudio::ReadAafSequenceFiles(sound_archive, archive))
        {
            return std::move(*files);
        }
    }

    return jaudio::ReadRarc(archive);
}

} // namespace

namespace
{

// The key under which a sound's name is found: its ID without an effect's bits 10 and 11, and without the category of
// a sequence or a stream. Those bits set how long an effect plays. The sound table gives a sequence category 0, and the
// names give it the table's category for sequences.
uint32_t NameKey(uint32_t id)
{
    const uint32_t type = id & jaudio::kAafTypeMask;
    return type == jaudio::kAafEffect ? id & ~(jaudio::kAafToEnd | jaudio::kAafToEnd >> 1)
                                      : type | (id & jaudio::kAafIndexMask);
}

// The sounds that a JAudio1 sound name table (.asn, jaudio::ReadAsnSounds) names, by NameKey: each sound's name and ID.
// The first name of a key wins. Throws xsflib::FormatError if the table is damaged.
std::map<uint32_t, std::pair<std::string, uint32_t>> ReadSoundNames(std::span<const uint8_t> table)
{
    std::map<uint32_t, std::pair<std::string, uint32_t>> names;
    for (const jaudio::AsnSound& sound : jaudio::ReadAsnSounds(table))
    {
        names.try_emplace(NameKey(sound.id), sound.name, sound.id);
    }

    return names;
}

} // namespace

std::vector<jaudio::SoundEntry> ListSounds(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                           const std::vector<uint8_t>& sequence_archive,
                                           const std::vector<uint8_t>* names)
{
    if (profile.library != SoundLibrary::kJAudio1)
    {
        jaudio::BaaOptions options = kBaaOptions;
        options.first_layouts = profile.first_layouts;
        std::vector<uint8_t> decoded;
        return jaudio::ReadSounds(ntdlib::DecodeIfYaz0(sound_archive, decoded), options);
    }

    // A JAudio1 sound table names no sound. A sequence takes its file's name in the sequence archive, without the
    // extension. A name table's name takes precedence. A sequence then takes the game's ID too, and plays as the game
    // plays it. An effect keeps its ID: it plays to its end. The archive may be compressed with Yaz0.
    const auto named = names ? ReadSoundNames(*names) : std::map<uint32_t, std::pair<std::string, uint32_t>>{};
    std::vector<uint8_t> decoded;
    std::map<uint32_t, std::string> file_names;
    for (const jaudio::ArchiveFile& file : SequenceFiles(profile, sound_archive, sequence_archive, decoded))
    {
        file_names.emplace(file.id, file.name.substr(0, file.name.rfind('.')));
    }

    // Pikmin 2's table lists sequences whose data the game's sound layer gives them. Pikmin 2's pieces take
    // their place, ahead of the effects (see PieceSounds).
    std::vector<jaudio::SoundEntry> sounds = jaudio::ReadAafSounds(sound_archive);
    if (profile.pikmin2_pieces)
    {
        const auto plays_archive_file = [](const jaudio::SoundEntry& sound)
        {
            return sound.source == jaudio::SoundEntry::Source::kArchive;
        };
        std::erase_if(sounds, plays_archive_file);
    }

    for (jaudio::SoundEntry& sound : sounds)
    {
        const uint32_t id = sound.Id();
        if (sound.source == jaudio::SoundEntry::Source::kArchive)
        {
            const auto name = file_names.find(sound.resource);
            if (name == file_names.end())
            {
                throw xsflib::FormatError(
                    "the AAF archive's sound table names a sequence that the sequence archive lacks");
            }

            sound.name = name->second;
            sound.section_name = "BGM";
            if (const auto game = named.find(NameKey(id)); game != named.end())
            {
                sound.name = game->second.first;
                sound.group = static_cast<uint8_t>(game->second.second >> 16);
            }

            continue;
        }

        // An effect is named by its category and its index.
        char label[32];
        std::snprintf(label, sizeof(label), "se%u-%04u", static_cast<unsigned>(jaudio::AafCategory(id)),
                      static_cast<unsigned>(id & jaudio::kAafIndexMask));
        const auto game = named.find(NameKey(id));
        sound.name = game != named.end() ? game->second.first : label;
        sound.section_name = "SE";
        sound.group_name = std::to_string(jaudio::AafCategory(id));
    }

    if (profile.pikmin2_pieces)
    {
        std::vector<jaudio::SoundEntry> pieces = PieceSounds(sequence_archive);
        sounds.insert(sounds.begin(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    }

    return sounds;
}

std::vector<jaudio::WaveSystem> WaveSystems(const GameProfile& profile, const std::vector<uint8_t>& sound_archive)
{
    if (profile.library == SoundLibrary::kJAudio1)
    {
        return jaudio::ReadAafWaveSystems(sound_archive);
    }

    std::vector<uint8_t> decoded;
    return jaudio::ReadWaveSystems(ntdlib::DecodeIfYaz0(sound_archive, decoded), kBaaOptions);
}

bool IsMusic(const jaudio::SoundEntry& sound)
{
    const std::string section = xsflib::AsciiLower(sound.section_name);
    return section == "bgm" || section == "type_bgm";
}

Sound SoundOf(const jaudio::SoundEntry& entry)
{
    std::string label = entry.name;
    if (label.empty())
    {
        char id[16];
        std::snprintf(id, sizeof(id), "%08X", static_cast<unsigned>(entry.Id()));
        label = id;
    }

    return {entry.Id(), std::move(label), IsMusic(entry), entry.stream};
}

std::vector<Sound> JacPieces(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kHeader = 0x20, kEntry = 0x20, kNameSize = 14, kSize = 0x1c;
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    const std::span<const uint8_t> index = DolBytes(dol, profile.sequence_index);
    if (index.size() < kHeader || !std::equal(index.begin(), index.begin() + 4, "BARC"))
    {
        throw xsflib::FormatError("main.dol has no sequence index at " + xsflib::Hex(profile.sequence_index, 8));
    }

    const uint32_t count = xsflib::LoadBe32(index.data() + 0xc);
    if (index.size() < kHeader + std::size_t{count} * kEntry)
    {
        throw xsflib::FormatError("main.dol's sequence index ends early");
    }

    // A sound's ID holds the piece's number in a byte.
    constexpr uint32_t kMaxPieces = 0x100;
    if (count > kMaxPieces)
    {
        throw xsflib::FormatError("main.dol's sequence index has " + std::to_string(count) + " files");
    }

    const auto id = [](uint32_t piece, uint32_t part, uint32_t system, uint32_t group)
    {
        return jaudio::kAafSequence | group << 24 | system << 16 | part << 8 | piece;
    };
    std::vector<Sound> pieces;
    for (uint32_t number = kJacFirstPiece; number < count; number++)
    {
        const uint8_t* entry = index.data() + kHeader + number * kEntry;
        if (xsflib::LoadBe32(entry + kSize) == 0)
        {
            continue;
        }

        std::string label = TextBeforeZero(std::span(entry, kNameSize));
        label = label.substr(0, label.rfind('.'));
        if (number != profile.parted_piece)
        {
            pieces.push_back({id(number, 0, number, 0), std::move(label), true});
            continue;
        }

        for (const JacPart& part : profile.parts)
        {
            pieces.push_back(
                {id(number, part.number, part.system, part.group), label + "-" + std::to_string(part.number), true});
        }
    }

    return pieces;
}

std::vector<Sound> JacEffects(const disc::GameFiles& game, const GameProfile& profile)
{
    if (!profile.jac_effects.table)
    {
        return {};
    }

    const auto label_of_other = [](uint32_t id)
    {
        char label[16];
        std::snprintf(label, sizeof(label), "%08X", static_cast<unsigned>(id));
        return std::string(label);
    };
    std::vector<Sound> effects;
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    for (jaudio::JacEffectSound& effect : jaudio::ListJacEffects(dol, profile.jac_effects, label_of_other))
    {
        effects.push_back({effect.id, std::move(effect.name), false});
    }

    return effects;
}

std::vector<UnplayedSequence> UnplayedSequences(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                                const std::vector<jaudio::SoundEntry>& sounds,
                                                const std::vector<uint8_t>& sequence_archive)
{
    // Pikmin 2's pieces are every piece of music already.
    if (profile.pikmin2_pieces)
    {
        return {};
    }

    // The files that the table's sounds play, and the volume that most of them play at. A tie goes to the volume that
    // comes first. JAudio1's effects sequence plays a file too.
    const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
    std::set<uint32_t> played;
    if (jaudio1)
    {
        played.insert(jaudio::AafEffectsSequenceFile(sound_archive));
    }

    std::map<uint8_t, int> volumes;
    const jaudio::SoundEntry* first = nullptr;
    for (const jaudio::SoundEntry& sound : sounds)
    {
        if (sound.source == jaudio::SoundEntry::Source::kArchive)
        {
            played.insert(sound.resource);
            volumes[sound.volume]++;
            first = first ? first : &sound;
        }
    }

    if (!first)
    {
        return {};
    }

    uint8_t volume = first->volume;
    int most = 0;
    for (const jaudio::SoundEntry& sound : sounds)
    {
        if (sound.source == jaudio::SoundEntry::Source::kArchive && volumes[sound.volume] > most)
        {
            volume = sound.volume;
            most = volumes[sound.volume];
        }
    }

    // Archives compressed with Yaz0 are decoded first. A copy of the start-up archive stays decoded. The game loads it
    // as it is.
    std::vector<uint8_t> decoded_baa;
    const std::span<const uint8_t> start_up = ntdlib::DecodeIfYaz0(sound_archive, decoded_baa);
    std::vector<uint8_t> decoded;
    std::vector<UnplayedSequence> unplayed;
    for (const jaudio::ArchiveFile& file : SequenceFiles(profile, sound_archive, sequence_archive, decoded))
    {
        if (played.contains(file.id) || file.data.empty())
        {
            continue;
        }

        UnplayedSequence u;
        u.sound = *first;
        u.sound.name = file.name.substr(0, file.name.rfind('.'));
        u.sound.volume = volume;
        u.sound.resource = file.id;
        u.file_name = file.name;

        // The sound's entry: the volume and the file's number, where the first sound's layout has them.
        u.sound_archive.assign(start_up.begin(), start_up.end());
        uint8_t* entry = u.sound_archive.data() + first->offset;
        const jaudio::ItemLayout layout = jaudio1 ? jaudio::kAafItemLayout : jaudio::LayoutOf(first->type);
        entry[layout.volume] = volume;
        xsflib::StoreBe16(entry + *layout.file, file.id);
        unplayed.push_back(std::move(u));
    }

    return unplayed;
}

namespace
{

// The argument types that a register prefix (0x90 to 0x97) gives a command. The top bits of `mask`, one for each of
// the first `count` arguments, mark those that come from registers.
uint16_t RegisterTypes(uint8_t mask, int count)
{
    uint16_t types = 0;
    for (int i = 0; i < count; i++)
    {
        if (mask & (0x80 >> i))
        {
            types = static_cast<uint16_t>(types | 3u << (2 * i));
        }
    }

    return types;
}

// Skips a MIDI value: bytes with the top bit set, then one without it.
std::size_t SkipMidiValue(const xsflib::Reader& r, std::size_t at)
{
    while (r.U8(at) & 0x80)
    {
        at++;
    }

    return at + 1;
}

// Reads the `count` arguments of a command at `at` in `r`, of the types in `types` (two bits each, the first argument
// in the lowest bits: 0 a byte, 1 a 16-bit value, 2 a 24-bit value, 3 a register's number). Moves `at` past them. An
// argument that comes from a register has no value here.
std::array<std::optional<uint32_t>, 8> ReadArguments(const xsflib::Reader& r, std::size_t& at, uint16_t types,
                                                     std::size_t count)
{
    std::array<std::optional<uint32_t>, 8> a;
    for (std::size_t i = 0; i < count; i++, types = static_cast<uint16_t>(types >> 2))
    {
        switch (types & 3)
        {
        case 0:
            a[i] = r.U8(at);
            at += 1;
            break;
        case 1:
            a[i] = r.U16(at);
            at += 2;
            break;
        case 2:
            a[i] = uint32_t{r.U16(at)} << 8 | r.U8(at + 2);
            at += 3;
            break;
        default:
            at += 1;
            break;
        }
    }

    return a;
}

// The banks that a JAudio2 sequence selects with commands 0xE1 and 0xE2, along every path from `start` in `data`
// through the tracks that it opens, its calls and its jumps. Returns nothing if a path meets a command that isn't
// known, a target or a bank that comes from a register, a jump or a call through a table, or the data's end.
std::optional<std::set<uint32_t>> SequenceBanks(std::span<const uint8_t> data, uint32_t start)
{
    const xsflib::Reader r(data, true);
    std::set<uint32_t> banks;
    std::set<std::size_t> seen;
    std::vector<std::size_t> paths = {start};
    try
    {
        while (!paths.empty())
        {
            std::size_t at = paths.back();
            paths.pop_back();
            bool ends = false;
            while (!ends && seen.insert(at).second)
            {
                const uint8_t b = r.U8(at++);
                if (b < 0x80)
                {
                    // A note: its flags, its velocity, and a gate note's length.
                    const uint8_t flags = r.U8(at);
                    at = (flags & 7) == 0 ? SkipMidiValue(r, at + 2) : at + 2;
                    continue;
                }

                if ((b & 0xf0) == 0x80)
                {
                    continue; // a note's end
                }

                uint8_t command = b;
                uint16_t from_registers = 0;
                if ((b & 0xf0) == 0x90)
                {
                    from_registers = RegisterTypes(r.U8(at), (b & 7) + 1);
                    command = r.U8(at + 1);
                    at += 2;
                }

                if (command == 0xf0)
                {
                    at = SkipMidiValue(r, at);
                    continue;
                }

                if (command < jaudio::kFirstSequenceCommand ||
                    !jaudio::kSequenceCommands[command - jaudio::kFirstSequenceCommand].known || command == 0xc9 ||
                    command == 0xca)
                {
                    return std::nullopt;
                }

                const jaudio::SequenceCommand& info =
                    jaudio::kSequenceCommands[command - jaudio::kFirstSequenceCommand];
                const std::array<std::optional<uint32_t>, 8> a =
                    ReadArguments(r, at, static_cast<uint16_t>(info.types | from_registers), info.count);

                const bool selects = command == 0xe1 || command == 0xe2;
                const bool goes =
                    command == 0xc1 || command == 0xc3 || command == 0xc4 || command == 0xc7 || command == 0xc8;
                const std::optional<uint32_t> value = command == 0xc3 || command == 0xc7 || selects ? a[0] : a[1];
                if ((selects || goes) && !value)
                {
                    return std::nullopt;
                }

                if (command == 0xe1)
                {
                    banks.insert(*value >> 8);
                }
                else if (command == 0xe2)
                {
                    banks.insert(*value);
                }
                else if (goes)
                {
                    paths.push_back(*value);
                }

                // A jump, a return and the sequence's end end the path.
                ends = command == 0xc7 || command == 0xc5 || command == 0xff;
            }
        }
    }
    catch (const xsflib::FormatError&)
    {
        return std::nullopt;
    }

    return banks;
}

// The start-up archives in the "baac" block of start-up archive `baa`, in the block's order. The block holds the
// archives' count, each one's offset in the block, then the archives. Throws xsflib::FormatError if the archive is
// damaged.
std::vector<std::span<const uint8_t>> BaacArchives(std::span<const uint8_t> baa)
{
    std::span<const uint8_t> block;
    const auto find_block = [baa, &block](const std::string& tag, std::size_t at)
    {
        if (tag == "baac")
        {
            block = jaudio::CommandBlock(baa, at, kBaaOptions);
        }
    };
    jaudio::ForEachCommand(baa, kBaaOptions, find_block);

    std::vector<std::span<const uint8_t>> archives;
    if (block.empty())
    {
        return archives;
    }

    const xsflib::Reader b(block, true);
    const uint32_t count = b.U32(0);
    if (count > b.Size() / 4)
    {
        throw xsflib::FormatError("the BAA archive's baac block is damaged");
    }

    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t start = b.U32(4 + 4 * i);
        const uint32_t end = i + 1 < count ? b.U32(8 + 4 * i) : static_cast<uint32_t>(b.Size());
        if (end < start)
        {
            throw xsflib::FormatError("the BAA archive's baac block is damaged");
        }

        archives.push_back(b.Sub(start, end - start));
    }

    return archives;
}

// The archive of the "baac" block that each of `sounds` plays a bank of: its place in the block plus 1, or 0 for none.
// A sound of the sequence collection plays the banks that its sequence selects (SequenceBanks). A bank's header holds
// its number after its tag and its size. Throws xsflib::FormatError if the archive is damaged.
std::vector<uint32_t> BaacArchivesOf(std::span<const uint8_t> baa, const std::vector<jaudio::SoundEntry>& sounds)
{
    // Each archive's banks.
    std::vector<std::set<uint32_t>> banks;
    for (const std::span<const uint8_t> archive : BaacArchives(baa))
    {
        const xsflib::Reader a(archive, true);
        std::set<uint32_t>& numbers = banks.emplace_back();
        const auto read_bank = [&a, &numbers](const std::string& tag, std::size_t at)
        {
            if (tag == "bnk ")
            {
                numbers.insert(a.U32(a.U32(at + 4) + 8));
            }
        };
        jaudio::ForEachCommand(archive, kBaaOptions, read_bank);
    }

    // The sequence collection: "SC", the categories' count, then each category's table of sequence offsets.
    std::span<const uint8_t> collection;
    const auto find_collection = [baa, &collection](const std::string& tag, std::size_t at)
    {
        if (tag == "bsc ")
        {
            collection = jaudio::CommandBlock(baa, at, kBaaOptions);
        }
    };
    jaudio::ForEachCommand(baa, kBaaOptions, find_collection);

    std::vector<uint32_t> archives(sounds.size(), 0);
    if (banks.empty() || collection.empty())
    {
        return archives;
    }

    const xsflib::Reader sc(collection, true);
    if (sc.U8(0) != 'S' || sc.U8(1) != 'C')
    {
        throw xsflib::FormatError("the BAA archive's sequence collection is damaged");
    }

    for (std::size_t s = 0; s < sounds.size(); s++)
    {
        const jaudio::SoundEntry& sound = sounds[s];
        if (sound.source != jaudio::SoundEntry::Source::kCollection || sound.stream || sound.group >= sc.U16(2))
        {
            continue;
        }

        const uint32_t table = sc.U32(8 + 4 * std::size_t{sound.group});
        if (sound.resource >= sc.U32(table))
        {
            continue;
        }

        const std::optional<std::set<uint32_t>> selected =
            SequenceBanks(collection, sc.U32(table + 4 + 4 * std::size_t{sound.resource}));
        for (std::size_t i = 0; selected && i < banks.size(); i++)
        {
            const auto in_archive = [&banks, i](uint32_t bank)
            {
                return banks[i].contains(bank);
            };
            if (std::any_of(selected->begin(), selected->end(), in_archive))
            {
                archives[s] = static_cast<uint32_t>(i + 1);
                break;
            }
        }
    }

    return archives;
}

// Whether a rip of `sounds` loads group `group` of wave system `system` in a Jac game. At start-up, the sound library
// loads the effects' first group, group 0 of system 0. The driver loads the group that a piece's ID gives, and the
// profile's groups for an effect (driver/pikmin.c). Without `sounds`, the rip loads every group.
bool JacLoadsGroup(const GameProfile& profile, const std::vector<uint32_t>* sounds, uint32_t system, uint32_t group)
{
    const uint32_t entry = system << 16 | group;
    const auto gives = [&profile, system, group, entry](uint32_t id)
    {
        if (IsJacEffect(id))
        {
            const std::vector<uint32_t>& groups = profile.effect_groups;
            return std::find(groups.begin(), groups.end(), entry) != groups.end();
        }

        return (id >> 16 & 0xff) == system && (id >> 24 & 0x7f) == group;
    };
    return !sounds || entry == 0 || std::any_of(sounds->begin(), sounds->end(), gives);
}

// Adds `name` to `names` unless `names` has it.
void AddName(std::vector<std::string>& names, const std::string& name)
{
    if (std::find(names.begin(), names.end(), name) == names.end())
    {
        names.push_back(name);
    }
}

// The wave archives that a JAudio rip of `sounds` adds, in the order of the library, from the start-up archive
// `sound_archive`. With `wave_archives`, the archives that wave discovery found, and the archives that a JAudio1 game
// keeps loaded. Otherwise every archive that the start-up archive names. The "baac" block's archives follow, for the
// sounds that load them. Throws xsflib::FormatError if the archive is damaged.
std::vector<std::string> WaveArchivesToAdd(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                           const std::vector<std::string>* wave_archives,
                                           const std::vector<uint32_t>* sounds)
{
    const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
    std::vector<std::string> names;
    if (wave_archives)
    {
        names = *wave_archives;
        if (jaudio1)
        {
            for (const std::string& name : jaudio::AafStayingWaveArchiveNames(sound_archive))
            {
                AddName(names, name);
            }

            for (const std::string& name :
                 jaudio::GroupArchives(jaudio::ReadAafWaveSystems(sound_archive), profile.staying_groups))
            {
                AddName(names, name);
            }
        }
    }
    else
    {
        std::vector<uint8_t> decoded;
        names = jaudio1 ? jaudio::AafWaveArchiveNames(sound_archive)
                        : jaudio::WaveArchiveNames(ntdlib::DecodeIfYaz0(sound_archive, decoded), kBaaOptions);
    }

    // The wave archives of the "baac" block's archives that the sounds load.
    if (profile.baac_archives)
    {
        std::vector<uint8_t> decoded;
        const std::vector<std::span<const uint8_t>> archives =
            BaacArchives(ntdlib::DecodeIfYaz0(sound_archive, decoded));
        for (std::size_t i = 0; i < archives.size(); i++)
        {
            const auto loads = [i](uint32_t id)
            {
                return id >> kBaacArchiveShift == i + 1;
            };
            if (sounds && std::none_of(sounds->begin(), sounds->end(), loads))
            {
                continue;
            }

            for (const std::string& name : jaudio::WaveArchiveNames(archives[i], kBaaOptions))
            {
                AddName(names, name);
            }
        }
    }

    return names;
}

} // namespace

std::optional<std::string> ListJAudioSounds(const disc::GameFiles& game, const GameProfile& profile, GameSounds& listed)
{
    if (std::optional<std::string> error = ReadDiscFile(game, profile.sound_archive, listed.sound_archive))
    {
        return error;
    }

    // A JAudio1 game's sound list requires its sequence archive. Otherwise a sequence archive that can't be read loses
    // only the sequences that no sound plays. A game without a sequence archive has no such sequences.
    std::vector<uint8_t> sequence_archive;
    const bool has_sequences = profile.sequence_archive && game.read_file(profile.sequence_archive, sequence_archive);
    if (!has_sequences && profile.library == SoundLibrary::kJAudio1)
    {
        return std::string("can't read ") + profile.sequence_archive;
    }

    // The sound name table, for a game that has one.
    std::vector<uint8_t> names;
    if (profile.sound_names)
    {
        if (std::optional<std::string> error = ReadDiscFile(game, profile.sound_names, names))
        {
            return error;
        }
    }

    std::vector<jaudio::SoundEntry> entries;
    try
    {
        entries = ListSounds(profile, listed.sound_archive, sequence_archive, profile.sound_names ? &names : nullptr);
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.sound_archive) + ": " + e.what();
    }

    // The sequences that no sound plays.
    if (has_sequences)
    {
        try
        {
            listed.unplayed = UnplayedSequences(profile, listed.sound_archive, entries, sequence_archive);
        }
        catch (const xsflib::FormatError& e)
        {
            listed.warnings.push_back(std::string(profile.sequence_archive) + ": " + e.what() +
                                      "; the sequences that no sound plays are left out");
        }
    }
    else if (profile.sequence_archive)
    {
        listed.warnings.push_back(std::string("can't read ") + profile.sequence_archive);
    }

    // The archive of the "baac" block that each sound loads, in bits 28 to 31 of its ID.
    std::vector<uint32_t> archives(entries.size(), 0);
    if (profile.baac_archives)
    {
        try
        {
            archives = BaacArchivesOf(listed.sound_archive, entries);
        }
        catch (const xsflib::FormatError& e)
        {
            listed.warnings.push_back(std::string(profile.sound_archive) + ": " + e.what() +
                                      "; no sound loads an archive of its baac block");
        }
    }

    for (std::size_t i = 0; i < entries.size(); i++)
    {
        Sound sound = SoundOf(entries[i]);
        sound.id |= archives[i] << kBaacArchiveShift;
        listed.sounds.push_back(std::move(sound));
    }

    return std::nullopt;
}

std::optional<std::string> AddJacFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile,
                                       const std::vector<uint32_t>* sounds)
{
    std::vector<uint8_t> banks;
    if (std::optional<std::string> error = ReadDiscFile(game, profile.sound_archive, banks))
    {
        return error;
    }

    std::vector<std::string> paths = {profile.sequence_archive};
    std::set<std::string> loaded = {profile.sequence_archive};
    try
    {
        const xsflib::Reader r(banks, true);
        const uint32_t list = r.U32(0);
        const uint32_t systems = r.U32(4);
        for (uint32_t number = 0; number < systems; number++)
        {
            const uint32_t offset = r.U32(list + number * 8);
            const uint32_t size = r.U32(list + number * 8 + 4);
            if (size == 0)
            {
                continue;
            }

            const std::vector<jaudio::WaveGroup> groups = jaudio::ReadWaveSystem(r.Sub(offset, size), number).groups;
            for (std::size_t g = 0; g < groups.size(); g++)
            {
                if (groups[g].archive.empty())
                {
                    continue;
                }

                const std::string path = profile.wave_folder + groups[g].archive;
                AddName(paths, path);
                if (JacLoadsGroup(profile, sounds, number, static_cast<uint32_t>(g)))
                {
                    loaded.insert(path);
                }
            }
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.sound_archive) + ": " + e.what();
    }

    program.AddFile(profile.sound_archive, banks);

    // An archive that the rip never loads keeps its size, but holds zeros.
    for (const std::string& path : paths)
    {
        std::vector<uint8_t> bytes;
        if (std::optional<std::string> error = AddDiscFile(program, game, path, bytes, loaded.contains(path)))
        {
            return error;
        }
    }

    return std::nullopt;
}

std::optional<std::string> AddJAudioFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                          const GameProfile& profile, const std::vector<std::string>* wave_archives,
                                          const std::vector<uint32_t>* sounds)
{
    std::vector<uint8_t> sound_archive;
    if (std::optional<std::string> error = ReadDiscFile(game, profile.sound_archive, sound_archive))
    {
        return error;
    }

    std::vector<std::string> paths;
    if (profile.sequence_archive)
    {
        paths.emplace_back(profile.sequence_archive);
    }

    if (profile.other_files_first)
    {
        paths.insert(paths.end(), profile.other_files.begin(), profile.other_files.end());
    }

    try
    {
        for (const std::string& name : WaveArchivesToAdd(profile, sound_archive, wave_archives, sounds))
        {
            paths.push_back(profile.wave_folder + name);
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.sound_archive) + ": " + e.what();
    }

    for (const char* path : profile.other_files)
    {
        if (!profile.other_files_first && std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }

    program.AddFile(profile.sound_archive, sound_archive);

    return AddDiscFiles(program, game, paths);
}

} // namespace gcsf::rip
