// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is JAudio2, JAudio1 or Jac: the start-up archive and another
// release's sound files, the sound table and its names, the sequences that no sound plays, Pikmin 2's pieces, the
// library's sound files, and the length analysis through ntdlib's AnalyzeGameSound.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/jaudio/aaf.h"
#include "ntdlib/jaudio/archive.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/jaudio/rarc.h"
#include "ntdlib/jaudio/wave_system.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/files.h"
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

// The entry of the profile's scene pieces for sound `id`, or null for a sound that isn't one of them.
const ScenePiece* PieceOf(const GameProfile& profile, uint32_t id)
{
    const uint32_t index = id & 0xffff;
    if ((id & jaudio::kAafTypeMask) != jaudio::kAafSequence || index >= profile.scene_pieces.size())
    {
        return nullptr;
    }

    return &profile.scene_pieces[index];
}

// The sounds of the profile's scene pieces, each named by its file in the sequence archive, without the extension. The
// archive may be compressed with Yaz0. Throws xsflib::FormatError if the archive is damaged or lacks a piece's file.
std::vector<jaudio::SoundEntry> PieceSounds(const GameProfile& profile, const std::vector<uint8_t>& sequence_archive)
{
    std::vector<uint8_t> decoded;
    const std::vector<jaudio::ArchiveFile> files = jaudio::ReadRarc(jaudio::DecodeIfYaz0(sequence_archive, decoded));
    std::vector<jaudio::SoundEntry> sounds;
    for (const ScenePiece& piece : profile.scene_pieces)
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
        sound.name = name.substr(0, name.rfind('.'));
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

// Reads a file of the host's file system. A file larger than 1 GiB, the largest that GCSF reads into memory, can't be
// read.
bool ReadHostFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && xsflib::ReadFile(path, data, std::size_t{1} << 30);
}

// Reads another release's sequence table as the sequence archive at `path`. `read_disc` reads the disc's archive. Each
// file's data then comes from the table (see jaudio::RepackSequenceTable). `folder` must hold one table, a `.bdt` file.
// The Wind Waker HD keeps its sequences that way. AppleDouble files don't count. macOS writes one, named "._" and the
// file's name, beside each file that it copies to a FAT or exFAT drive.
bool ReadSequenceTable(const std::filesystem::path& folder,
                       const std::function<bool(const std::string&, std::vector<uint8_t>&)>& read_disc,
                       const std::string& path, std::vector<uint8_t>& data)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> tables;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code type_error;
        if (it->path().extension() == ".bdt" && !xsflib::Utf8(it->path().filename()).starts_with("._") &&
            it->is_regular_file(type_error))
        {
            tables.push_back(it->path());
        }
    }

    std::vector<uint8_t> table;
    std::vector<uint8_t> archive;
    if (ec || tables.size() != 1 || !ReadHostFile(tables.front(), table) || !read_disc || !read_disc(path, archive))
    {
        return false;
    }

    try
    {
        data = jaudio::RepackSequenceTable(archive, table);
    }
    catch (const xsflib::FormatError&)
    {
        return false;
    }

    return true;
}

// The files of a sequence archive, the archive decoded first if it's compressed with Yaz0. `decoded` then holds the
// decoded archive, and the files' spans point into it. An earlier JAudio1's archive isn't a RARC. The sequence index in
// its start-up archive divides it (Luigi's Mansion's `JaiArcS.arc`). Throws xsflib::FormatError if the archive or the
// index is damaged.
std::vector<jaudio::ArchiveFile> SequenceFiles(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                               const std::vector<uint8_t>& sequence_archive,
                                               std::vector<uint8_t>& decoded)
{
    const std::span<const uint8_t> archive = jaudio::DecodeIfYaz0(sequence_archive, decoded);
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

void UnpackSoundArchive(disc::GameFiles& game, const GameProfile& profile)
{
    if (!profile.packed_archive)
    {
        return;
    }

    const auto from_game = game.read_file;
    const std::string sound_archive = profile.sound_archive;
    const std::string packed_archive = profile.packed_archive;
    const std::string name = xsflib::AsciiLower(profile.packed_sound_archive);
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        if (path != sound_archive)
        {
            return from_game && from_game(path, data);
        }

        std::vector<uint8_t> packed;
        if (!from_game || !from_game(packed_archive, packed))
        {
            return false;
        }

        try
        {
            std::vector<uint8_t> decoded;
            for (const jaudio::ArchiveFile& file : jaudio::ReadRarc(jaudio::DecodeIfYaz0(packed, decoded)))
            {
                if (xsflib::AsciiLower(file.name) == name)
                {
                    data.assign(file.data.begin(), file.data.end());
                    return true;
                }
            }
        }
        catch (const xsflib::FormatError&)
        {
        }

        return false;
    };
}

std::optional<std::filesystem::path> UseSoundFiles(disc::GameFiles& game, const GameProfile& profile,
                                                   const std::filesystem::path& folder, std::string* error)
{
    namespace fs = std::filesystem;
    const auto fail = [error](const std::string& message) -> std::optional<fs::path>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    // The start-up archive's folder on the disc ("Audiores/"), and its file name.
    const std::string sound_archive = profile.sound_archive;
    const std::size_t slash = sound_archive.rfind('/');
    const std::string sound_folder = slash == std::string::npos ? "" : sound_archive.substr(0, slash + 1);
    const std::string file_name = sound_archive.substr(sound_folder.size());
    const fs::path file_path = xsflib::PathFromUtf8(file_name);

    std::vector<fs::path> found;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        std::error_code type_error;
        if (it->path().filename() == file_path && it->is_regular_file(type_error))
        {
            found.push_back(it->path().parent_path());
        }
    }

    if (ec)
    {
        return fail(xsflib::Utf8(folder) + ": " + ec.message());
    }

    if (found.empty())
    {
        return fail(xsflib::Utf8(folder) + " holds no " + file_name);
    }

    if (found.size() > 1)
    {
        std::sort(found.begin(), found.end());
        std::string folders;
        for (const fs::path& path : found)
        {
            folders += (folders.empty() ? "" : ", ") + xsflib::Utf8(path);
        }

        return fail(xsflib::Utf8(folder) + " holds more than one " + file_name + " (in " + folders +
                    "). Give the folder of the one to use.");
    }

    // The sound files come from the folder, and the rest from the game. A sound file that the folder lacks belongs to
    // the other release, and isn't read from the game. A sequence table in place of the sequence archive is read into
    // the game's archive.
    const fs::path root = found.front();
    const auto from_game = game.read_file;
    const std::string sequence_archive = profile.sequence_archive ? profile.sequence_archive : "";
    const std::string wave_folder = profile.wave_folder;
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        const bool sound_file = path == sound_archive || (!sequence_archive.empty() && path == sequence_archive) ||
                                path.starts_with(wave_folder);
        if (!sound_file || !path.starts_with(sound_folder))
        {
            return from_game && from_game(path, data);
        }

        const fs::path host = root / xsflib::PathFromUtf8(path.substr(sound_folder.size()));
        if (ReadHostFile(host, data))
        {
            return true;
        }

        return path == sequence_archive && ReadSequenceTable(host.parent_path(), from_game, path, data);
    };

    return root;
}

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
        return jaudio::ReadSounds(jaudio::DecodeIfYaz0(sound_archive, decoded), options);
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

    // Pikmin 2's table lists sequences whose data the game's sound layer gives them. The profile's scene pieces take
    // their place, ahead of the effects (see PieceSounds).
    std::vector<jaudio::SoundEntry> sounds = jaudio::ReadAafSounds(sound_archive);
    if (!profile.scene_pieces.empty())
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

    if (!profile.scene_pieces.empty())
    {
        std::vector<jaudio::SoundEntry> pieces = PieceSounds(profile, sequence_archive);
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
    return jaudio::ReadWaveSystems(jaudio::DecodeIfYaz0(sound_archive, decoded), kBaaOptions);
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
    if (!profile.effect_table)
    {
        return {};
    }

    constexpr std::size_t kRow = 20, kEventTypes = 8, kAction = 6;
    constexpr int32_t kSystem = -2, kPlayer = -1;
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    const std::span<const uint8_t> rows = DolBytes(dol, profile.effect_table);
    const std::span<const uint8_t> offsets = DolBytes(dol, profile.event_offsets);
    if (rows.size() < profile.effect_count * kRow || offsets.size() < kEventTypes * 4)
    {
        throw xsflib::FormatError("main.dol's catalogue of effects runs past its section");
    }

    // An event type's first entry in the table of actions, and the effect that an entry plays.
    const std::span<const uint8_t> actions = offsets.subspan(kEventTypes * 4);
    const auto first_entry = [&offsets](uint32_t type)
    {
        return xsflib::LoadBe32(offsets.data() + 4 * type);
    };
    const auto effect_of = [&actions](uint32_t entry)
    {
        if (actions.size() / kAction <= entry)
        {
            throw xsflib::FormatError("main.dol's table of actions runs past its section");
        }

        return xsflib::LoadBe16(actions.data() + entry * kAction + 4);
    };

    std::vector<Sound> effects;
    std::set<uint32_t> ids;
    const auto add = [&](uint32_t id, std::string label)
    {
        const auto& left_out = profile.left_out_effects;
        if (std::find(left_out.begin(), left_out.end(), id) == left_out.end() && ids.insert(id).second)
        {
            effects.push_back({id, std::move(label), false});
        }
    };

    // Actions of events of different types can play the same effect. The first row that plays an effect names it.
    std::set<uint32_t> event_effects;
    for (uint32_t i = 0; i < profile.effect_count; i++)
    {
        const uint8_t* row = rows.data() + i * kRow;
        const uint32_t number = xsflib::LoadBe32(row + 4);
        std::string name =
            TerminatedText(DolBytes(dol, xsflib::LoadBe32(row + 8)), "main.dol's name of an effect has no end");
        const auto kind = static_cast<int32_t>(xsflib::LoadBe32(row + 16));
        if (kind == kSystem || kind == kPlayer)
        {
            add(JacEffectId(kind == kSystem ? JacEffect::kSystem : JacEffect::kPlayer, number), std::move(name));
            continue;
        }

        if (kind < 1 || kind >= static_cast<int32_t>(kEventTypes))
        {
            throw xsflib::FormatError("main.dol's catalogue of effects has a row of unknown kind");
        }

        // An event of the sounding type starts another effect as the game creates it. The rip plays the effects of
        // that type on an event of the quiet type, by the same entry in the table of actions.
        auto type = static_cast<uint32_t>(kind);
        const uint32_t entry = first_entry(type) + number;
        if (!event_effects.insert(effect_of(entry)).second)
        {
            continue;
        }

        if (type == profile.sounding_event)
        {
            type = profile.quiet_event;
        }

        add(JacEffectId(JacEffect::kEvent, type << 12 | (entry - first_entry(type))), std::move(name));
    }

    for (const uint32_t id : profile.other_effects)
    {
        char label[16];
        std::snprintf(label, sizeof(label), "%08X", static_cast<unsigned>(id));
        add(id, label);
    }

    return effects;
}

std::vector<UnplayedSequence> UnplayedSequences(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                                const std::vector<jaudio::SoundEntry>& sounds,
                                                const std::vector<uint8_t>& sequence_archive)
{
    // Pikmin 2's scene pieces are every piece of music already.
    if (!profile.scene_pieces.empty())
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
    const std::span<const uint8_t> start_up = jaudio::DecodeIfYaz0(sound_archive, decoded_baa);
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

// The arguments of a JAudio2 sequence command: their count, and their types, two bits each from the lowest. Type 0 is
// a byte, 1 is 16 bits, 2 is 24 bits, and 3 is a register's number as a byte.
struct SequenceCommand
{
    bool known = false;
    uint8_t count = 0;
    uint16_t types = 0;
};

// JAudio2's sequence commands from 0xA0, as ntdlib's sequence export reads them.
constexpr std::array<SequenceCommand, 96> MakeSequenceCommands()
{
    std::array<SequenceCommand, 96> c{};
    const auto set = [&c](int command, uint8_t count, uint16_t types)
    {
        c[static_cast<std::size_t>(command - 0xa0)] = {true, count, types};
    };

    set(0xb1, 3, 0x0000); // note on
    set(0xb2, 1, 0x0000); // note off
    set(0xb3, 4, 0x0040); // note
    set(0xb4, 1, 0x0000); // the latest key
    set(0xb8, 2, 0x0000); // a parameter, a byte
    set(0xb9, 2, 0x0004); // a parameter, 16 bits
    set(0xba, 3, 0x0010); // a parameter, a byte and a time
    set(0xbb, 3, 0x0014); // a parameter, 16 bits and a time
    set(0xc1, 2, 0x0008); // open a track
    set(0xc2, 1, 0x0000); // close a track
    set(0xc3, 1, 0x0002); // call
    set(0xc4, 2, 0x0008); // call if
    set(0xc5, 0, 0x0000); // return
    set(0xc6, 1, 0x0000); // return if
    set(0xc7, 1, 0x0002); // jump
    set(0xc8, 2, 0x0008); // jump if
    set(0xc9, 2, 0x000b); // jump through a table
    set(0xca, 2, 0x000b); // call through a table
    set(0xcb, 1, 0x0001); // loop start
    set(0xcc, 0, 0x0000); // loop end
    set(0xd0, 2, 0x0000); // read a port
    set(0xd1, 2, 0x000c); // write a port
    set(0xd2, 1, 0x0000); // check a port's import
    set(0xd3, 1, 0x0000); // check a port's export
    set(0xd4, 2, 0x000c); // write the parent's port
    set(0xd5, 2, 0x000c); // write a child's port
    set(0xd6, 2, 0x0000); // read the parent's port
    set(0xd7, 2, 0x0000); // read a child's port
    set(0xd8, 2, 0x0004); // load a register
    set(0xd9, 3, 0x0030); // register arithmetic with a register
    set(0xda, 3, 0x0010); // register arithmetic with a value
    set(0xdb, 2, 0x0000); // negate a register
    set(0xdc, 4, 0x00e0); // load a register from a table
    set(0xe0, 1, 0x0001); // tempo
    set(0xe1, 1, 0x0001); // bank and program
    set(0xe2, 1, 0x0000); // bank
    set(0xe3, 1, 0x0000); // program
    set(0xe7, 2, 0x0004); // an oscillator's scale
    set(0xe8, 2, 0x0008); // an oscillator's table
    set(0xe9, 5, 0x0155); // a simple ADSR
    set(0xea, 2, 0x0004); // bus connection
    set(0xeb, 1, 0x0000); // IIR cutoff
    set(0xec, 4, 0x0055); // IIR coefficients
    set(0xed, 1, 0x0002); // FIR coefficients
    set(0xf0, 0, 0x0000); // wait, a MIDI value
    set(0xf1, 1, 0x0000); // wait, a byte
    set(0xf3, 1, 0x0002); // interrupt table
    set(0xf4, 1, 0x0001); // enable interrupts
    set(0xf5, 1, 0x0001); // disable interrupts
    set(0xf6, 0, 0x0000); // return from an interrupt
    set(0xf7, 0, 0x0000); // clear the interrupt
    set(0xf8, 2, 0x0004); // interrupt timer
    set(0xf9, 1, 0x0001); // synchronise with the game
    set(0xfd, 0, 0x0000); // print
    set(0xfe, 0, 0x0000); // nothing
    set(0xff, 0, 0x0000); // finish
    return c;
}

constexpr std::array<SequenceCommand, 96> kSequenceCommands = MakeSequenceCommands();

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

                if (command < 0xa0 || !kSequenceCommands[command - 0xa0].known || command == 0xc9 || command == 0xca)
                {
                    return std::nullopt;
                }

                // The arguments. One that comes from a register has no value here.
                const SequenceCommand& info = kSequenceCommands[command - 0xa0];
                std::array<std::optional<uint32_t>, 8> a;
                uint16_t types = info.types | from_registers;
                for (std::size_t i = 0; i < info.count; i++, types = static_cast<uint16_t>(types >> 2))
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

// Walks the command list of a JAudio2 start-up archive, from "AA_<" to ">_AA", and calls visit(tag, at) for each
// command, with `at` the offset of its first argument. The commands' sizes are those that ntdlib's readers skip.
// Throws xsflib::FormatError if the list is damaged or has a command that isn't known.
void VisitCommands(const xsflib::Reader& r, const std::function<void(std::string_view, std::size_t)>& visit)
{
    if (!r.Magic(0, "AA_<"))
    {
        throw xsflib::FormatError("it isn't a BAA archive");
    }

    for (std::size_t at = 4;;)
    {
        const std::span<const uint8_t> bytes = r.Sub(at, 4);
        const std::string_view tag(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        at += 4;
        if (tag == ">_AA")
        {
            return;
        }

        std::size_t size = 0;
        if (tag == "ws  " || tag == "bms ")
        {
            size = 12;
        }
        else if (tag == "bnk " || tag == "bst " || tag == "bstn" || tag == "bsc " || tag == "bl_<" || tag == "vbnk" ||
                 tag == "baac")
        {
            size = 8;
        }
        else if (tag == "bmsa" || tag == "dsqb" || tag == "bsft" || tag == "sect" || tag == "bfca")
        {
            size = 4;
        }
        else if (tag != ">_bl")
        {
            throw xsflib::FormatError("the BAA archive has a command that isn't known");
        }

        visit(tag, at);
        at += size;
    }
}

// The block that the command whose arguments start at `at` gives by its start and its end.
std::span<const uint8_t> BlockAt(const xsflib::Reader& r, std::size_t at)
{
    const uint32_t start = r.U32(at), end = r.U32(at + 4);
    if (end < start)
    {
        throw xsflib::FormatError("the BAA archive is damaged");
    }

    return r.Sub(start, end - start);
}

// The start-up archives in the "baac" block of start-up archive `baa`, in the block's order. The block holds the
// archives' count, each one's offset in the block, then the archives. Throws xsflib::FormatError if the archive is
// damaged.
std::vector<std::span<const uint8_t>> BaacArchives(std::span<const uint8_t> baa)
{
    const xsflib::Reader r(baa, true);
    std::span<const uint8_t> block;
    const auto find_block = [&r, &block](std::string_view tag, std::size_t at)
    {
        if (tag == "baac")
        {
            block = BlockAt(r, at);
        }
    };
    VisitCommands(r, find_block);

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
        const auto read_bank = [&a, &numbers](std::string_view tag, std::size_t at)
        {
            if (tag == "bnk ")
            {
                numbers.insert(a.U32(a.U32(at + 4) + 8));
            }
        };
        VisitCommands(a, read_bank);
    }

    // The sequence collection: "SC", the categories' count, then each category's table of sequence offsets.
    const xsflib::Reader r(baa, true);
    std::span<const uint8_t> collection;
    const auto find_collection = [&r, &collection](std::string_view tag, std::size_t at)
    {
        if (tag == "bsc ")
        {
            collection = BlockAt(r, at);
        }
    };
    VisitCommands(r, find_collection);

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

} // namespace

std::optional<std::string> ListJAudioSounds(const disc::GameFiles& game, const GameProfile& profile, GameSounds& listed)
{
    if (!game.read_file(profile.sound_archive, listed.sound_archive))
    {
        return std::string("can't read ") + profile.sound_archive;
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
    if (profile.sound_names && !game.read_file(profile.sound_names, names))
    {
        return std::string("can't read ") + profile.sound_names;
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
    if (!game.read_file || !game.read_file(profile.sound_archive, banks))
    {
        return std::string("can't read ") + profile.sound_archive;
    }

    // Whether the rip loads group `group` of wave system `system`. At start-up, the sound library loads the effects'
    // first group, group 0 of system 0. The driver loads the group that a piece's ID gives, and the profile's groups
    // for an effect (driver/pikmin.c). Without `sounds`, the rip loads every group.
    const auto loads = [sounds, &profile](uint32_t system, uint32_t group)
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
    };

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
                if (std::find(paths.begin(), paths.end(), path) == paths.end())
                {
                    paths.push_back(path);
                }

                if (loads(number, static_cast<uint32_t>(g)))
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
        if (!game.read_file || !game.read_file(path, bytes))
        {
            return "can't read " + path;
        }

        program.AddFile(path, loaded.contains(path) ? bytes : std::vector<uint8_t>(bytes.size(), 0));
    }

    return std::nullopt;
}

std::optional<std::string> AddJAudioFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                          const GameProfile& profile, const std::vector<std::string>* wave_archives,
                                          const std::vector<uint32_t>* sounds)
{
    std::vector<uint8_t> sound_archive;
    if (!game.read_file || !game.read_file(profile.sound_archive, sound_archive))
    {
        return std::string("can't read ") + profile.sound_archive;
    }

    std::vector<std::string> paths;
    if (profile.sequence_archive)
    {
        paths.emplace_back(profile.sequence_archive);
    }

    paths.insert(paths.end(), profile.other_sound_files.begin(), profile.other_sound_files.end());
    try
    {
        const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
        std::vector<std::string> names;
        if (wave_archives)
        {
            names = *wave_archives;
            std::vector<std::string> staying;
            if (jaudio1)
            {
                staying = jaudio::AafStayingWaveArchiveNames(sound_archive);
                for (const std::string& name :
                     jaudio::GroupArchives(jaudio::ReadAafWaveSystems(sound_archive), profile.staying_groups))
                {
                    staying.push_back(name);
                }
            }

            for (const std::string& name : staying)
            {
                if (std::find(names.begin(), names.end(), name) == names.end())
                {
                    names.push_back(name);
                }
            }
        }
        else
        {
            std::vector<uint8_t> decoded;
            names = jaudio1 ? jaudio::AafWaveArchiveNames(sound_archive)
                            : jaudio::WaveArchiveNames(jaudio::DecodeIfYaz0(sound_archive, decoded), kBaaOptions);
        }

        // The wave archives of the "baac" block's archives that the sounds load.
        if (profile.baac_archives)
        {
            std::vector<uint8_t> decoded;
            const std::vector<std::span<const uint8_t>> archives =
                BaacArchives(jaudio::DecodeIfYaz0(sound_archive, decoded));
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
                    if (std::find(names.begin(), names.end(), name) == names.end())
                    {
                        names.push_back(name);
                    }
                }
            }
        }

        for (const std::string& name : names)
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
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }

    program.AddFile(profile.sound_archive, sound_archive);

    return AddDiscFiles(program, game, paths);
}

namespace
{

// What the length analysis watches for sound `id`: the profile's `watch`, with `jaudio1` from its sound library. The
// conductor of one of Pikmin 2's AutoBgm pieces picks what the tracks play, at random. The piece never repeats. Once
// its root track idles in its polling routine, the passes of its other tracks would still look like a loop's. Its root
// track's polls therefore stay moves. They hold back every loop, and the piece plays to the analysis's limit.
ntdlib::jaudio::GameWatch WatchOf(const GameProfile& profile, uint32_t id)
{
    ntdlib::jaudio::GameWatch watch = profile.watch;
    watch.jaudio1 = profile.library == SoundLibrary::kJAudio1 || profile.library == SoundLibrary::kJac;
    const ScenePiece* piece = PieceOf(profile, id);
    if (piece && (piece->kind == ScenePiece::Kind::kAuto || piece->kind == ScenePiece::Kind::kAutoFree))
    {
        watch.idle_in_calls = false;
    }

    return watch;
}

// The most levels of parents that the test of a track's sound climbs.
constexpr int kMaxTrackDepth = 16;

// The CPU as ntdlib's analysis reads it: the registers, and the watch of the game's code. The watch leaves out the
// events at the places for which `skip` returns true. The registers keep the name that ppc::Cpu gives them. The
// analysis reads them by that name.
class SkippingCpu
{
public:
    SkippingCpu(ppc::Cpu& cpu, std::function<bool(uint32_t pc)> skip)
        : state_(cpu.state_), cpu_(cpu), skip_(std::move(skip))
    {
    }

    void Watch(std::vector<uint32_t> pcs, std::function<void(uint32_t pc)> on_watch)
    {
        if (!on_watch)
        {
            cpu_.Watch(std::move(pcs), nullptr);
            return;
        }

        cpu_.Watch(std::move(pcs),
                   [this, on_watch = std::move(on_watch)](uint32_t pc)
                   {
                       if (!skip_(pc))
                       {
                           on_watch(pc);
                       }
                   });
    }

    ppc::State& state_;

private:
    ppc::Cpu& cpu_;
    std::function<bool(uint32_t pc)> skip_;
};

// GCSF's player as ntdlib's analysis reads it, without the tracks of the sound that the driver plays beside the ripped
// one (GameProfile::background_sound).
class JAudioMachine
{
public:
    JAudioMachine(Player& player, const GameProfile& profile)
        : machine_(player),
          profile_(profile),
          cpu_(player.System()->Cpu(), [this](uint32_t pc) { return OnBackgroundTrack(pc); })
    {
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        return machine_.Render(out, frames);
    }

    uint32_t Word(uint32_t address) const
    {
        return machine_.Word(address);
    }

    std::optional<uint8_t> Byte(uint32_t address) const
    {
        return machine_.Byte(address);
    }

    SkippingCpu& Cpu()
    {
        return cpu_;
    }

private:
    // Whether the watched instruction at `pc` parses a track of the sound that the driver plays beside the ripped one.
    // A track belongs to that sound if it or one of its parents is the sound's root track. The register that holds the
    // track is the one that ntdlib's analysis reads.
    bool OnBackgroundTrack(uint32_t pc) const
    {
        const ntdlib::jaudio::GameWatch& watch = profile_.watch;
        const bool conditional = watch.conditional_jump != 0 && pc == watch.conditional_jump;
        if (profile_.background_sound < 0 || (pc != watch.parse_command && pc != watch.jump_handler && !conditional))
        {
            return false;
        }

        const uint32_t sound = Word(DriverBlock::kAddress + DriverBlock::kListedProfileOffset +
                                    4 * static_cast<uint32_t>(profile_.background_sound));
        if (sound == 0 || Word(sound + watch.vtable_offset) != watch.sequence_vtable)
        {
            return false;
        }

        const uint32_t root = sound + watch.sequence_track_offset;
        const uint32_t reg = pc == watch.parse_command ? watch.command_track_register
                             : conditional             ? watch.conditional_track_register
                                                       : watch.jump_track_register;
        uint32_t track = cpu_.state_.gpr[reg];
        for (int level = 0; track != 0 && level < kMaxTrackDepth; level++)
        {
            if (track == root)
            {
                return true;
            }

            track = Word(track + profile_.track_parent_offset);
        }

        return false;
    }

    AnalysisMachine machine_;
    const GameProfile& profile_;
    SkippingCpu cpu_;
};

} // namespace

std::optional<std::vector<uint32_t>> GameWaveGroups(const GameProfile& profile, uint32_t id)
{
    const ScenePiece* piece = PieceOf(profile, id);
    if (!piece || piece->wave_scene == ScenePiece::kFindWaves)
    {
        return std::nullopt;
    }

    // Group 0 holds the effects and stays loaded. The driver loads the scene's group.
    std::vector<uint32_t> groups = {0};
    if (piece->wave_scene != 0)
    {
        groups.push_back(piece->wave_scene);
    }

    return groups;
}

std::optional<Timing> MeasureJAudioSound(Player& player, const GameProfile& profile, uint32_t id,
                                         std::set<jaudio::WaveUse>* found, bool lookups_sound)
{
    const uint32_t handle = DriverBlock::kAddress + DriverBlock::kHandleOffset;
    if (profile.background_sound >= 0)
    {
        JAudioMachine machine(player, profile);
        return ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), kLengthRules, found,
                                                lookups_sound, id);
    }

    AnalysisMachine machine(player);
    return ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), kLengthRules, found, lookups_sound,
                                            id);
}

} // namespace gcsf::rip
