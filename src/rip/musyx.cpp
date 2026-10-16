// SPDX-License-Identifier: MIT

#include "rip/musyx.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/compression.h"
#include "ntdlib/dol.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/text.h"

#include "rip/fsys.h"

namespace gcsf::rip
{
namespace
{

// A file of the game's disc. Throws xsflib::FormatError if it can't be read.
std::vector<uint8_t> ReadGameFile(const disc::GameFiles& game, const std::string& path)
{
    std::vector<uint8_t> bytes;
    if (!game.read_file || !game.read_file(path, bytes))
    {
        throw xsflib::FormatError("can't read " + path);
    }

    return bytes;
}

// `size` bytes at `offset` of `data`. Throws xsflib::FormatError if they run past its end.
std::span<const uint8_t> Part(std::span<const uint8_t> data, std::size_t offset, std::size_t size, const char* what)
{
    if (offset > data.size() || size > data.size() - offset)
    {
        throw xsflib::FormatError(std::string(what) + " runs past the end of its file");
    }

    return data.subspan(offset, size);
}

// A resource of a Retro Studios pack: its type, its ID, its name if the pack names it, and its data, decompressed.
struct PakResource
{
    std::string type;
    uint32_t id = 0;
    std::string name;
    std::vector<uint8_t> data;
};

// The resources of a pack whose type is in `types` (Metroid Prime's pack format, version 3.5). The header gives the
// version, then the named resources: each one's type, ID, and the length and bytes of its name. A table follows: the
// number of resources, then for each one whether it's compressed, its type, its ID, its size and its offset. A
// compressed resource holds its size once decompressed, then a zlib stream. Throws xsflib::FormatError if the pack is
// damaged.
std::vector<PakResource> ReadPak(std::span<const uint8_t> pak, const std::vector<std::string>& types)
{
    constexpr uint16_t kMajor = 3, kMinor = 5;
    if (pak.size() < 12 || xsflib::LoadBe16(pak.data()) != kMajor || xsflib::LoadBe16(pak.data() + 2) != kMinor)
    {
        throw xsflib::FormatError("a pack isn't of version 3.5");
    }

    std::size_t at = 8;
    const auto word = [&pak, &at]()
    {
        const uint32_t value = xsflib::LoadBe32(Part(pak, at, 4, "a pack's table").data());
        at += 4;
        return value;
    };
    const auto four_cc = [&pak, &at]()
    {
        const std::span<const uint8_t> bytes = Part(pak, at, 4, "a pack's table");
        at += 4;
        return std::string(bytes.begin(), bytes.end());
    };

    std::map<std::pair<std::string, uint32_t>, std::string> names;
    const uint32_t named = word();
    for (uint32_t i = 0; i < named; i++)
    {
        const std::string type = four_cc();
        const uint32_t id = word();
        const uint32_t length = word();
        const std::span<const uint8_t> name = Part(pak, at, length, "a pack's name");
        at += length;
        names[{type, id}] = std::string(name.begin(), std::find(name.begin(), name.end(), uint8_t{0}));
    }

    std::vector<PakResource> resources;
    const uint32_t count = word();
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t compressed = word();
        PakResource resource;
        resource.type = four_cc();
        resource.id = word();
        const uint32_t size = word();
        const uint32_t offset = word();
        if (std::find(types.begin(), types.end(), resource.type) == types.end())
        {
            continue;
        }

        const std::span<const uint8_t> stored = Part(pak, offset, size, "a pack's resource");
        if (compressed)
        {
            const uint32_t full = xsflib::LoadBe32(Part(stored, 0, 4, "a compressed resource").data());
            resource.data.resize(full);
            try
            {
                resource.data.resize(ntdlib::disc::Inflate(stored.subspan(4), resource.data));
            }
            catch (const std::exception&)
            {
                throw xsflib::FormatError("a pack's compressed resource is damaged");
            }
        }
        else
        {
            resource.data.assign(stored.begin(), stored.end());
        }

        const auto name = names.find({resource.type, resource.id});
        if (name != names.end())
        {
            resource.name = name->second;
        }

        resources.push_back(std::move(resource));
    }

    return resources;
}

// An audio group set (AGSC) as MusyX's four files, and the set's name.
struct AudioGroupSet
{
    std::string name;
    MusyXCatalogue::FileSet files;
};

// The string that starts at `at` in `agsc`. Moves `at` past its end. Throws xsflib::FormatError if it doesn't end.
std::string SetString(std::span<const uint8_t> agsc, std::size_t& at)
{
    const auto start = agsc.begin() + static_cast<std::ptrdiff_t>(std::min(at, agsc.size()));
    const auto end = std::find(start, agsc.end(), uint8_t{0});
    if (end == agsc.end())
    {
        throw xsflib::FormatError("an audio group set has no name");
    }

    at = static_cast<std::size_t>(end - agsc.begin()) + 1;

    return std::string(start, end);
}

// An audio group set in either of its layouts. Metroid Prime's set starts with two strings: a folder and the set's
// name. The pool, the project, the samples and the sample directory follow. A word before each gives its size. Metroid
// Prime 2: Echoes' set starts with a word 1, the set's name, and its group's ID in a big-endian half. Four words give
// the sizes of the pool, the project, the sample directory and the samples. The four parts follow in that order. A
// folder's name never starts with a byte 0. A set that starts with the word 1 therefore has the second layout.
AudioGroupSet ReadAudioGroupSet(std::span<const uint8_t> agsc)
{
    constexpr uint32_t kEchoesLayout = 1;
    AudioGroupSet set;
    std::size_t at = 0;
    if (xsflib::LoadBe32(Part(agsc, 0, 4, "an audio group set").data()) == kEchoesLayout)
    {
        at = 4;
        set.name = SetString(agsc, at);
        at += 2;
        const std::span<const uint8_t> sizes = Part(agsc, at, 16, "an audio group set");
        at += 16;
        std::vector<uint8_t>* parts[] = {&set.files.pool, &set.files.project, &set.files.sdir, &set.files.samples};
        for (std::size_t i = 0; i < std::size(parts); i++)
        {
            const uint32_t size = xsflib::LoadBe32(sizes.data() + i * 4);
            const std::span<const uint8_t> bytes = Part(agsc, at, size, "an audio group set");
            parts[i]->assign(bytes.begin(), bytes.end());
            at += size;
        }

        return set;
    }

    SetString(agsc, at);
    set.name = SetString(agsc, at);
    const auto part = [&agsc, &at]()
    {
        const uint32_t size = xsflib::LoadBe32(Part(agsc, at, 4, "an audio group set").data());
        const std::span<const uint8_t> bytes = Part(agsc, at + 4, size, "an audio group set");
        at += 4 + size;
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };

    set.files.pool = part();
    set.files.project = part();
    set.files.samples = part();
    set.files.sdir = part();

    return set;
}

// An effect of a project's effects group: the group's ID and the effect's ID.
struct ProjectEffect
{
    uint16_t group = 0;
    uint16_t id = 0;
};

// The effects of the effects groups in `project`, a MusyX project file, in the file's order. A group's header starts
// with the offset of the next group (0xFFFFFFFF ends the list), then the group's ID and its type (1 for effects) in
// big-endian halves. An effects group's header gives the offset of its table of effects at 0x1C. The table holds the
// number of effects in a big-endian half, two more bytes, and 10 bytes for each effect, starting with the effect's ID
// in a big-endian half. The byte at +4 of an effect's row limits the voices that play the effect at once. MusyX never
// starts an effect whose limit is 0. The list leaves such an effect out. Every offset counts from the start of the
// file. Throws xsflib::FormatError if the file is damaged.
std::vector<ProjectEffect> ProjectEffects(std::span<const uint8_t> project)
{
    constexpr uint32_t kEndOfGroups = 0xffffffff;
    constexpr uint16_t kEffectsGroup = 1;
    constexpr std::size_t kGroupHeader = 0x28, kEffectsTable = 0x1c, kEffect = 10, kVoices = 4;
    std::vector<ProjectEffect> effects;
    std::vector<uint32_t> seen;
    for (uint32_t at = 0;;)
    {
        const std::span<const uint8_t> header = Part(project, at, 4, "a project's group");
        const uint32_t next = xsflib::LoadBe32(header.data());
        if (next == kEndOfGroups)
        {
            break;
        }

        if (std::find(seen.begin(), seen.end(), at) != seen.end())
        {
            throw xsflib::FormatError("a project's groups run in a circle");
        }

        seen.push_back(at);
        const std::span<const uint8_t> group = Part(project, at, kGroupHeader, "a project's group");
        const uint16_t id = xsflib::LoadBe16(group.data() + 4);
        const uint32_t table = xsflib::LoadBe32(group.data() + kEffectsTable);
        if (xsflib::LoadBe16(group.data() + 6) == kEffectsGroup && table != 0)
        {
            const uint16_t count = xsflib::LoadBe16(Part(project, table, 2, "a group's effects").data());
            const std::span<const uint8_t> rows = Part(project, table + 4, count * kEffect, "a group's effects");
            for (std::size_t i = 0; i < count; i++)
            {
                const uint8_t* row = rows.data() + i * kEffect;
                if (row[kVoices] != 0)
                {
                    effects.push_back({id, xsflib::LoadBe16(row)});
                }
            }
        }

        at = next;
    }

    return effects;
}

// The names in a string table (STLC): the number of strings in a big-endian word, then the strings. A byte 0 ends each
// string. Metroid Prime 2: Echoes' table audio_name_lookup names its effects by their IDs: string n names effect n.
// Throws xsflib::FormatError if the table is damaged.
std::vector<std::string> StringTable(std::span<const uint8_t> table)
{
    const uint32_t count = xsflib::LoadBe32(Part(table, 0, 4, "a string table").data());
    std::vector<std::string> names;
    std::size_t at = 4;
    for (uint32_t i = 0; i < count; i++)
    {
        const auto start = table.begin() + static_cast<std::ptrdiff_t>(std::min(at, table.size()));
        const auto end = std::find(start, table.end(), uint8_t{0});
        if (end == table.end())
        {
            throw xsflib::FormatError("a string table's string doesn't end");
        }

        names.emplace_back(start, end);
        at = static_cast<std::size_t>(end - table.begin()) + 1;
    }

    return names;
}

// Metroid Prime's and Metroid Prime 2: Echoes' sounds (see MusyXFiles::kRetro). MidiData.pak holds the songs as CSNG
// resources: a word 2, MusyX's song ID (its MIDI set-up), the song's group, the audio group set that holds the group,
// the song's size and the song. The game plays a song with sndSeqPlayEx, the group and the ID. The audio group sets are
// AGSC resources of the packs. Metroid Prime 2 keeps Metroid Prime's four songs. None of its packs holds the set that
// they name. The songs can't play. The catalogue leaves them out.
//
// The effects follow the songs, in the order of their IDs: every effect of each set's effects group. The game plays an
// effect with sndFXStartEx and the effect's ID once it has pushed the group. A rip plays it at the volume and the pan
// that the group's table gives it. A pack's string table audio_name_lookup names the effects by their IDs
// (StringTable). Metroid Prime 2's AudioGrp.pak holds one. An effect without a name there is named after its set and
// its ID, such as "Scarab-0721".
MusyXCatalogue GatherRetro(const disc::GameFiles& game)
{
    MusyXCatalogue catalogue;
    const std::vector<uint8_t> midi = ReadGameFile(game, "MidiData.pak");
    std::vector<PakResource> songs = ReadPak(midi, {"CSNG"});

    // The audio group sets, by ID, from every pack, and the effects' names. A set becomes a file set when a sound uses
    // it.
    std::map<uint32_t, AudioGroupSet> sets;
    std::vector<std::string> names;
    for (const disc::GameFile& file : game.files)
    {
        if (!xsflib::AsciiLower(file.path).ends_with(".pak"))
        {
            continue;
        }

        for (PakResource& resource : ReadPak(ReadGameFile(game, file.path), {"AGSC", "STLC"}))
        {
            if (resource.type == "STLC" && resource.name.starts_with("audio_name_lookup"))
            {
                names = StringTable(resource.data);
            }
            else if (resource.type == "AGSC" && !sets.contains(resource.id))
            {
                sets.emplace(resource.id, ReadAudioGroupSet(resource.data));
            }
        }
    }

    // The effects of every set, by ID. Two sets that give one ID would push different groups for it. The first set
    // wins.
    struct Effect
    {
        uint16_t id = 0;
        uint16_t group = 0;
        uint32_t set = 0;
    };
    std::vector<Effect> effects;
    for (const auto& [id, set] : sets)
    {
        if (set.files.project.empty())
        {
            continue;
        }

        for (const ProjectEffect& effect : ProjectEffects(set.files.project))
        {
            effects.push_back({.id = effect.id, .group = effect.group, .set = id});
        }
    }

    std::stable_sort(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) { return a.id < b.id; });
    const auto same_id = [](const Effect& a, const Effect& b)
    {
        return a.id == b.id;
    };
    effects.erase(std::unique(effects.begin(), effects.end(), same_id), effects.end());

    std::map<uint32_t, uint16_t> set_numbers;
    const auto set_number = [&](uint32_t set)
    {
        auto [number, added] = set_numbers.try_emplace(set, static_cast<uint16_t>(catalogue.sets.size()));
        if (added)
        {
            catalogue.sets.push_back(sets.at(set).files);
        }

        return number->second;
    };

    for (const PakResource& song : songs)
    {
        constexpr std::size_t kHeader = 20;
        const std::span<const uint8_t> head = Part(song.data, 0, kHeader, "a song");
        const uint32_t id = xsflib::LoadBe32(head.data() + 4);
        const uint32_t group = xsflib::LoadBe32(head.data() + 8);
        const uint32_t set = xsflib::LoadBe32(head.data() + 12);
        const uint32_t size = xsflib::LoadBe32(head.data() + 16);
        const std::span<const uint8_t> data = Part(song.data, kHeader, size, "a song");
        if (!sets.contains(set))
        {
            continue;
        }

        MusyXCatalogue::Entry entry;
        entry.label = song.name.empty() ? xsflib::Hex(song.id, 8) : song.name;
        entry.id = static_cast<uint16_t>(id);
        entry.song_group = static_cast<uint16_t>(group);
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        entry.groups = {{static_cast<uint16_t>(group), set_number(set)}};
        catalogue.songs.emplace_back(data.begin(), data.end());
        catalogue.entries.push_back(std::move(entry));
    }

    for (const Effect& effect : effects)
    {
        MusyXCatalogue::Entry entry;
        if (effect.id < names.size() && !names[effect.id].empty())
        {
            entry.label = names[effect.id];
        }
        else
        {
            char number[8];
            std::snprintf(number, sizeof(number), "%04u", static_cast<unsigned>(effect.id));
            entry.label = sets.at(effect.set).name + "-" + number;
        }

        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, set_number(effect.set)}};
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// A file of the game's disc whose path matches `path` in any case. Tales of Symphonia's code names its files in lower
// case, and the disc's folder is "S". The game's disc library matches names in any case. Throws xsflib::FormatError if
// the disc has no such file.
std::vector<uint8_t> ReadGameFileAnyCase(const disc::GameFiles& game, const std::string& path)
{
    const std::string lower = xsflib::AsciiLower(path);
    for (const disc::GameFile& file : game.files)
    {
        if (xsflib::AsciiLower(file.path) == lower)
        {
            return ReadGameFile(game, file.path);
        }
    }

    throw xsflib::FormatError("the disc has no " + path);
}

// MusyX's four files from one file of Tales of Symphonia. The file starts with the number of its parts, 4, then gives
// each part's offset and size, a big-endian word each. The parts are the project, the pool, the sample directory and
// the samples.
MusyXCatalogue::FileSet SoundFile(std::span<const uint8_t> file)
{
    constexpr uint32_t kParts = 4;
    if (file.size() < 4 + kParts * 8 || xsflib::LoadBe32(file.data()) != kParts)
    {
        throw xsflib::FormatError("a sound file doesn't hold four parts");
    }

    std::vector<uint8_t> parts[kParts];
    for (uint32_t i = 0; i < kParts; i++)
    {
        const uint32_t offset = xsflib::LoadBe32(file.data() + 4 + i * 8);
        const uint32_t size = xsflib::LoadBe32(file.data() + 8 + i * 8);
        const std::span<const uint8_t> part = Part(file, offset, size, "a sound file's part");
        parts[i].assign(part.begin(), part.end());
    }

    return {.project = std::move(parts[0]),
            .pool = std::move(parts[1]),
            .sdir = std::move(parts[2]),
            .samples = std::move(parts[3])};
}

// The words that give a reverb its settings (MusyXReverb). SND_AUX_REVERBSTD holds them from 0x140: the coloration,
// the mix, the time, the damping and the pre-delay, as floats. SND_AUX_REVERBHI holds them from 0x1C8, and the
// crosstalk after them. A reverb at 0 has none.
void AddReverb(std::vector<MusyXCatalogue::Write>& writes, const MusyXReverb& reverb)
{
    constexpr uint32_t kStandardSettings = 0x140, kHighSettings = 0x1c8;
    if (reverb.block == 0)
    {
        return;
    }

    const float settings[] = {reverb.coloration, reverb.mix,       reverb.time,
                              reverb.damping,    reverb.pre_delay, reverb.crosstalk};
    const uint32_t at = reverb.block + (reverb.high ? kHighSettings : kStandardSettings);
    const std::size_t count = std::size(settings) - (reverb.high ? 0 : 1);
    for (std::size_t i = 0; i < count; i++)
    {
        writes.push_back({at + static_cast<uint32_t>(i) * 4, std::bit_cast<uint32_t>(settings[i])});
    }
}

// Tales of Symphonia's songs (see MusyXFiles::kTales). The game's code lists them at `musyx_songs`, 8 bytes each: the
// address of the file's name, then MusyX's song ID and the song's kind, a big-endian half each. A song ID of 0xFFFF
// ends the list. The game plays a song through sndSeqPlayEx, with the song ID and the group of the first file in
// `bank_files`. The song plays at the music's volume in the game's options. The byte at `song_reverbs` plus the song ID
// picks aux A's reverb.
MusyXCatalogue GatherTales(const disc::GameFiles& game, const GameProfile& profile)
{
    // The music's volume in the game's options: its default (0x80081D80) and its maximum.
    constexpr uint8_t kMusicVolume = 0x7f;
    constexpr std::size_t kEntry = 8;
    constexpr uint16_t kEnd = 0xffff;
    constexpr uint8_t kOtherReverb = 2;

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    MusyXCatalogue catalogue;
    for (const char* bank : profile.bank_files)
    {
        catalogue.sets.push_back(SoundFile(ReadGameFileAnyCase(game, bank)));
    }

    if (catalogue.sets.empty())
    {
        throw xsflib::FormatError(std::string(profile.name) + " has no MusyX group for its songs");
    }

    // A project's first group starts with the offset of the next group, then the group's ID in a big-endian half.
    const uint16_t group = xsflib::LoadBe16(Part(catalogue.sets[0].project, 4, 2, "a project").data());
    for (uint32_t at = profile.musyx_songs;; at += kEntry)
    {
        const std::span<const uint8_t> row = Part(DolBytes(*dol, at), 0, kEntry, "the song table");
        const uint16_t id = xsflib::LoadBe16(row.data() + 4);
        if (id == kEnd)
        {
            break;
        }

        const std::span<const uint8_t> name = DolBytes(*dol, xsflib::LoadBe32(row.data()));
        const std::string path(name.begin(), std::find(name.begin(), name.end(), uint8_t{0}));
        const std::size_t slash = path.find_last_of('/');
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);

        MusyXCatalogue::Entry entry;
        entry.label = file.substr(0, file.find_last_of('.'));
        entry.volume = kMusicVolume;
        entry.id = id;
        entry.song_group = group;
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        entry.groups = {{group, 0}};
        const bool other = profile.song_reverbs != 0 && DolBytes(*dol, profile.song_reverbs + id)[0] == kOtherReverb;
        AddReverb(entry.writes, other ? profile.other_reverb : profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        catalogue.songs.push_back(ReadGameFileAnyCase(game, path));
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// MusyX's four files under one name: the project (.pro), the pool (.poo), the sample directory (.sdi) and the
// samples (.sam).
MusyXCatalogue::FileSet FileSetNamed(const disc::GameFiles& game, const std::string& stem)
{
    return {.project = ReadGameFile(game, stem + ".pro"),
            .pool = ReadGameFile(game, stem + ".poo"),
            .sdir = ReadGameFile(game, stem + ".sdi"),
            .samples = ReadGameFile(game, stem + ".sam")};
}

// The volume that most of a song's triggers give it. Of volumes that tie, the one that comes first wins.
uint8_t MostCommon(const std::vector<uint8_t>& volumes)
{
    uint8_t best = volumes.front();
    std::ptrdiff_t best_count = 0;
    for (const uint8_t volume : volumes)
    {
        const std::ptrdiff_t count = std::count(volumes.begin(), volumes.end(), volume);
        if (count > best_count)
        {
            best = volume;
            best_count = count;
        }
    }

    return best;
}

// Star Fox Adventures' songs (see MusyXFiles::kStarFox). The game's code lists them at `musyx_songs`, in
// `musyx_song_count` entries of 16 bytes. An entry gives MusyX's song ID in a big-endian half, then the song's group
// in a byte. The address of the song's name follows at +4. The game fills in the entry's last two words.
// audio/midi.wad holds the songs by ID: their sizes in 100 big-endian words at its start, then the songs from 0x1A0,
// each padded to 32 bytes. The game plays a song through sndSeqPlayEx, with the song's group and ID. At start-up it
// pushes group 0 of the starfoxm files, then groups 1 to 55 of the starfoxs files, and keeps them all. A rip pushes
// them in the same order.
//
// The game starts its music through triggers: the entries of audio/data/Music.bin, 16 bytes each. An entry gives its
// trigger's number and the song's ID in big-endian halves at its start, and the song's volume in its byte at 0xC. A
// song plays at the volume that most of its triggers give it. Trigger 0xEC plays a song that its caller picks. A song
// that no other trigger names takes that trigger's volume.
MusyXCatalogue GatherStarFox(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kSongEntry = 16;
    constexpr uint32_t kWadSongs = 100;
    constexpr std::size_t kWadHeader = 0x1a0;
    constexpr std::size_t kTrigger = 16;
    constexpr uint16_t kPickedSong = 0xec;
    constexpr uint16_t kLastGroup = 55;

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    // The volumes that the triggers give each song, in the file's order.
    const std::vector<uint8_t> triggers = ReadGameFile(game, "audio/data/Music.bin");
    std::map<uint16_t, std::vector<uint8_t>> volumes;
    uint8_t picked_volume = MusyXCatalogue::kDefaultVolume;
    for (std::size_t at = 0; at + kTrigger <= triggers.size(); at += kTrigger)
    {
        const uint8_t* trigger = triggers.data() + at;
        if (xsflib::LoadBe16(trigger) == kPickedSong)
        {
            picked_volume = trigger[0xc];
        }
        else
        {
            volumes[xsflib::LoadBe16(trigger + 2)].push_back(trigger[0xc]);
        }
    }

    // The songs, by ID.
    const std::vector<uint8_t> wad = ReadGameFile(game, "audio/midi.wad");
    const std::span<const uint8_t> sizes = Part(wad, 0, kWadHeader, "audio/midi.wad's table");
    std::vector<std::span<const uint8_t>> songs;
    std::size_t at = kWadHeader;
    for (uint32_t id = 0; id < kWadSongs; id++)
    {
        const uint32_t size = xsflib::LoadBe32(sizes.data() + id * 4);
        songs.push_back(Part(wad, at, size, "a song of audio/midi.wad"));
        at += (std::size_t{size} + 31) & ~std::size_t{31};
    }

    struct Song
    {
        uint16_t id = 0;
        uint8_t group = 0;
        std::string name;
    };
    std::vector<Song> table;
    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const std::span<const uint8_t> row =
            Part(DolBytes(*dol, profile.musyx_songs + i * static_cast<uint32_t>(kSongEntry)), 0, kSongEntry,
                 "the song table");
        const std::span<const uint8_t> name = DolBytes(*dol, xsflib::LoadBe32(row.data() + 4));
        table.push_back({.id = xsflib::LoadBe16(row.data()),
                         .group = row[2],
                         .name = std::string(name.begin(), std::find(name.begin(), name.end(), uint8_t{0}))});
    }

    std::stable_sort(table.begin(), table.end(), [](const Song& a, const Song& b) { return a.id < b.id; });

    MusyXCatalogue catalogue;
    catalogue.sets.push_back(FileSetNamed(game, "audio/starfoxm"));
    catalogue.sets.push_back(FileSetNamed(game, "audio/starfoxs"));
    std::vector<MusyXCatalogue::Group> groups = {{.id = 0, .set = 0}};
    for (uint16_t group = 1; group <= kLastGroup; group++)
    {
        groups.push_back({.id = group, .set = 1});
    }

    for (const Song& song : table)
    {
        if (song.id >= kWadSongs)
        {
            throw xsflib::FormatError("audio/midi.wad has no song " + xsflib::Hex(song.id, 4));
        }

        const auto found = volumes.find(song.id);
        MusyXCatalogue::Entry entry;
        entry.label = song.name.empty() ? xsflib::Hex(song.id, 4) : song.name;
        entry.volume = found == volumes.end() ? picked_volume : MostCommon(found->second);
        entry.id = song.id;
        entry.song_group = song.group;
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        entry.groups = groups;
        AddReverb(entry.writes, profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        catalogue.songs.emplace_back(songs[song.id].begin(), songs[song.id].end());
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// Converts a UTF-16 string in big-endian halves to UTF-8. The string ends at a half of 0. A lone surrogate becomes
// U+FFFD.
std::string Utf16Be(std::span<const uint8_t> bytes)
{
    std::string out;
    const auto put = [&out](uint32_t c)
    {
        if (c < 0x80)
        {
            out += static_cast<char>(c);
        }
        else if (c < 0x800)
        {
            out += static_cast<char>(0xc0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xe0 | c >> 12);
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xf0 | c >> 18);
            out += static_cast<char>(0x80 | (c >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    };

    for (std::size_t at = 0; at + 2 <= bytes.size(); at += 2)
    {
        const uint32_t c = xsflib::LoadBe16(bytes.data() + at);
        if (c == 0)
        {
            break;
        }

        const bool high = c >= 0xd800 && c < 0xdc00;
        const uint32_t low = at + 4 <= bytes.size() ? xsflib::LoadBe16(bytes.data() + at + 2) : 0;
        if (high && low >= 0xdc00 && low < 0xe000)
        {
            put(0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00));
            at += 2;
        }
        else
        {
            put(c >= 0xd800 && c < 0xe000 ? 0xfffd : c);
        }
    }

    return out;
}

// The offset and size of the file at `path` in the pack whose list is `dir` (see GatherRogue). `list` is the list's
// path, for the messages. Throws xsflib::FormatError if the list doesn't name it.
std::pair<std::size_t, std::size_t> PackedFile(std::span<const uint8_t> dir, const std::string& path,
                                               const std::string& list = "Rogue2/data.dir")
{
    constexpr std::size_t kNameWidth = 64;
    const std::string text(dir.begin(), dir.end());
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
        {
            end = text.size();
        }

        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.size() <= kNameWidth)
        {
            continue;
        }

        const std::string name = line.substr(0, line.find_last_not_of(' ', kNameWidth - 1) + 1);
        if (name != path)
        {
            continue;
        }

        std::size_t numbers[2] = {};
        std::size_t field = kNameWidth;
        for (std::size_t& number : numbers)
        {
            // Rebel Strike's list pads the numbers with spaces on the left.
            while (field < line.size() && line[field] == ' ')
            {
                field++;
            }

            std::size_t digits = 0;
            while (field < line.size() && line[field] >= '0' && line[field] <= '9')
            {
                number = number * 10 + static_cast<std::size_t>(line[field] - '0');
                field++;
                digits++;
            }

            if (digits == 0 || field >= line.size() || line[field] != '\t')
            {
                throw xsflib::FormatError(list + "'s line for " + path + " is damaged");
            }

            field++;
        }

        return {numbers[0], numbers[1]};
    }

    throw xsflib::FormatError(list + " doesn't list " + path);
}

// Star Wars Rogue Squadron II: Rogue Leader's songs (see MusyXFiles::kRogue). Rogue2/data.dat packs most of the game's
// files. Rogue2/data.dir lists them in lines of text: the file's path, padded with spaces to 64 characters, then its
// offset in the pack and its size in decimal, and two more numbers, all split by tabs. audio/data holds the sound. It
// starts with the size of its index and the offset of its table of banks. The table gives the number of banks, then the
// offset of each bank's entry. An entry gives the offsets and sizes of MusyX's four files (the project, the pool, the
// sample directory and the samples), the offset and size of a block before the songs, the number of songs, the offset
// of their table and the offset of the bank's list of groups. A song's row in the table gives the song's offset and
// size, then its group and its ID in big-endian halves. The list of groups holds big-endian halves and ends with
// 0xFFFF. Every word is big-endian, and every offset counts from the start of audio/data.
//
// The game's music hall lists its songs at `musyx_songs`, in `musyx_song_count` entries of 16 bytes: the song's ID and
// group in big-endian halves, then the address of its name, in UTF-16 in the DOL at `embedded_dol`. The byte at 0xA
// gives the volume at which the hall plays the song. The game loads a bank whole and pushes each of its groups. The
// songs of most banks play samples of the bank that holds the hall's first song. A rip pushes that bank's groups, then
// the groups of the song's bank, each bank's in its order. The songs that the hall doesn't list follow, at the volume
// that the profile's `musyx_song_volumes` gives, or else at MusyX's volume.
MusyXCatalogue GatherRogue(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kBankTable = 4, kBankEntry = 13 * 4, kSongRow = 12, kHallEntry = 16;
    constexpr uint16_t kEndOfGroups = 0xffff;

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    const std::optional<ntdlib::Dol> embedded = ntdlib::Dol::Parse(DolBytes(*dol, profile.embedded_dol));
    if (!embedded)
    {
        throw xsflib::FormatError("main.dol holds no second DOL at " + xsflib::Hex(profile.embedded_dol, 8));
    }

    const std::vector<uint8_t> dir = ReadGameFile(game, "Rogue2/data.dir");
    const std::vector<uint8_t> pack = ReadGameFile(game, "Rogue2/data.dat");
    const auto [audio_at, audio_size] = PackedFile(dir, "audio/data");
    const std::span<const uint8_t> audio = Part(pack, audio_at, audio_size, "audio/data");
    const auto word = [&audio](std::size_t at)
    {
        return xsflib::LoadBe32(Part(audio, at, 4, "audio/data").data());
    };

    struct SongRow
    {
        uint32_t offset = 0, size = 0;
        uint16_t group = 0, id = 0;
        uint16_t set = 0;
    };
    MusyXCatalogue catalogue;
    std::vector<std::vector<MusyXCatalogue::Group>> bank_groups;
    std::vector<SongRow> rows;
    const uint32_t table = word(kBankTable);
    const uint32_t banks = word(table);
    for (uint32_t b = 0; b < banks; b++)
    {
        const uint32_t entry = word(table + 4 + b * 4);
        Part(audio, entry, kBankEntry, "a bank's entry");
        const auto file = [&](std::size_t n)
        {
            const std::span<const uint8_t> bytes = Part(audio, word(entry + n * 8), word(entry + n * 8 + 4), "a bank");
            return std::vector<uint8_t>(bytes.begin(), bytes.end());
        };
        catalogue.sets.push_back({.project = file(0), .pool = file(1), .sdir = file(2), .samples = file(3)});

        const uint16_t set = static_cast<uint16_t>(b);
        std::vector<MusyXCatalogue::Group> groups;
        for (uint32_t at = word(entry + 48);; at += 2)
        {
            const uint16_t group = xsflib::LoadBe16(Part(audio, at, 2, "a bank's groups").data());
            if (group == kEndOfGroups)
            {
                break;
            }

            groups.push_back({.id = group, .set = set});
        }

        bank_groups.push_back(std::move(groups));
        const uint32_t songs = word(entry + 40), song_table = word(entry + 44);
        for (uint32_t s = 0; s < songs; s++)
        {
            const std::span<const uint8_t> row = Part(audio, song_table + s * kSongRow, kSongRow, "a bank's songs");
            const uint32_t key = xsflib::LoadBe32(row.data() + 8);
            rows.push_back({.offset = xsflib::LoadBe32(row.data()),
                            .size = xsflib::LoadBe32(row.data() + 4),
                            .group = static_cast<uint16_t>(key >> 16),
                            .id = static_cast<uint16_t>(key),
                            .set = set});
        }
    }

    // The bank of the hall's first song holds the samples that the songs of the other banks play too.
    std::optional<uint16_t> base;
    std::vector<bool> named(rows.size(), false);
    const auto add = [&](std::size_t r, std::string label, uint8_t volume)
    {
        const SongRow& row = rows[r];
        if (!base)
        {
            base = row.set;
        }

        const std::span<const uint8_t> song = Part(audio, row.offset, row.size, "a song");
        MusyXCatalogue::Entry entry;
        entry.label = std::move(label);
        entry.volume = volume;
        entry.id = row.id;
        entry.song_group = row.group;
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        if (row.set != *base)
        {
            entry.groups = bank_groups[*base];
        }

        entry.groups.insert(entry.groups.end(), bank_groups[row.set].begin(), bank_groups[row.set].end());
        for (const auto& [address, value] : profile.musyx_writes)
        {
            entry.writes.push_back({address, value});
        }

        AddReverb(entry.writes, profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        catalogue.songs.emplace_back(song.begin(), song.end());
        catalogue.entries.push_back(std::move(entry));
    };

    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const std::span<const uint8_t> hall =
            Part(DolBytes(*dol, profile.musyx_songs + i * static_cast<uint32_t>(kHallEntry)), 0, kHallEntry,
                 "the music hall");
        const uint16_t id = xsflib::LoadBe16(hall.data()), group = xsflib::LoadBe16(hall.data() + 2);
        const auto row =
            std::find_if(rows.begin(), rows.end(), [&](const SongRow& r) { return r.group == group && r.id == id; });
        if (row == rows.end())
        {
            throw xsflib::FormatError("audio/data has no song " + xsflib::Hex(id, 4) + " in group " +
                                      xsflib::Hex(group, 4));
        }

        const std::size_t r = static_cast<std::size_t>(row - rows.begin());
        named[r] = true;
        add(r, Utf16Be(DolBytes(*embedded, xsflib::LoadBe32(hall.data() + 4))), hall[0xa]);
    }

    for (std::size_t r = 0; r < rows.size(); r++)
    {
        if (named[r])
        {
            continue;
        }

        const uint32_t key = uint32_t{rows[r].group} << 16 | rows[r].id;
        uint8_t volume = MusyXCatalogue::kDefaultVolume;
        for (const auto& [song, given] : profile.musyx_song_volumes)
        {
            if (song == key)
            {
                volume = given;
            }
        }

        add(r, "Song " + xsflib::Hex(rows[r].group, 2, true) + "-" + xsflib::Hex(rows[r].id, 2, true), volume);
    }

    return catalogue;
}

// The file of an FSYS archive with the name `name`. Throws xsflib::FormatError if the archive has none.
const FsysFile& FsysFileNamed(const std::vector<FsysFile>& files, const std::string& name, const char* archive)
{
    for (const FsysFile& file : files)
    {
        if (file.name == name)
        {
            return file;
        }
    }

    throw xsflib::FormatError(std::string(archive) + " has no file " + name);
}

// Pokémon Colosseum's songs (see MusyXFiles::kColosseum). The module common_rel in common.fsys holds the game's table
// of sounds at `musyx_songs`, in `musyx_song_count` entries of 12 bytes. An entry's first byte has bit 7 set for an
// effect. A song's entry gives its group at +1, its reverb's preset at +2, its file's number at +3, and MusyX's song ID
// in a big-endian half at +4. The table of files at `musyx_file_table` gives each file's FSYS ID in the first word of
// 8 bytes. bgm_archive.fsys holds the songs under those IDs and names them. An ID of 0 means no file. The game plays a
// song through sndSeqPlayEx with the song's group and ID, at volume 0x7F.
//
// The presets at `musyx_reverb_presets` give the settings of aux A's high-quality reverb, 24 bytes each: six floats in
// the order of MusyXReverb. A song's preset other than 0 sets them. With 0, the reverb keeps its settings. A rip then
// takes preset 0's settings. The game's set-up writes those.
//
// The music has two groups: group 0 in the files of snd_music, and group 1 in those of snd_music_atmos. common.fsys
// holds a group's project, pool and sample directory under the files' name with _proj, _pool and _sdir. The samples are
// in sound/, with .samp.
MusyXCatalogue GatherColosseum(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr uint8_t kMusicVolume = 0x7f;
    constexpr std::size_t kSound = 12, kFile = 8, kPreset = 24;
    constexpr uint8_t kEffect = 0x80;
    constexpr const char* kSets[] = {"snd_music", "snd_music_atmos"};

    const std::vector<FsysFile> common = ReadFsys(ReadGameFile(game, "common.fsys"));
    const std::vector<FsysFile> songs = ReadFsys(ReadGameFile(game, "bgm_archive.fsys"));
    const std::vector<uint8_t>& module = FsysFileNamed(common, "common_rel", "common.fsys").data;

    // Each set's group: a project's first group starts with the offset of the next group, then the group's ID in a
    // big-endian half.
    MusyXCatalogue catalogue;
    std::map<uint16_t, uint16_t> set_of_group;
    for (const char* stem : kSets)
    {
        const std::string name = stem;
        MusyXCatalogue::FileSet set{.project = FsysFileNamed(common, name + "_proj", "common.fsys").data,
                                    .pool = FsysFileNamed(common, name + "_pool", "common.fsys").data,
                                    .sdir = FsysFileNamed(common, name + "_sdir", "common.fsys").data,
                                    .samples = ReadGameFile(game, "sound/" + name + ".samp")};
        const uint16_t group = xsflib::LoadBe16(Part(set.project, 4, 2, "a project").data());
        set_of_group[group] = static_cast<uint16_t>(catalogue.sets.size());
        catalogue.sets.push_back(std::move(set));
    }

    const std::span<const uint8_t> sounds =
        Part(module, profile.musyx_songs, std::size_t{profile.musyx_song_count} * kSound, "common_rel's sound table");
    const std::span<const uint8_t> files =
        Part(module, profile.musyx_file_table, std::size_t{profile.musyx_file_count} * kFile, "common_rel's files");
    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const uint8_t* row = sounds.data() + i * kSound;
        if ((row[0] & kEffect) != 0)
        {
            continue;
        }

        const uint8_t group = row[1], preset = row[2], file = row[3];
        if (file >= profile.musyx_file_count)
        {
            throw xsflib::FormatError("common_rel's sound " + std::to_string(i) + " names no file");
        }

        const uint32_t id = xsflib::LoadBe32(files.data() + file * kFile);
        if (id == 0)
        {
            continue;
        }

        const auto song = std::find_if(songs.begin(), songs.end(), [id](const FsysFile& f) { return f.id == id; });
        const auto set = set_of_group.find(group);
        if (song == songs.end() || set == set_of_group.end())
        {
            throw xsflib::FormatError("bgm_archive.fsys or common.fsys lacks sound " + std::to_string(i) + "'s files");
        }

        const std::span<const uint8_t> settings =
            Part(module, profile.musyx_reverb_presets + preset * kPreset, kPreset, "a reverb's preset");
        MusyXReverb reverb = profile.reverbs[0];
        float* fields[] = {&reverb.coloration, &reverb.mix,       &reverb.time,
                           &reverb.damping,    &reverb.pre_delay, &reverb.crosstalk};
        for (std::size_t f = 0; f < std::size(fields); f++)
        {
            *fields[f] = std::bit_cast<float>(xsflib::LoadBe32(settings.data() + f * 4));
        }

        MusyXCatalogue::Entry entry;
        entry.label = song->name;
        entry.volume = kMusicVolume;
        entry.id = xsflib::LoadBe16(row + 4);
        entry.song_group = group;
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        entry.groups = {{.id = group, .set = set->second}};
        AddReverb(entry.writes, reverb);
        catalogue.songs.push_back(song->data);
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// A part of one of Resident Evil Zero's archives: its type and its bytes.
struct ArchivePart
{
    std::string type;
    std::span<const uint8_t> data;
};

// The parts of one of Resident Evil Zero's archives (.arc). The archive starts with a word 0, the number of its parts
// and the size of its header. Each part then has 0x20 bytes: its offset and size, its name in 8 bytes and its type in
// 16. Every word is big-endian. Throws xsflib::FormatError if a part runs past the archive's end.
std::vector<ArchivePart> ArchiveParts(std::span<const uint8_t> archive)
{
    constexpr std::size_t kPart = 0x20, kType = 0x10, kTypeSize = 16;

    const std::span<const uint8_t> header = Part(archive, 0, 12, "an archive's header");
    const uint32_t count = xsflib::LoadBe32(header.data() + 4);
    const uint32_t first = xsflib::LoadBe32(header.data() + 8);
    std::vector<ArchivePart> parts;
    for (uint32_t i = 0; i < count; i++)
    {
        const std::span<const uint8_t> row = Part(archive, first + std::size_t{i} * kPart, kPart, "an archive's parts");
        const std::span<const uint8_t> type = row.subspan(kType, kTypeSize);
        parts.push_back({.type = std::string(type.begin(), std::find(type.begin(), type.end(), uint8_t{0})),
                         .data = Part(archive, xsflib::LoadBe32(row.data()), xsflib::LoadBe32(row.data() + 4),
                                      "an archive's part")});
    }

    return parts;
}

// The contents of one of Resident Evil Zero's compressed files (.alz). The file's bits come from each byte's lowest bit
// up. The header gives the mode in 8 bits and the size of the contents in 32. Mode 0 stores the contents after the
// header. In modes 1 and 2, a 1 bit starts a literal byte in the next 8 bits. A 0 bit starts a copy of earlier
// contents: its distance less 1, then its length. Mode 1 gives the distance in 10 bits. Mode 2 gives it as a class and
// a value. A class is the number of 0 bits before a 1 bit. Classes 0 to 3 give the value in 2, 4, 6 and 10 bits. The
// length is a class and a value in both modes. The game copies the bytes as memmove does (0x80007360). Throws
// xsflib::FormatError if the file ends early, if a copy reaches outside the contents, or if the contents would be
// larger than the console's memory.
std::vector<uint8_t> Unalz(std::span<const uint8_t> file)
{
    constexpr uint32_t kMaxSize = 24 << 20;
    constexpr uint32_t kWidths[] = {2, 4, 6, 10};
    constexpr std::size_t kHeader = 5;

    std::size_t bit = 0;
    const auto read = [&](uint32_t count)
    {
        uint32_t value = 0;
        for (uint32_t i = 0; i < count; i++, bit++)
        {
            if (bit / 8 >= file.size())
            {
                throw xsflib::FormatError("a compressed file ends early");
            }

            value |= uint32_t{(file[bit / 8] >> (bit % 8)) & 1u} << i;
        }

        return value;
    };
    const auto width = [&]
    {
        uint32_t zeros = 0;
        while (read(1) == 0)
        {
            if (++zeros >= std::size(kWidths))
            {
                throw xsflib::FormatError("a compressed file is damaged");
            }
        }

        return kWidths[zeros];
    };

    const uint32_t mode = read(8);
    const uint32_t size = read(32);
    if (size > kMaxSize)
    {
        throw xsflib::FormatError("a compressed file is too large");
    }

    if (mode == 0)
    {
        const std::span<const uint8_t> stored = Part(file, kHeader, size, "a stored file");
        return {stored.begin(), stored.end()};
    }

    std::vector<uint8_t> contents(size);
    for (std::size_t at = 0; at < size;)
    {
        if (read(1) != 0)
        {
            contents[at++] = static_cast<uint8_t>(read(8));
            continue;
        }

        const uint32_t distance = read(mode == 1 ? 10 : width()) + 1;
        const uint32_t length = read(width());
        if (distance > at || length > size - at)
        {
            throw xsflib::FormatError("a compressed file copies from outside its contents");
        }

        std::memmove(contents.data() + at, contents.data() + at - distance, length);
        at += length;
    }

    return contents;
}

// Resident Evil Zero's songs (see MusyXFiles::kResidentEvil). The game's code lists them at `musyx_songs`, 12 bytes
// each: MusyX's song ID and the song's group in big-endian halves, then the addresses of two names. The first names
// the archive that holds the group, and the second the song's file. A song ID of 0xFFFF ends the list. The game reads
// the files from bio0/sound/: the archive (.arc), its samples (.sam) and the song (.son). The archive's parts of the
// types "proj", "pool" and "sdir" are MusyX's project, pool and sample directory. The game's disc library matches names
// in any case. The list names the songs of both discs, and songs that neither disc holds.
//
// Some of the songs that bio0/sound/ lacks are the music of a cutscene. The game loads a cutscene's compressed archive,
// bio0/demo/<name>.alz (0x80169280). Part 7 of the archive holds the music: an archive of MusyX's pool, project and
// sample directory, then the song. Part 1 of bio0/demo/<name>sam.arc holds the samples (0x8016A3C0). The ID of the
// project's first group is the group in the song's row of the list. A rip takes the files from the first cutscene that
// holds the group. A rip lists the songs whose files the disc holds, in the list's order.
//
// The game pushes a song's group and plays the song through sndSeqPlayEx. A room or a cutscene starts a song at the
// volume of one of the game's sound options. Each option starts at 127, MusyX's full volume (0x80148F8C). A cutscene
// raises its music to that volume over 133 ms (0x8016C254). A rip leaves the fade-in out. No room plays bgm_52. The
// extras play it at 100/127 of the music's option (0x8017B970).
MusyXCatalogue GatherResidentEvil(const disc::GameFiles& game, const GameProfile& profile)
{
    // The volume that the game's sound options give a song at start-up.
    constexpr uint8_t kOptionVolume = 0x7f;
    // The song that the extras play, and its volume there.
    constexpr uint16_t kExtrasSong = 0x90;
    constexpr uint8_t kExtrasVolume = 100;
    constexpr std::size_t kEntry = 12;
    constexpr uint16_t kEnd = 0xffff;
    constexpr char kSounds[] = "bio0/sound/", kCutscenes[] = "bio0/demo/", kCompressed[] = ".alz";
    // A cutscene's parts: its music in its archive, and the music's samples in its archive of samples.
    constexpr std::size_t kMusicPart = 7, kMusicSamples = 1;

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    // The paths of the disc's files, by their paths in lower case.
    std::map<std::string, std::string> paths;
    for (const disc::GameFile& file : game.files)
    {
        paths.emplace(xsflib::AsciiLower(file.path), file.path);
    }

    const auto path_of = [&paths](const std::string& path) -> const std::string*
    {
        const auto found = paths.find(xsflib::AsciiLower(path));
        return found == paths.end() ? nullptr : &found->second;
    };
    const auto read = [&](const std::string& path)
    {
        const std::string* found = path_of(path);
        if (found == nullptr)
        {
            throw xsflib::FormatError("the disc has no " + path);
        }

        return ReadGameFile(game, *found);
    };
    const auto name_at = [&dol](uint32_t address)
    {
        const std::span<const uint8_t> name = DolBytes(*dol, address);
        return std::string(name.begin(), std::find(name.begin(), name.end(), uint8_t{0}));
    };
    const auto bytes = [](std::span<const uint8_t> data)
    {
        return std::vector<uint8_t>(data.begin(), data.end());
    };

    // The cutscenes' music: the files of each group, its song, and the number of its file set once a sound uses it.
    struct Music
    {
        MusyXCatalogue::FileSet set;
        std::vector<uint8_t> song;
        std::optional<uint16_t> number;
    };
    std::map<uint16_t, Music> music;
    const std::string cutscenes = xsflib::AsciiLower(kCutscenes);
    for (const auto& [lower, path] : paths)
    {
        if (!lower.starts_with(cutscenes) || !lower.ends_with(kCompressed) ||
            lower.find('/', cutscenes.size()) != std::string::npos)
        {
            continue;
        }

        const std::string stem = lower.substr(0, lower.size() - (std::size(kCompressed) - 1));
        const std::string* samples = path_of(stem + "sam.arc");
        if (samples == nullptr)
        {
            continue;
        }

        const std::vector<uint8_t> archive = Unalz(ReadGameFile(game, path));
        const std::vector<ArchivePart> parts = ArchiveParts(archive);
        if (parts.size() <= kMusicPart || parts[kMusicPart].data.empty())
        {
            continue;
        }

        const std::vector<ArchivePart> files = ArchiveParts(parts[kMusicPart].data);
        const std::vector<uint8_t> sample_archive = ReadGameFile(game, *samples);
        const std::vector<ArchivePart> sample_parts = ArchiveParts(sample_archive);
        if (files.size() < 4 || sample_parts.size() <= kMusicSamples)
        {
            throw xsflib::FormatError(path + " lacks a part of its music");
        }

        // A project's first group starts with the offset of the next group, then the group's ID in a big-endian half.
        const uint16_t group = xsflib::LoadBe16(Part(files[1].data, 4, 2, "a project").data());
        music.try_emplace(group, Music{.set = {.project = bytes(files[1].data),
                                               .pool = bytes(files[0].data),
                                               .sdir = bytes(files[2].data),
                                               .samples = bytes(sample_parts[kMusicSamples].data)},
                                       .song = bytes(files[3].data)});
    }

    MusyXCatalogue catalogue;
    std::map<std::string, uint16_t> sets; // the file set of each archive, by the archive's name
    for (uint32_t at = profile.musyx_songs;; at += kEntry)
    {
        const std::span<const uint8_t> row = Part(DolBytes(*dol, at), 0, kEntry, "the song table");
        const uint16_t id = xsflib::LoadBe16(row.data());
        if (id == kEnd)
        {
            break;
        }

        MusyXCatalogue::Entry entry;
        entry.label = name_at(xsflib::LoadBe32(row.data() + 8));
        entry.volume = id == kExtrasSong ? kExtrasVolume : kOptionVolume;
        entry.id = id;
        entry.song_group = xsflib::LoadBe16(row.data() + 2);
        entry.song = static_cast<uint16_t>(catalogue.songs.size());
        const std::string song = kSounds + entry.label + ".son";
        const auto cutscene = music.find(entry.song_group);
        if (path_of(song) != nullptr)
        {
            const std::string archive = name_at(xsflib::LoadBe32(row.data() + 4));
            const auto [set, added] = sets.try_emplace(archive, static_cast<uint16_t>(catalogue.sets.size()));
            if (added)
            {
                const std::string stem = kSounds + archive;
                const std::vector<uint8_t> file = read(stem + ".arc");
                const std::vector<ArchivePart> parts = ArchiveParts(file);
                const auto part = [&](const char* type)
                {
                    const auto found =
                        std::find_if(parts.begin(), parts.end(), [&](const ArchivePart& p) { return p.type == type; });
                    if (found == parts.end())
                    {
                        throw xsflib::FormatError(stem + ".arc has no part of the type " + type);
                    }

                    return bytes(found->data);
                };
                catalogue.sets.push_back({.project = part("proj"),
                                          .pool = part("pool"),
                                          .sdir = part("sdir"),
                                          .samples = read(stem + ".sam")});
            }

            entry.groups = {{entry.song_group, set->second}};
            catalogue.songs.push_back(read(song));
        }
        else if (cutscene != music.end())
        {
            Music& found = cutscene->second;
            if (!found.number)
            {
                found.number = static_cast<uint16_t>(catalogue.sets.size());
                catalogue.sets.push_back(found.set);
            }

            entry.groups = {{entry.song_group, *found.number}};
            catalogue.songs.push_back(found.song);
        }
        else
        {
            continue;
        }

        AddReverb(entry.writes, profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// Star Wars Rogue Squadron III: Rebel Strike's effects (see MusyXFiles::kRebelStrike). Rogue3/data.dat packs the
// game's files as Rogue2/data.dat does in Rogue Leader. The banks in its audio/data have the same layout too (see
// GatherRogue). Many banks hold copies of the same groups. Each effect ID belongs to one group. A rip takes each effect
// from the first bank whose project lists it. It pushes the effect's group alone. The library holds the banks that the
// effects take. An effect plays at the volume and the pan that MusyX gives it. The rip names an effect by its ID.
MusyXCatalogue GatherRebelStrike(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kBankTable = 4, kBankEntry = 13 * 4;
    const std::vector<uint8_t> dir = ReadGameFile(game, "Rogue3/data.dir");
    const std::vector<uint8_t> pack = ReadGameFile(game, "Rogue3/data.dat");
    const auto [audio_at, audio_size] = PackedFile(dir, "audio/data", "Rogue3/data.dir");
    const std::span<const uint8_t> audio = Part(pack, audio_at, audio_size, "audio/data");
    const auto word = [&audio](std::size_t at)
    {
        return xsflib::LoadBe32(Part(audio, at, 4, "audio/data").data());
    };

    struct Effect
    {
        uint16_t id = 0;
        uint16_t group = 0;
        uint32_t bank = 0;
    };
    std::vector<Effect> effects;
    std::vector<MusyXCatalogue::FileSet> banks;
    const uint32_t table = word(kBankTable);
    const uint32_t bank_count = word(table);
    for (uint32_t b = 0; b < bank_count; b++)
    {
        const uint32_t entry = word(table + 4 + b * 4);
        Part(audio, entry, kBankEntry, "a bank's entry");
        const auto file = [&](std::size_t n)
        {
            const std::span<const uint8_t> bytes = Part(audio, word(entry + n * 8), word(entry + n * 8 + 4), "a bank");
            return std::vector<uint8_t>(bytes.begin(), bytes.end());
        };
        banks.push_back({.project = file(0), .pool = file(1), .sdir = file(2), .samples = file(3)});
        for (const ProjectEffect& effect : ProjectEffects(banks.back().project))
        {
            effects.push_back({.id = effect.id, .group = effect.group, .bank = b});
        }
    }

    std::stable_sort(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) { return a.id < b.id; });
    effects.erase(
        std::unique(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) { return a.id == b.id; }),
        effects.end());

    MusyXCatalogue catalogue;
    std::map<uint32_t, uint16_t> set_numbers;
    for (const Effect& effect : effects)
    {
        const auto [set, added] = set_numbers.try_emplace(effect.bank, static_cast<uint16_t>(catalogue.sets.size()));
        if (added)
        {
            catalogue.sets.push_back(std::move(banks[effect.bank]));
        }

        MusyXCatalogue::Entry entry;
        char number[8];
        std::snprintf(number, sizeof(number), "%04u", static_cast<unsigned>(effect.id));
        entry.label = std::string("RebelStrike-") + number;
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, set->second}};
        for (const auto& [address, value] : profile.musyx_writes)
        {
            entry.writes.push_back({address, value});
        }

        AddReverb(entry.writes, profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// Paper Mario: The Thousand-Year Door's sounds (see MusyXFiles::kPaperMario). sound/proj/pmario.proj, .pool, .sdir and
// .samp hold every MusyX group of the game. The game pushes groups 0 to 11 at start-up and keeps them all. It streams
// its music. A rip holds the effects of the project's effects groups, in the order of their IDs. Each pushes its group.
//
// The game's code lists its sounds at `musyx_effects`, in `musyx_effect_count` rows of 20 bytes. A row starts with the
// address of the sound's name. The word after it gives the sound's kind in its top bits and its ID in its low 16 bits.
// 0x20000000 marks an effect of MusyX, 0x40000000 a song and 0x80000000 a stream. A row of 0 is unused. The bytes at
// 0xC, 0xD and 0xF give an effect's volume, its pan and its send to the reverb. The big-endian half at 0x12 bends its
// pitch. The game starts the effect with sndFXStartEx, a volume of 0x7F and the row's pan (0x800E0E4C). It then sets
// MIDI controller 7 to the row's volume, the pitch wheel to 0x2000 plus the bend, and controller 91 to the send. The
// game adds its room's reverb to a send above 0. A rip plays no room. Most of the game's sounds play at a place in the
// world. Their volumes and pans then follow the listener. A rip plays an effect as the row gives it, through the
// catalogue's controllers.
//
// Several rows can play one effect. A rip takes the first row with a volume above 0, or the first row if none has one.
// Its name labels the sound. An effect that no row plays is named after the set and its ID, such as "pmario-0051". It
// plays at MusyX's defaults.
MusyXCatalogue GatherPaperMario(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kRow = 20;
    constexpr uint32_t kKind = 0xf0000000, kEffect = 0x20000000;
    constexpr uint8_t kStartVolume = 0x7f, kVolume = 7, kReverbSend = 91, kPitchWheel = 0x80;
    constexpr uint16_t kNoBend = 0x2000;

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    struct Row
    {
        std::string name;
        uint8_t volume = 0, pan = 0, send = 0;
        uint16_t bend = 0;
    };
    std::map<uint16_t, Row> rows;
    for (uint32_t i = 0; i < profile.musyx_effect_count; i++)
    {
        const std::span<const uint8_t> row =
            Part(DolBytes(*dol, profile.musyx_effects + i * static_cast<uint32_t>(kRow)), 0, kRow, "the sound table");
        const uint32_t sound = xsflib::LoadBe32(row.data() + 4);
        const uint16_t id = static_cast<uint16_t>(sound);
        const auto found = rows.find(id);
        if ((sound & kKind) != kEffect || (found != rows.end() && (found->second.volume != 0 || row[0xc] == 0)))
        {
            continue;
        }

        std::string name;
        if (const uint32_t at = xsflib::LoadBe32(row.data()); at != 0)
        {
            const std::span<const uint8_t> text = DolBytes(*dol, at);
            name.assign(text.begin(), std::find(text.begin(), text.end(), uint8_t{0}));
        }

        rows[id] = {.name = std::move(name),
                    .volume = row[0xc],
                    .pan = row[0xd],
                    .send = row[0xf],
                    .bend = xsflib::LoadBe16(row.data() + 0x12)};
    }

    MusyXCatalogue catalogue;
    catalogue.sets.push_back({.project = ReadGameFile(game, "sound/proj/pmario.proj"),
                              .pool = ReadGameFile(game, "sound/proj/pmario.pool"),
                              .sdir = ReadGameFile(game, "sound/proj/pmario.sdir"),
                              .samples = ReadGameFile(game, "sound/proj/pmario.samp")});
    std::vector<ProjectEffect> effects = ProjectEffects(catalogue.sets[0].project);
    std::stable_sort(effects.begin(), effects.end(),
                     [](const ProjectEffect& a, const ProjectEffect& b) { return a.id < b.id; });
    effects.erase(std::unique(effects.begin(), effects.end(),
                              [](const ProjectEffect& a, const ProjectEffect& b) { return a.id == b.id; }),
                  effects.end());
    for (const ProjectEffect& effect : effects)
    {
        MusyXCatalogue::Entry entry;
        char number[8];
        std::snprintf(number, sizeof(number), "%04u", static_cast<unsigned>(effect.id));
        entry.label = std::string("pmario-") + number;
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, 0}};
        for (const auto& [address, value] : profile.musyx_writes)
        {
            entry.writes.push_back({address, value});
        }

        AddReverb(entry.writes, profile.reverbs[0]);
        AddReverb(entry.writes, profile.reverbs[1]);
        if (const auto found = rows.find(effect.id); found != rows.end())
        {
            const Row& row = found->second;
            if (!row.name.empty())
            {
                entry.label = row.name;
            }

            entry.volume = kStartVolume;
            entry.pan = row.pan;
            entry.controls = {
                {.controller = kVolume, .value = row.volume},
                {.controller = kPitchWheel, .fine = true, .value = static_cast<uint16_t>(kNoBend + row.bend)},
                {.controller = kReverbSend, .value = row.send}};
        }

        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

} // namespace

MusyXCatalogue GatherMusyX(const disc::GameFiles& game, const GameProfile& profile)
{
    switch (profile.musyx_files)
    {
    case MusyXFiles::kRetro:
        return GatherRetro(game);
    case MusyXFiles::kTales:
        return GatherTales(game, profile);
    case MusyXFiles::kStarFox:
        return GatherStarFox(game, profile);
    case MusyXFiles::kRogue:
        return GatherRogue(game, profile);
    case MusyXFiles::kColosseum:
        return GatherColosseum(game, profile);
    case MusyXFiles::kResidentEvil:
        return GatherResidentEvil(game, profile);
    case MusyXFiles::kPaperMario:
        return GatherPaperMario(game, profile);
    case MusyXFiles::kRebelStrike:
        return GatherRebelStrike(game, profile);
    case MusyXFiles::kNone:
        break;
    }

    throw xsflib::FormatError(std::string(profile.name) + " has no catalogue of MusyX sounds");
}

std::vector<uint8_t> CatalogueFile(const MusyXCatalogue& catalogue)
{
    constexpr uint32_t kMagic = 0x47434d58; // 'GCMX'
    constexpr uint32_t kVersion = 1;
    constexpr uint32_t kHeader = 32, kSound = 20, kGroup = 4, kWrite = 8;
    std::size_t groups = 0, writes = 0;
    for (const MusyXCatalogue::Entry& entry : catalogue.entries)
    {
        groups += entry.groups.size();
        writes += entry.writes.size() + entry.controls.size();
    }

    if (groups > 0xffff || writes > 0xffff)
    {
        throw xsflib::FormatError("the catalogue of MusyX sounds has too many entries");
    }

    const uint32_t sounds_at = kHeader;
    const uint32_t groups_at = sounds_at + static_cast<uint32_t>(catalogue.entries.size()) * kSound;
    const uint32_t writes_at = groups_at + static_cast<uint32_t>(groups) * kGroup;
    std::vector<uint8_t> file(writes_at + writes * kWrite);
    uint8_t* p = file.data();
    xsflib::StoreBe32(p, kMagic);
    xsflib::StoreBe32(p + 4, kVersion);
    xsflib::StoreBe32(p + 8, static_cast<uint32_t>(catalogue.entries.size()));
    xsflib::StoreBe32(p + 12, sounds_at);
    xsflib::StoreBe32(p + 16, static_cast<uint32_t>(groups));
    xsflib::StoreBe32(p + 20, groups_at);
    xsflib::StoreBe32(p + 24, static_cast<uint32_t>(writes));
    xsflib::StoreBe32(p + 28, writes_at);

    uint32_t group = 0, write = 0;
    for (std::size_t i = 0; i < catalogue.entries.size(); i++)
    {
        const MusyXCatalogue::Entry& entry = catalogue.entries[i];
        uint8_t* sound = p + sounds_at + i * kSound;
        sound[0] = static_cast<uint8_t>(entry.kind);
        sound[1] = entry.volume;
        sound[2] = entry.pan;
        sound[3] = entry.studio;
        xsflib::StoreBe16(sound + 4, entry.id);
        xsflib::StoreBe16(sound + 6, entry.song_group);
        xsflib::StoreBe16(sound + 8, entry.song);
        xsflib::StoreBe16(sound + 10, static_cast<uint16_t>(group));
        xsflib::StoreBe16(sound + 12, static_cast<uint16_t>(entry.groups.size()));
        xsflib::StoreBe16(sound + 14, static_cast<uint16_t>(write));
        xsflib::StoreBe16(sound + 16, static_cast<uint16_t>(entry.writes.size()));
        xsflib::StoreBe16(sound + 18, static_cast<uint16_t>(entry.controls.size()));
        for (const MusyXCatalogue::Group& g : entry.groups)
        {
            xsflib::StoreBe16(p + groups_at + group * kGroup, g.id);
            xsflib::StoreBe16(p + groups_at + group * kGroup + 2, g.set);
            group++;
        }

        for (const MusyXCatalogue::Write& w : entry.writes)
        {
            xsflib::StoreBe32(p + writes_at + write * kWrite, w.address);
            xsflib::StoreBe32(p + writes_at + write * kWrite + 4, w.value);
            write++;
        }

        // The controllers follow the words. Bit 8 of the first value picks sndFXCtrl14.
        for (const MusyXCatalogue::Control& c : entry.controls)
        {
            xsflib::StoreBe32(p + writes_at + write * kWrite, c.controller | (c.fine ? 0x100u : 0u));
            xsflib::StoreBe32(p + writes_at + write * kWrite + 4, c.value);
            write++;
        }
    }

    return file;
}

std::string MusyXPath(uint32_t number, const char* extension)
{
    return "MusyX/" + std::to_string(number) + extension;
}

MusyXSections SectionsOf(std::span<const uint8_t> song)
{
    constexpr std::size_t kInfo = 0x10, kSectionTable = 0x54, kEntry = 12, kPattern = 8;
    constexpr uint32_t kSectioned = 0x80000000;
    constexpr uint16_t kEnd = 0xffff, kLoop = 0xfffe;
    const auto bytes = [&song](std::size_t offset, std::size_t size) -> const uint8_t*
    {
        return offset <= song.size() && size <= song.size() - offset ? song.data() + offset : nullptr;
    };

    // The tracks' sections, and the table of the tracks.
    MusyXSections sections;
    sections.looping = 0;
    const uint8_t* header = bytes(0, kInfo + 4);
    if (header == nullptr)
    {
        return sections;
    }

    if (xsflib::LoadBe32(header + kInfo) & kSectioned)
    {
        const uint8_t* offset = bytes(kSectionTable, 4);
        const uint8_t* table = offset != nullptr ? bytes(xsflib::LoadBe32(offset), MusyXSections::kTracks) : nullptr;
        if (table != nullptr)
        {
            std::copy_n(table, MusyXSections::kTracks, sections.of_track.begin());
        }
    }

    const uint8_t* tracks = bytes(xsflib::LoadBe32(header), MusyXSections::kTracks * 4);
    if (tracks == nullptr)
    {
        return sections;
    }

    // A track's regions end with an end entry or a loop entry.
    for (std::size_t k = 0; k < MusyXSections::kTracks; k++)
    {
        const uint32_t first = xsflib::LoadBe32(tracks + 4 * k);
        const uint8_t section = sections.of_track[k];
        if (first == 0 || section >= MusyXSections::kSections)
        {
            continue;
        }

        for (std::size_t at = first; const uint8_t* entry = bytes(at, kEntry); at += kEntry)
        {
            const uint16_t pattern = xsflib::LoadBe16(entry + kPattern);
            if (pattern == kLoop)
            {
                sections.looping |= static_cast<uint16_t>(1u << section);
            }
            if (pattern == kLoop || pattern == kEnd)
            {
                break;
            }
        }
    }

    return sections;
}

} // namespace gcsf::rip
