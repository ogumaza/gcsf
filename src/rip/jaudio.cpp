// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is JAudio2, JAudio1 or Jac: the start-up archive and another
// release's sound files, the sound table and its names, the sequences that no sound plays, Pikmin 2's pieces, the
// library's sound files, and the length analysis through ntdlib's AnalyzeGameSound.

#include <algorithm>
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
    constexpr uint32_t kFirstPiece = 2;
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
    for (uint32_t number = kFirstPiece; number < count; number++)
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

    for (const jaudio::SoundEntry& entry : entries)
    {
        listed.sounds.push_back(SoundOf(entry));
    }

    return std::nullopt;
}

std::optional<std::string> AddJacFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    std::vector<uint8_t> banks;
    if (!game.read_file || !game.read_file(profile.sound_archive, banks))
    {
        return std::string("can't read ") + profile.sound_archive;
    }

    std::vector<std::string> paths = {profile.sequence_archive};
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

            for (const jaudio::WaveGroup& group : jaudio::ReadWaveSystem(r.Sub(offset, size), number).groups)
            {
                const std::string path = profile.wave_folder + group.archive;
                if (!group.archive.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
                {
                    paths.push_back(path);
                }
            }
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.sound_archive) + ": " + e.what();
    }

    program.AddFile(profile.sound_archive, banks);

    return AddDiscFiles(program, game, paths);
}

std::optional<std::string> AddJAudioFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                          const GameProfile& profile, const std::vector<std::string>* wave_archives)
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
    AnalysisMachine machine(player);
    return ntdlib::jaudio::AnalyzeGameSound(machine, DriverBlock::kAddress + DriverBlock::kHandleOffset,
                                            WatchOf(profile, id), kLengthRules, found, lookups_sound, id);
}

} // namespace gcsf::rip
