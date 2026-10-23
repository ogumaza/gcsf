// SPDX-License-Identifier: MIT

#include "rip/musyx_catalogue.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ntdlib/disc/compression.h"
#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/text.h"
#include "xsflib/utf8.h"

#include "gcsf/format.h"
#include "rip/fsys.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{
namespace
{

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
        names[{type, id}] = TextBeforeZero(name);
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
    const std::size_t start = std::min(at, agsc.size());
    std::string text = TerminatedText(agsc.subspan(start), "an audio group set has no name");
    at = start + text.size() + 1;

    return text;
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

// The ID of the first group in `project`, a MusyX project file. A group's header starts with the offset of the next
// group, then the group's ID in a big-endian half (see ProjectEffects). Throws xsflib::FormatError if the file is too
// short.
uint16_t FirstGroupId(std::span<const uint8_t> project)
{
    return xsflib::LoadBe16(Part(project, 4, 2, "a project").data());
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
        names.push_back(TerminatedText(table.subspan(at), "a string table's string doesn't end"));
        at += names.back().size() + 1;
    }

    return names;
}

// The names of a project's effects, by their IDs, from a header of C defines that MusyX's tools write. A line
// "#define SFX<name> <ID>" names an effect, with the ID in decimal. The define's name, "SFX" and all, is the effect's
// name. The lines of groups and songs start with other prefixes. Of two names for one ID, the first wins.
std::map<uint16_t, std::string> HeaderEffectNames(std::span<const uint8_t> header)
{
    std::map<uint16_t, std::string> names;
    const std::string text(header.begin(), header.end());
    std::size_t line = 0;
    while (line < text.size())
    {
        const std::size_t end = std::min(text.find('\n', line), text.size());
        unsigned id = 0;
        char name[64] = {};
        const std::string row = text.substr(line, end - line);
        if (std::sscanf(row.c_str(), "#define %63s %u", name, &id) == 2 && std::string_view(name).starts_with("SFX") &&
            id <= 0xffff)
        {
            names.try_emplace(static_cast<uint16_t>(id), name);
        }

        line = end + 1;
    }

    return names;
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

// The writes that a sound of the catalogue plays with: the profile's musyx_writes, then the settings of aux A's
// reverb `aux_a` and of aux B's (GameProfile::reverbs).
std::vector<MusyXCatalogue::Write> SoundWrites(const GameProfile& profile, const MusyXReverb& aux_a)
{
    std::vector<MusyXCatalogue::Write> writes;
    for (const auto& [address, value] : profile.musyx_writes)
    {
        writes.push_back({address, value});
    }

    AddReverb(writes, aux_a);
    AddReverb(writes, profile.reverbs[1]);

    return writes;
}

// Sorts `effects` by ID and keeps the first effect of each ID. The sort keeps the order of effects that share an ID.
template <typename Effect>
void KeepFirstOfEachId(std::vector<Effect>& effects)
{
    const auto by_id = [](const Effect& a, const Effect& b)
    {
        return a.id < b.id;
    };
    const auto same_id = [](const Effect& a, const Effect& b)
    {
        return a.id == b.id;
    };
    std::stable_sort(effects.begin(), effects.end(), by_id);
    effects.erase(std::unique(effects.begin(), effects.end(), same_id), effects.end());
}

// The label of an effect that nothing names: the name of its set, a hyphen and its ID in four digits, such as
// "pmario-0051".
std::string EffectLabel(const std::string& set, uint16_t id)
{
    char number[8];
    std::snprintf(number, sizeof(number), "%04u", static_cast<unsigned>(id));

    return set + "-" + number;
}

// Adds a song's entry and the song's file to the catalogue. The entry's `song` then names the file.
void AddSong(MusyXCatalogue& catalogue, MusyXCatalogue::Entry entry, std::vector<uint8_t> song)
{
    entry.song = static_cast<uint16_t>(catalogue.songs.size());
    catalogue.songs.push_back(std::move(song));
    catalogue.entries.push_back(std::move(entry));
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
MusyXCatalogue GatherRetro(const disc::GameFiles& game, const GameProfile& profile)
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

    KeepFirstOfEachId(effects);

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
        entry.groups = {{static_cast<uint16_t>(group), set_number(set)}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), std::vector<uint8_t>(data.begin(), data.end()));
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
            entry.label = EffectLabel(sets.at(effect.set).name, effect.id);
        }

        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, set_number(effect.set)}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// The disc's files, found by their paths in any case. The disc libraries of Tales of Symphonia and Resident Evil Zero
// match paths in any case. Tales of Symphonia's code names its files in lower case. The disc's folder is "S".
class AnyCaseFiles
{
public:
    explicit AnyCaseFiles(const disc::GameFiles& game) : game_(game)
    {
        for (const disc::GameFile& file : game.files)
        {
            paths_.emplace(xsflib::AsciiLower(file.path), file.path);
        }
    }

    // The disc's paths, by their paths in lower case. Of two paths that differ only in case, the first on the disc
    // stands for both.
    const std::map<std::string, std::string>& Paths() const
    {
        return paths_;
    }

    // The disc's path of the file whose path matches `path` in any case, or null if the disc has none.
    const std::string* Find(const std::string& path) const
    {
        const auto found = paths_.find(xsflib::AsciiLower(path));
        return found == paths_.end() ? nullptr : &found->second;
    }

    // The file whose path matches `path` in any case. Throws xsflib::FormatError if the disc has none, or if it can't
    // be read.
    std::vector<uint8_t> Read(const std::string& path) const
    {
        const std::string* found = Find(path);
        if (found == nullptr)
        {
            throw xsflib::FormatError("the disc has no " + path);
        }

        return ReadGameFile(game_, *found);
    }

private:
    const disc::GameFiles& game_;
    std::map<std::string, std::string> paths_;
};

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

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    const AnyCaseFiles files(game);

    MusyXCatalogue catalogue;
    for (const char* bank : profile.bank_files)
    {
        catalogue.sets.push_back(SoundFile(files.Read(bank)));
    }

    if (catalogue.sets.empty())
    {
        throw xsflib::FormatError(std::string(profile.name) + " has no MusyX group for its songs");
    }

    const uint16_t group = FirstGroupId(catalogue.sets[0].project);
    for (uint32_t at = profile.musyx_songs;; at += kEntry)
    {
        const std::span<const uint8_t> row = Part(DolBytes(dol, at), 0, kEntry, "the song table");
        const uint16_t id = xsflib::LoadBe16(row.data() + 4);
        if (id == kEnd)
        {
            break;
        }

        const std::string path = TextBeforeZero(DolBytes(dol, xsflib::LoadBe32(row.data())));
        const std::size_t slash = path.find_last_of('/');
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);

        MusyXCatalogue::Entry entry;
        entry.label = file.substr(0, file.find_last_of('.'));
        entry.volume = kMusicVolume;
        entry.id = id;
        entry.song_group = group;
        entry.groups = {{group, 0}};
        const bool other = profile.song_reverbs != 0 && DolBytes(dol, profile.song_reverbs + id)[0] == kOtherReverb;
        entry.writes = SoundWrites(profile, other ? profile.other_reverb : profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), files.Read(path));
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

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);

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
            Part(DolBytes(dol, profile.musyx_songs + i * static_cast<uint32_t>(kSongEntry)), 0, kSongEntry,
                 "the song table");
        table.push_back({.id = xsflib::LoadBe16(row.data()),
                         .group = row[2],
                         .name = TextBeforeZero(DolBytes(dol, xsflib::LoadBe32(row.data() + 4)))});
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
        entry.groups = groups;
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), std::vector<uint8_t>(songs[song.id].begin(), songs[song.id].end()));
    }

    return catalogue;
}

// Converts a UTF-16 string in big-endian halves to UTF-8. The string ends at a half of 0. A lone surrogate becomes
// U+FFFD.
std::string Utf16Be(std::span<const uint8_t> bytes)
{
    std::string out;
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
            xsflib::AppendUtf8(out, 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00));
            at += 2;
        }
        else
        {
            xsflib::AppendUtf8(out, c);
        }
    }

    return out;
}

// The offset and size of the file at `path` in the pack whose list is `dir` (see GatherRogue). `list` is the list's
// path, for the messages. Throws xsflib::FormatError if the list doesn't name it.
std::pair<std::size_t, std::size_t> PackedFile(std::span<const uint8_t> dir, const std::string& path,
                                               const std::string& list)
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

// Where audio/data gives the offset of its table of banks, and the size of a bank's entry (see ReadRogueBanks).
constexpr std::size_t kRogueBankTable = 4, kRogueBankEntry = 13 * 4;

// Rogue Leader's or Rebel Strike's audio/data: its bytes in `folder`/data.dat, where `folder`/data.dir lists it (see
// GatherRogue). Throws xsflib::FormatError if the disc lacks either file, or the list doesn't give audio/data.
std::vector<uint8_t> RogueAudio(const disc::GameFiles& game, const std::string& folder)
{
    const std::string list = folder + "/data.dir";
    const std::vector<uint8_t> dir = ReadGameFile(game, list);
    const std::vector<uint8_t> pack = ReadGameFile(game, folder + "/data.dat");
    const auto [at, size] = PackedFile(dir, "audio/data", list);
    const std::span<const uint8_t> audio = Part(pack, at, size, "audio/data");
    return std::vector<uint8_t>(audio.begin(), audio.end());
}

// The big-endian word at `at` in audio/data. Throws xsflib::FormatError if it runs past the end of audio/data.
uint32_t AudioWord(std::span<const uint8_t> audio, std::size_t at)
{
    return xsflib::LoadBe32(Part(audio, at, 4, "audio/data").data());
}

// MusyX's four files of the bank whose entry is at `entry` in audio/data. Throws xsflib::FormatError if the entry or a
// file runs past the end of audio/data.
MusyXCatalogue::FileSet RogueBankFiles(std::span<const uint8_t> audio, uint32_t entry)
{
    Part(audio, entry, kRogueBankEntry, "a bank's entry");
    const auto file = [&](std::size_t n)
    {
        const std::span<const uint8_t> bytes =
            Part(audio, AudioWord(audio, entry + n * 8), AudioWord(audio, entry + n * 8 + 4), "a bank");
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };

    return {.project = file(0), .pool = file(1), .sdir = file(2), .samples = file(3)};
}

// A song's row in a bank's table of songs (see ReadRogueBanks), with the number of its bank.
struct RogueSong
{
    uint32_t offset = 0, size = 0;
    uint16_t group = 0, id = 0;
    uint16_t set = 0;
};

// The banks of audio/data: each bank's MusyX files and groups, and the songs of every bank. Bank n is the catalogue's
// set n.
struct RogueBanks
{
    std::vector<MusyXCatalogue::FileSet> files;
    std::vector<std::vector<MusyXCatalogue::Group>> groups;
    std::vector<RogueSong> songs;
};

// The banks of audio/data, in the order of its table of banks, and their songs in the order of each bank's table.
// audio/data starts with the size of its index and the offset of its table of banks. The table gives the number of
// banks, then the offset of each bank's entry. An entry gives the offsets and sizes of MusyX's four files (the project,
// the pool, the sample directory and the samples), the offset and size of a block before the songs, the number of
// songs, the offset of their table and the offset of the bank's list of groups. A song's row in the table gives the
// song's offset and size, then its group and its ID in big-endian halves. The list of groups holds big-endian halves
// and ends with 0xFFFF. Every word is big-endian. Every offset counts from the start of audio/data. Throws
// xsflib::FormatError if the table or a part of a bank runs past the end of audio/data.
RogueBanks ReadRogueBanks(std::span<const uint8_t> audio)
{
    constexpr std::size_t kSongRow = 12;
    constexpr uint16_t kEndOfGroups = 0xffff;

    RogueBanks banks;
    const uint32_t table = AudioWord(audio, kRogueBankTable);
    const uint32_t count = AudioWord(audio, table);
    for (uint32_t b = 0; b < count; b++)
    {
        const uint32_t entry = AudioWord(audio, table + 4 + b * 4);
        banks.files.push_back(RogueBankFiles(audio, entry));

        const uint16_t set = static_cast<uint16_t>(b);
        std::vector<MusyXCatalogue::Group> groups;
        for (uint32_t at = AudioWord(audio, entry + 48);; at += 2)
        {
            const uint16_t group = xsflib::LoadBe16(Part(audio, at, 2, "a bank's groups").data());
            if (group == kEndOfGroups)
            {
                break;
            }

            groups.push_back({.id = group, .set = set});
        }

        banks.groups.push_back(std::move(groups));
        const uint32_t songs = AudioWord(audio, entry + 40), song_table = AudioWord(audio, entry + 44);
        for (uint32_t s = 0; s < songs; s++)
        {
            const std::span<const uint8_t> row = Part(audio, song_table + s * kSongRow, kSongRow, "a bank's songs");
            const uint32_t key = xsflib::LoadBe32(row.data() + 8);
            banks.songs.push_back({.offset = xsflib::LoadBe32(row.data()),
                                   .size = xsflib::LoadBe32(row.data() + 4),
                                   .group = static_cast<uint16_t>(key >> 16),
                                   .id = static_cast<uint16_t>(key),
                                   .set = set});
        }
    }

    return banks;
}

// Star Wars Rogue Squadron II: Rogue Leader's songs (see MusyXFiles::kRogue). Rogue2/data.dat packs most of the game's
// files. Rogue2/data.dir lists them in lines of text: the file's path, padded with spaces to 64 characters, then its
// offset in the pack and its size in decimal, and two more numbers, all split by tabs. audio/data holds the sound, in
// banks (ReadRogueBanks).
//
// The game's music hall lists its songs at `musyx_songs`, in `musyx_song_count` entries of 16 bytes: the song's ID and
// group in big-endian halves, then the address of its name, in UTF-16 in the DOL at `embedded_dol`. The byte at 0xA
// gives the volume at which the hall plays the song. The game loads a bank whole and pushes each of its groups. The
// songs of most banks play samples of the bank that holds the hall's first song. A rip pushes that bank's groups, then
// the groups of the song's bank, each bank's in its order. The songs that the hall doesn't list follow, at the volume
// that the profile's `musyx_song_volumes` gives, or else at MusyX's volume.
MusyXCatalogue GatherRogue(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr std::size_t kHallEntry = 16;

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);

    const std::optional<ntdlib::Dol> embedded = ntdlib::Dol::Parse(DolBytes(dol, profile.embedded_dol));
    if (!embedded)
    {
        throw xsflib::FormatError("main.dol holds no second DOL at " + xsflib::Hex(profile.embedded_dol, 8));
    }

    const std::vector<uint8_t> audio = RogueAudio(game, "Rogue2");
    RogueBanks banks = ReadRogueBanks(audio);
    MusyXCatalogue catalogue;
    catalogue.sets = std::move(banks.files);

    // The bank of the hall's first song holds the samples that the songs of the other banks play too.
    std::optional<uint16_t> base;
    std::vector<bool> named(banks.songs.size(), false);
    const auto add = [&](std::size_t r, std::string label, uint8_t volume)
    {
        const RogueSong& row = banks.songs[r];
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
        if (row.set != *base)
        {
            entry.groups = banks.groups[*base];
        }

        entry.groups.insert(entry.groups.end(), banks.groups[row.set].begin(), banks.groups[row.set].end());
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), std::vector<uint8_t>(song.begin(), song.end()));
    };

    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const std::span<const uint8_t> hall =
            Part(DolBytes(dol, profile.musyx_songs + i * static_cast<uint32_t>(kHallEntry)), 0, kHallEntry,
                 "the music hall");
        const uint16_t id = xsflib::LoadBe16(hall.data()), group = xsflib::LoadBe16(hall.data() + 2);
        const auto row = std::find_if(banks.songs.begin(), banks.songs.end(),
                                      [&](const RogueSong& r) { return r.group == group && r.id == id; });
        if (row == banks.songs.end())
        {
            throw xsflib::FormatError("audio/data has no song " + xsflib::Hex(id, 4) + " in group " +
                                      xsflib::Hex(group, 4));
        }

        const std::size_t r = static_cast<std::size_t>(row - banks.songs.begin());
        named[r] = true;
        add(r, Utf16Be(DolBytes(*embedded, xsflib::LoadBe32(hall.data() + 4))), hall[0xa]);
    }

    for (std::size_t r = 0; r < banks.songs.size(); r++)
    {
        if (named[r])
        {
            continue;
        }

        const RogueSong& row = banks.songs[r];
        const uint32_t key = uint32_t{row.group} << 16 | row.id;
        uint8_t volume = MusyXCatalogue::kDefaultVolume;
        for (const auto& [song, given] : profile.musyx_song_volumes)
        {
            if (song == key)
            {
                volume = given;
            }
        }

        add(r, "Song " + xsflib::Hex(row.group, 2, true) + "-" + xsflib::Hex(row.id, 2, true), volume);
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

// Pokémon Colosseum's sounds (see MusyXFiles::kColosseum). The module common_rel in common.fsys holds the game's table
// of sounds at `musyx_songs`, in `musyx_song_count` entries of 12 bytes. An entry's first byte has bit 7 set for an
// effect. An entry gives its group at +1, and MusyX's song ID or effect ID in a big-endian half at +4. A song's entry
// gives its reverb's preset at +2 and its file's number at +3. The table of files at `musyx_file_table` gives each
// file's FSYS ID in the first word of 8 bytes. bgm_archive.fsys holds the songs under those IDs and names them. An ID
// of 0 means no file. The game plays a song through sndSeqPlayEx with the song's group and ID, at volume 0x7F.
//
// The presets at `musyx_reverb_presets` give the settings of aux A's high-quality reverb, 24 bytes each: six floats in
// the order of MusyXReverb. A song's preset other than 0 sets them. With 0, the reverb keeps its settings. A rip then
// takes preset 0's settings. The game's set-up writes those.
//
// The game's play function (0x80166AB8) starts an effect with sndFXStartEx at volume 0x7F, pan 0x40 and studio 0. It
// then sets the effect's MIDI volume to 0x7F with sndFXCtrl. MusyX starts an effect's controllers at that value. The
// catalogue sets no controller. A rip plays the effect with preset 0's reverb and the profile's `musyx_effect_writes`.
// The effects follow the songs in the table's order. An effect is left out if its group's project limits its voices to
// 0 (ProjectEffects). MusyX never starts such an effect. Nothing names the effects. An effect is named after its set
// and its ID, such as "snd_se_battle-0201".
//
// The music has two groups: group 0 in the files of snd_music, and group 1 in those of snd_music_atmos. The effects
// have five, one in each of the sets snd_se, snd_se_battle, snd_se_motion, snd_se_nakigoe and snd_se_nakigoe2.
// common.fsys holds a group's project, pool and sample directory under the files' name with _proj, _pool and _sdir. The
// samples are in sound/, with .samp.
MusyXCatalogue GatherColosseum(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr uint8_t kMusicVolume = 0x7f, kEffectVolume = 0x7f, kEffectPan = 0x40;
    constexpr std::size_t kSound = 12, kFile = 8, kPreset = 24;
    constexpr uint8_t kEffect = 0x80;
    constexpr const char* kSets[] = {"snd_music",     "snd_music_atmos", "snd_se",         "snd_se_battle",
                                     "snd_se_motion", "snd_se_nakigoe",  "snd_se_nakigoe2"};

    const std::vector<FsysFile> common = ReadFsys(ReadGameFile(game, "common.fsys"));
    const std::vector<FsysFile> songs = ReadFsys(ReadGameFile(game, "bgm_archive.fsys"));
    const std::vector<uint8_t>& module = FsysFileNamed(common, "common_rel", "common.fsys").data;

    // Each set's group: its project's first group. The effects that MusyX starts, by their groups and IDs.
    MusyXCatalogue catalogue;
    std::map<uint16_t, uint16_t> set_of_group;
    std::set<std::pair<uint16_t, uint16_t>> playable;
    for (const char* stem : kSets)
    {
        const std::string name = stem;
        MusyXCatalogue::FileSet set{.project = FsysFileNamed(common, name + "_proj", "common.fsys").data,
                                    .pool = FsysFileNamed(common, name + "_pool", "common.fsys").data,
                                    .sdir = FsysFileNamed(common, name + "_sdir", "common.fsys").data,
                                    .samples = ReadGameFile(game, "sound/" + name + ".samp")};
        const uint16_t group = FirstGroupId(set.project);
        for (const ProjectEffect& effect : ProjectEffects(set.project))
        {
            playable.insert({effect.group, effect.id});
        }

        set_of_group[group] = static_cast<uint16_t>(catalogue.sets.size());
        catalogue.sets.push_back(std::move(set));
    }

    // Aux A's reverb with the settings of `preset`.
    const auto preset_reverb = [&](uint8_t preset)
    {
        const std::span<const uint8_t> settings =
            Part(module, profile.musyx_reverb_presets + preset * kPreset, kPreset, "a reverb's preset");
        MusyXReverb reverb = profile.reverbs[0];
        float* fields[] = {&reverb.coloration, &reverb.mix,       &reverb.time,
                           &reverb.damping,    &reverb.pre_delay, &reverb.crosstalk};
        for (std::size_t f = 0; f < std::size(fields); f++)
        {
            *fields[f] = std::bit_cast<float>(xsflib::LoadBe32(settings.data() + f * 4));
        }

        return reverb;
    };

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

        MusyXCatalogue::Entry entry;
        entry.label = song->name;
        entry.volume = kMusicVolume;
        entry.id = xsflib::LoadBe16(row + 4);
        entry.song_group = group;
        entry.groups = {{.id = group, .set = set->second}};
        entry.writes = SoundWrites(profile, preset_reverb(preset));
        AddSong(catalogue, std::move(entry), song->data);
    }

    const MusyXReverb effect_reverb = preset_reverb(0);
    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const uint8_t* row = sounds.data() + i * kSound;
        if ((row[0] & kEffect) == 0)
        {
            continue;
        }

        const uint8_t group = row[1];
        const uint16_t id = xsflib::LoadBe16(row + 4);
        const auto set = set_of_group.find(group);
        if (set == set_of_group.end())
        {
            throw xsflib::FormatError("common.fsys lacks the group of sound " + std::to_string(i));
        }

        if (!playable.contains({group, id}))
        {
            continue;
        }

        MusyXCatalogue::Entry entry;
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.volume = kEffectVolume;
        entry.pan = kEffectPan;
        entry.id = id;
        entry.label = EffectLabel(kSets[set->second], entry.id);
        entry.groups = {{.id = group, .set = set->second}};
        entry.writes = SoundWrites(profile, effect_reverb);
        for (const auto& [address, value] : profile.musyx_effect_writes)
        {
            entry.writes.push_back({address, value});
        }

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
        parts.push_back({.type = TextBeforeZero(row.subspan(kType, kTypeSize)),
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

// A song of one of Resident Evil Zero's cutscenes: the files of its group, and the song.
struct CutsceneSong
{
    MusyXCatalogue::FileSet set;
    std::vector<uint8_t> song;
};

// The songs of Resident Evil Zero's cutscenes, by the ID of each song's group (see GatherResidentEvil). The game loads
// a cutscene's compressed archive, bio0/demo/<name>.alz (0x80169280). Part 7 of the archive holds the music: an archive
// of MusyX's pool, project and sample directory, then the song. Part 1 of bio0/demo/<name>sam.arc holds the samples
// (0x8016A3C0). The ID of the project's first group is the group in the song's row of the game's list. A rip takes the
// files from the first cutscene that holds the group, in the order of the paths in lower case. Throws
// xsflib::FormatError if a cutscene's archives can't be read, are damaged or lack a part of the music.
std::map<uint16_t, CutsceneSong> CutsceneSongs(const AnyCaseFiles& files)
{
    constexpr char kCutscenes[] = "bio0/demo/", kCompressed[] = ".alz";
    // A cutscene's parts: its music in its archive, and the music's samples in its archive of samples.
    constexpr std::size_t kMusicPart = 7, kMusicSamples = 1;

    const auto bytes = [](std::span<const uint8_t> data)
    {
        return std::vector<uint8_t>(data.begin(), data.end());
    };

    std::map<uint16_t, CutsceneSong> songs;
    const std::string cutscenes = xsflib::AsciiLower(kCutscenes);
    for (const auto& [lower, path] : files.Paths())
    {
        if (!lower.starts_with(cutscenes) || !lower.ends_with(kCompressed) ||
            lower.find('/', cutscenes.size()) != std::string::npos)
        {
            continue;
        }

        const std::string stem = lower.substr(0, lower.size() - (std::size(kCompressed) - 1));
        const std::string* samples = files.Find(stem + "sam.arc");
        if (samples == nullptr)
        {
            continue;
        }

        const std::vector<uint8_t> archive = Unalz(files.Read(path));
        const std::vector<ArchivePart> parts = ArchiveParts(archive);
        if (parts.size() <= kMusicPart || parts[kMusicPart].data.empty())
        {
            continue;
        }

        const std::vector<ArchivePart> music = ArchiveParts(parts[kMusicPart].data);
        const std::vector<uint8_t> sample_archive = files.Read(*samples);
        const std::vector<ArchivePart> sample_parts = ArchiveParts(sample_archive);
        if (music.size() < 4 || sample_parts.size() <= kMusicSamples)
        {
            throw xsflib::FormatError(path + " lacks a part of its music");
        }

        const uint16_t group = FirstGroupId(music[1].data);
        songs.try_emplace(group, CutsceneSong{.set = {.project = bytes(music[1].data),
                                                      .pool = bytes(music[0].data),
                                                      .sdir = bytes(music[2].data),
                                                      .samples = bytes(sample_parts[kMusicSamples].data)},
                                              .song = bytes(music[3].data)});
    }

    return songs;
}

// Resident Evil Zero's songs (see MusyXFiles::kResidentEvil). The game's code lists them at `musyx_songs`, 12 bytes
// each: MusyX's song ID and the song's group in big-endian halves, then the addresses of two names. The first names
// the archive that holds the group, and the second the song's file. A song ID of 0xFFFF ends the list. The game reads
// the files from bio0/sound/: the archive (.arc), its samples (.sam) and the song (.son). The archive's parts of the
// types "proj", "pool" and "sdir" are MusyX's project, pool and sample directory. The game's disc library matches names
// in any case. The list names the songs of both discs, and songs that neither disc holds.
//
// Some of the songs that bio0/sound/ lacks are the music of a cutscene (CutsceneSongs). A rip lists the songs whose
// files the disc holds, in the list's order.
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
    constexpr char kSounds[] = "bio0/sound/";

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    const AnyCaseFiles files(game);
    const std::map<uint16_t, CutsceneSong> cutscene_songs = CutsceneSongs(files);

    MusyXCatalogue catalogue;
    std::map<std::string, uint16_t> sets;       // the file set of each archive, by the archive's name
    std::map<uint16_t, uint16_t> cutscene_sets; // the file set of each cutscene's song, by the song's group
    for (uint32_t at = profile.musyx_songs;; at += kEntry)
    {
        const std::span<const uint8_t> row = Part(DolBytes(dol, at), 0, kEntry, "the song table");
        const uint16_t id = xsflib::LoadBe16(row.data());
        if (id == kEnd)
        {
            break;
        }

        MusyXCatalogue::Entry entry;
        entry.label = TextBeforeZero(DolBytes(dol, xsflib::LoadBe32(row.data() + 8)));
        entry.volume = id == kExtrasSong ? kExtrasVolume : kOptionVolume;
        entry.id = id;
        entry.song_group = xsflib::LoadBe16(row.data() + 2);
        const std::string song_path = kSounds + entry.label + ".son";
        const auto cutscene = cutscene_songs.find(entry.song_group);
        std::vector<uint8_t> song;
        if (files.Find(song_path) != nullptr)
        {
            const std::string archive = TextBeforeZero(DolBytes(dol, xsflib::LoadBe32(row.data() + 4)));
            const auto [set, added] = sets.try_emplace(archive, static_cast<uint16_t>(catalogue.sets.size()));
            if (added)
            {
                const std::string stem = kSounds + archive;
                const std::vector<uint8_t> file = files.Read(stem + ".arc");
                const std::vector<ArchivePart> parts = ArchiveParts(file);
                const auto part = [&](const char* type)
                {
                    const auto found =
                        std::find_if(parts.begin(), parts.end(), [&](const ArchivePart& p) { return p.type == type; });
                    if (found == parts.end())
                    {
                        throw xsflib::FormatError(stem + ".arc has no part of the type " + type);
                    }

                    return std::vector<uint8_t>(found->data.begin(), found->data.end());
                };
                catalogue.sets.push_back({.project = part("proj"),
                                          .pool = part("pool"),
                                          .sdir = part("sdir"),
                                          .samples = files.Read(stem + ".sam")});
            }

            entry.groups = {{entry.song_group, set->second}};
            song = files.Read(song_path);
        }
        else if (cutscene != cutscene_songs.end())
        {
            const auto [set, added] =
                cutscene_sets.try_emplace(entry.song_group, static_cast<uint16_t>(catalogue.sets.size()));
            if (added)
            {
                catalogue.sets.push_back(cutscene->second.set);
            }

            entry.groups = {{entry.song_group, set->second}};
            song = cutscene->second.song;
        }
        else
        {
            continue;
        }

        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), std::move(song));
    }

    return catalogue;
}

// Star Wars Rogue Squadron III: Rebel Strike's effects (see MusyXFiles::kRebelStrike). Rogue3/data.dat packs the
// game's files as Rogue2/data.dat does in Rogue Leader. The banks in its audio/data have the same layout too (see
// ReadRogueBanks). Many banks hold copies of the same groups. Each effect ID belongs to one group. A rip takes each
// effect from the first bank whose project lists it. It pushes the effect's group alone. The library holds the banks
// that the effects take. An effect plays at the volume and the pan that MusyX gives it. The rip names an effect by its
// ID.
MusyXCatalogue GatherRebelStrike(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<uint8_t> audio = RogueAudio(game, "Rogue3");

    struct Effect
    {
        uint16_t id = 0;
        uint16_t group = 0;
        uint32_t bank = 0;
    };
    std::vector<Effect> effects;
    std::vector<MusyXCatalogue::FileSet> banks;
    const uint32_t table = AudioWord(audio, kRogueBankTable);
    const uint32_t bank_count = AudioWord(audio, table);
    for (uint32_t b = 0; b < bank_count; b++)
    {
        const uint32_t entry = AudioWord(audio, table + 4 + b * 4);
        banks.push_back(RogueBankFiles(audio, entry));
        for (const ProjectEffect& effect : ProjectEffects(banks.back().project))
        {
            effects.push_back({.id = effect.id, .group = effect.group, .bank = b});
        }
    }

    KeepFirstOfEachId(effects);

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
        entry.label = EffectLabel("RebelStrike", effect.id);
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, set->second}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        catalogue.entries.push_back(std::move(entry));
    }

    return catalogue;
}

// The names that Pokémon Channel's sound viewer gives the effects, by the effects' IDs (see GatherPokemonChannel). The
// viewer's script holds its names from `viewer_names` on. Each name starts and ends with a tab, and a byte 0 follows
// it. The first string that doesn't start with a tab ends the names. From `viewer_effects` on, the script's code gives
// the names' effects in a run of entries. An entry holds 0x31 0x00 and the name's number in a big-endian word, 0x02 and
// a word, then 0x19 and the value. The value is 0x00 and the effect's ID in a big-endian word, or one byte other than
// 0. The rip can't tell which effect such a byte gives. The bytes 0x1F 0x35 follow every entry but the last. An effect
// that several names give takes the first name. Throws xsflib::FormatError if a name doesn't end.
std::map<uint16_t, std::string> ViewerNames(std::span<const uint8_t> script, const GameProfile& profile)
{
    constexpr uint8_t kTab = '\t';
    std::vector<std::string> names;
    for (std::size_t at = profile.viewer_names; at < script.size() && script[at] == kTab;)
    {
        const std::string text = TerminatedText(script.subspan(at), "a name of the sound viewer doesn't end");
        if (text.size() < 2 || text.back() != kTab)
        {
            break;
        }

        names.push_back(text.substr(1, text.size() - 2));
        at += text.size() + 1;
    }

    // An entry's parts: its number at +2, the 0x02 at +6, the 0x19 at +11 and its value from +12.
    constexpr std::size_t kNumber = 2, kJump = 6, kValue = 11, kShortEntry = 13, kLongEntry = 17;
    std::map<uint16_t, std::string> by_effect;
    std::size_t at = profile.viewer_effects;
    while (at + kShortEntry <= script.size() && script[at] == 0x31 && script[at + 1] == 0 &&
           script[at + kJump] == 0x02 && script[at + kValue] == 0x19)
    {
        const uint32_t number = xsflib::LoadBe32(&script[at + kNumber]);
        std::size_t next = at + kShortEntry;
        if (script[at + kValue + 1] == 0)
        {
            if (at + kLongEntry > script.size())
            {
                break;
            }

            const uint32_t id = xsflib::LoadBe32(&script[at + kValue + 2]);
            if (number < names.size() && id <= 0xffff)
            {
                by_effect.try_emplace(static_cast<uint16_t>(id), names[number]);
            }

            next = at + kLongEntry;
        }

        if (next + 2 > script.size() || script[next] != 0x1f || script[next + 1] != 0x35)
        {
            break;
        }

        at = next + 2;
    }

    return by_effect;
}

// Pokémon Channel's sounds (see MusyXFiles::kPokemonChannel). en/pch/pchsound.proj, .pool, .sdir and .samp hold
// every MusyX group of the game. The game's code lists its music at `musyx_songs`, in `musyx_song_count` rows of 8
// bytes: the effect's ID, then the number of a sound whose group the game pushes first, a big-endian word each. Each
// piece of music is an effect. The effect's layer starts two voices that play a stereo recording, panned hard left and
// right. The game's music function (0x80015398) pushes the group that the project gives the effect. It then starts the
// effect. A rip lists the music first, in the order of the table. The project's other effects follow, in the order of
// their IDs. Each sound pushes its group alone.
//
// The game starts every sound through 0x80015570: sndFXStartEx with a volume of 0x7F and the default pan, then MIDI
// controller 7 with the sound's volume, and the pan through controllers 0x83 (the surround pan) and 10. The music plays
// at a volume of 127 and a pan of 0x40 until a scene changes them. A scene's effect takes the volume and the pan of an
// object of the scene. A rip plays every sound at the music's volume and pan.
//
// The game's sound viewer, pch/scripts/sndviewer.pkc, names most of the sounds (ViewerNames). A sound that it doesn't
// name is named after the set and its ID, such as "pchsound-1064".
MusyXCatalogue GatherPokemonChannel(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr uint32_t kRow = 8;
    constexpr uint8_t kStartVolume = 0x7f, kVolume = 7, kSurroundPan = 0x83, kPan = 10;
    constexpr uint16_t kFullVolume = 127, kCentre = 0x40;

    MusyXCatalogue catalogue;
    catalogue.sets.push_back({.project = ReadGameFile(game, "en/pch/pchsound.proj"),
                              .pool = ReadGameFile(game, "en/pch/pchsound.pool"),
                              .sdir = ReadGameFile(game, "en/pch/pchsound.sdir"),
                              .samples = ReadGameFile(game, "en/pch/pchsound.samp")});
    std::vector<ProjectEffect> effects = ProjectEffects(catalogue.sets[0].project);
    KeepFirstOfEachId(effects);
    const std::map<uint16_t, std::string> names = ViewerNames(ReadGameFile(game, "pch/scripts/sndviewer.pkc"), profile);

    const auto add = [&](const ProjectEffect& effect, bool music)
    {
        MusyXCatalogue::Entry entry;
        const auto name = names.find(effect.id);
        entry.label = name != names.end() ? name->second : EffectLabel("pchsound", effect.id);
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.volume = kStartVolume;
        entry.id = effect.id;
        entry.groups = {{effect.group, 0}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        entry.controls = {{.controller = kVolume, .value = kFullVolume},
                          {.controller = kSurroundPan, .value = kCentre},
                          {.controller = kPan, .value = kCentre}};
        entry.music = music;
        catalogue.entries.push_back(std::move(entry));
    };

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    std::vector<uint16_t> music;
    for (uint32_t i = 0; i < profile.musyx_song_count; i++)
    {
        const std::span<const uint8_t> row =
            Part(DolBytes(dol, profile.musyx_songs + i * kRow), 0, kRow, "the table of music");
        const uint32_t id = xsflib::LoadBe32(row.data());
        const auto found =
            std::find_if(effects.begin(), effects.end(), [id](const ProjectEffect& effect) { return effect.id == id; });
        if (found == effects.end())
        {
            throw xsflib::FormatError("the table of music names an effect that en/pch/pchsound.proj doesn't hold");
        }

        add(*found, true);
        music.push_back(found->id);
    }

    for (const ProjectEffect& effect : effects)
    {
        if (std::find(music.begin(), music.end(), effect.id) == music.end())
        {
            add(effect, false);
        }
    }

    return catalogue;
}

// A row of Paper Mario: The Thousand-Year Door's table of sounds (see GatherPaperMario): the sound's name, its kind and
// ID, its volume, its pan, its send to the reverb and the bend of its pitch.
struct PaperMarioRow
{
    std::string name;
    uint32_t sound = 0;
    uint8_t volume = 0, pan = 0, send = 0;
    uint16_t bend = 0;
};

// The bits of a row's second word that give the sound's kind, and the kinds of Paper Mario's effects and songs.
constexpr uint32_t kPaperMarioKind = 0xf0000000, kPaperMarioEffect = 0x20000000, kPaperMarioSong = 0x40000000;

// The rows of Paper Mario's table of sounds that play sounds of `kind`: an effect's row by the effect's ID, a song's
// row by the song's record (see AddPaperMarioSongs). Several rows can play one sound. The sound takes the first row
// with a volume above 0, or the first row if none has one.
std::map<uint32_t, PaperMarioRow> PaperMarioRows(const ntdlib::Dol& dol, const GameProfile& profile, uint32_t kind)
{
    constexpr std::size_t kRow = 20;
    constexpr uint32_t kId = 0xffff, kRecord = 0xff;
    std::map<uint32_t, PaperMarioRow> rows;
    for (uint32_t i = 0; i < profile.musyx_effect_count; i++)
    {
        const std::span<const uint8_t> row =
            Part(DolBytes(dol, profile.musyx_effects + i * static_cast<uint32_t>(kRow)), 0, kRow, "the sound table");
        const uint32_t sound = xsflib::LoadBe32(row.data() + 4);
        const uint32_t key = sound & (kind == kPaperMarioSong ? kRecord : kId);
        const auto found = rows.find(key);
        if ((sound & kPaperMarioKind) != kind || (found != rows.end() && (found->second.volume != 0 || row[0xc] == 0)))
        {
            continue;
        }

        std::string name;
        if (const uint32_t at = xsflib::LoadBe32(row.data()); at != 0)
        {
            name = TextBeforeZero(DolBytes(dol, at));
        }

        rows[key] = {.name = std::move(name),
                     .sound = sound,
                     .volume = row[0xc],
                     .pan = row[0xd],
                     .send = row[0xf],
                     .bend = xsflib::LoadBe16(row.data() + 0x12)};
    }

    return rows;
}

// Adds Paper Mario's songs to `catalogue`, in the order of sound/proj/pmario.slib. The game reads the file at start-up.
// The file holds the songs, one record after another. A record starts with its size in a big-endian word, its 32 bytes
// of header included. The song follows the header. A size of 0xFFFFFFFF ends the file.
//
// A song's row gives the song's group in bits 16 to 23 of its second word, MusyX's song ID in bits 8 to 15, and the
// song's record in bits 0 to 7. The game plays the song with sndSeqPlayEx, the group, the ID and the record's song
// (0x800E124C). It sets the song's volume to 0x7F and mutes tracks 32 to 63 through sndSeqMute. No song has a track
// there. The game then sets the song's volume to the row's volume. The row's pan and send play no part. A rip names the
// song as the row does.
//
// pmario.stbl names the records: 32 bytes for each, the song's name, ended by a byte 0, then the song's group at 0x1E
// and its ID at 0x1F. A song that no row plays takes its name, its group and its ID from there. It plays at MusyX's
// default volume. Each song pushes its group from the catalogue's first file set.
void AddPaperMarioSongs(MusyXCatalogue& catalogue, const disc::GameFiles& game, const GameProfile& profile,
                        const std::map<uint32_t, PaperMarioRow>& rows)
{
    constexpr uint32_t kEndOfSongs = 0xffffffff;
    constexpr std::size_t kHeader = 32, kName = 32, kNameGroup = 0x1e, kNameId = 0x1f;
    const std::vector<uint8_t> songs = ReadGameFile(game, "sound/proj/pmario.slib");
    const std::vector<uint8_t> names = ReadGameFile(game, "sound/proj/pmario.stbl");

    std::size_t at = 0;
    for (uint32_t record = 0;; record++)
    {
        const uint32_t size = xsflib::LoadBe32(Part(songs, at, 4, "the song library").data());
        if (size == kEndOfSongs)
        {
            break;
        }

        if (size < kHeader)
        {
            throw xsflib::FormatError("a record of the song library is shorter than its header");
        }

        const std::span<const uint8_t> song = Part(songs, at + kHeader, size - kHeader, "the song library");
        at += size;

        MusyXCatalogue::Entry entry;
        if (const auto found = rows.find(record); found != rows.end())
        {
            const PaperMarioRow& row = found->second;
            entry.label = row.name;
            entry.id = static_cast<uint16_t>(row.sound >> 8 & 0xff);
            entry.song_group = static_cast<uint16_t>(row.sound >> 16 & 0xff);
            entry.volume = row.volume;
        }
        else
        {
            const std::span<const uint8_t> name = Part(names, record * kName, kName, "the song table");
            entry.id = name[kNameId];
            entry.song_group = name[kNameGroup];
        }

        if (entry.label.empty())
        {
            entry.label = TextBeforeZero(Part(names, record * kName, kNameGroup, "the song table"));
        }

        entry.groups = {{entry.song_group, 0}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), {song.begin(), song.end()});
    }
}

// Paper Mario: The Thousand-Year Door's sounds (see MusyXFiles::kPaperMario). sound/proj/pmario.proj, .pool, .sdir and
// .samp hold every MusyX group of the game. The game pushes groups 0 to 11 at start-up and keeps them all. It streams
// its music. A rip holds the effects of the project's effects groups, in the order of their IDs, then the songs (see
// AddPaperMarioSongs). Each sound pushes its group.
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
// A rip names an effect as its row does (see PaperMarioRows). An effect that no row plays is named after the set and
// its ID, such as "pmario-0051". It plays at MusyX's defaults.
MusyXCatalogue GatherPaperMario(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr uint8_t kStartVolume = 0x7f, kVolume = 7, kReverbSend = 91, kPitchWheel = 0x80;
    constexpr uint16_t kNoBend = 0x2000;
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    const std::map<uint32_t, PaperMarioRow> rows = PaperMarioRows(dol, profile, kPaperMarioEffect);

    MusyXCatalogue catalogue;
    catalogue.sets.push_back({.project = ReadGameFile(game, "sound/proj/pmario.proj"),
                              .pool = ReadGameFile(game, "sound/proj/pmario.pool"),
                              .sdir = ReadGameFile(game, "sound/proj/pmario.sdir"),
                              .samples = ReadGameFile(game, "sound/proj/pmario.samp")});
    std::vector<ProjectEffect> effects = ProjectEffects(catalogue.sets[0].project);
    KeepFirstOfEachId(effects);
    for (const ProjectEffect& effect : effects)
    {
        MusyXCatalogue::Entry entry;
        entry.label = EffectLabel("pmario", effect.id);
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{effect.group, 0}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        if (const auto found = rows.find(effect.id); found != rows.end())
        {
            const PaperMarioRow& row = found->second;
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

    AddPaperMarioSongs(catalogue, game, profile, PaperMarioRows(dol, profile, kPaperMarioSong));

    return catalogue;
}

// A module of a game's code (a REL) in the module's file, and the places that the module's relocations against the
// module give. The header's big-endian words give the module's ID at 0, the number of sections at 0xC, the offset of
// the table of sections at 0x10, and the offset and the size of the table of imports at 0x28 and 0x2C. A section's
// entry gives the section's offset in the file, with bit 0 set for code, then the section's size. An offset of 0 gives
// a section without data in the file. An import gives a module's ID and the offset of the relocations against that
// module. A relocation takes 8 bytes: the distance from the place of the relocation before it in a big-endian half,
// the type, the section that the relocation points into, and the offset there in a big-endian word. Type 202 starts
// the relocations in the section that it gives. Type 203 ends the list. Type 201 does nothing. Type 1 writes the
// address into a word.
class RelModule
{
public:
    // Throws xsflib::FormatError if the module is damaged.
    explicit RelModule(std::span<const uint8_t> file) : file_(file)
    {
        constexpr std::size_t kHeader = 0x30, kSectionEntry = 8, kImport = 8;
        const std::span<const uint8_t> header = Part(file, 0, kHeader, "a module's header");
        const uint32_t id = xsflib::LoadBe32(header.data());
        const uint32_t count = xsflib::LoadBe32(header.data() + 0xc);
        const std::span<const uint8_t> entries = Part(file, xsflib::LoadBe32(header.data() + 0x10),
                                                      std::size_t{count} * kSectionEntry, "a module's sections");
        for (uint32_t s = 0; s < count; s++)
        {
            const uint32_t offset = xsflib::LoadBe32(entries.data() + s * kSectionEntry) & ~1u;
            const uint32_t size = xsflib::LoadBe32(entries.data() + s * kSectionEntry + 4);
            if (offset != 0)
            {
                Part(file, offset, size, "a module's section");
            }

            sections_.push_back({.offset = offset, .size = offset == 0 ? 0 : size});
        }

        const std::span<const uint8_t> imports = Part(file, xsflib::LoadBe32(header.data() + 0x28),
                                                      xsflib::LoadBe32(header.data() + 0x2c), "a module's imports");
        for (std::size_t i = 0; i + kImport <= imports.size(); i += kImport)
        {
            if (xsflib::LoadBe32(imports.data() + i) == id)
            {
                AddRelocations(xsflib::LoadBe32(imports.data() + i + 4));
            }
        }
    }

    // The offset in the file of `offset` in section `section`. Throws xsflib::FormatError if the file holds no such
    // place.
    std::size_t At(uint32_t section, uint32_t offset) const
    {
        if (section >= sections_.size() || sections_[section].offset == 0 || offset >= sections_[section].size)
        {
            throw xsflib::FormatError("a module's section " + std::to_string(section) + " has no data at " +
                                      xsflib::Hex(offset));
        }

        return sections_[section].offset + offset;
    }

    // The offset in the file of the place that the word at `at` points at, or nothing if no relocation sets the word to
    // a place in the file.
    std::optional<std::size_t> Pointer(std::size_t at) const
    {
        const auto found = pointers_.find(at);
        if (found == pointers_.end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    // The object at `at` in the file: its bytes up to the next place in its section that a relocation points at, or up
    // to the section's end. Throws xsflib::FormatError if no section's data holds `at`.
    std::span<const uint8_t> Object(std::size_t at) const
    {
        for (const Section& section : sections_)
        {
            if (section.offset == 0 || at < section.offset || at - section.offset >= section.size)
            {
                continue;
            }

            const std::size_t offset = at - section.offset;
            const auto next = section.targets.upper_bound(static_cast<uint32_t>(offset));
            const std::size_t end =
                next == section.targets.end() ? section.size : std::min<std::size_t>(*next, section.size);
            return file_.subspan(at, end - offset);
        }

        throw xsflib::FormatError("an object lies outside a module's sections");
    }

private:
    // A section: its offset in the file, or 0 for none, its size, and the offsets in it at which relocations point.
    struct Section
    {
        std::size_t offset = 0, size = 0;
        std::set<uint32_t> targets;
    };

    // Reads the list of relocations at `at` in the file. Their targets lie in the module.
    void AddRelocations(std::size_t at)
    {
        constexpr std::size_t kRelocation = 8;
        constexpr uint8_t kAddress32 = 1, kNothing = 201, kSection = 202, kEnd = 203;
        std::size_t section = sections_.size(), offset = 0;
        for (;; at += kRelocation)
        {
            const std::span<const uint8_t> relocation = Part(file_, at, kRelocation, "a module's relocations");
            const uint8_t type = relocation[2], target = relocation[3];
            if (type == kEnd)
            {
                break;
            }

            if (type == kSection)
            {
                section = target;
                offset = 0;
                continue;
            }

            offset += xsflib::LoadBe16(relocation.data());
            if (type == kNothing)
            {
                continue;
            }

            if (target >= sections_.size())
            {
                throw xsflib::FormatError("a module's relocation points outside its sections");
            }

            const uint32_t addend = xsflib::LoadBe32(relocation.data() + 4);
            sections_[target].targets.insert(addend);
            const bool in_file = section < sections_.size() && sections_[section].offset != 0 &&
                                 offset < sections_[section].size && sections_[target].offset != 0 &&
                                 addend < sections_[target].size;
            if (type == kAddress32 && in_file)
            {
                pointers_[sections_[section].offset + offset] = sections_[target].offset + addend;
            }
        }
    }

    std::span<const uint8_t> file_;
    std::vector<Section> sections_;
    std::map<std::size_t, std::size_t> pointers_; // the place that each word points at, by the word's place
};

// The pointers of a table at `at` in `module`, each with its number in the table. The table runs up to the next object
// (RelModule::Object). A word that no relocation sets gives no pointer.
std::vector<std::pair<uint16_t, std::size_t>> TablePointers(const RelModule& module, std::size_t at)
{
    std::vector<std::pair<uint16_t, std::size_t>> pointers;
    const std::size_t words = module.Object(at).size() / 4;
    for (std::size_t i = 0; i < words; i++)
    {
        if (const std::optional<std::size_t> pointer = module.Pointer(at + i * 4))
        {
            pointers.emplace_back(static_cast<uint16_t>(i), *pointer);
        }
    }

    return pointers;
}

// The bytes of an object of `module` (RelModule::Object) at `offset` in section `section`.
std::vector<uint8_t> ModuleObject(const RelModule& module, uint32_t section, uint32_t offset)
{
    const std::span<const uint8_t> object = module.Object(module.At(section, offset));
    return {object.begin(), object.end()};
}

// Where a module of Nintendo Puzzle Collection keeps a MusyX group's files: the group's ID, then the offsets of its
// project, its pool and its sample directory in one section of the module.
struct PuzzleGroup
{
    uint16_t id = 0;
    uint32_t project = 0, pool = 0, sdir = 0;
};

// The file set of `group` in section `section` of `module`, with the samples `samples`.
MusyXCatalogue::FileSet PuzzleSet(const RelModule& module, uint32_t section, const PuzzleGroup& group,
                                  std::vector<uint8_t> samples)
{
    return {.project = ModuleObject(module, section, group.project),
            .pool = ModuleObject(module, section, group.pool),
            .sdir = ModuleObject(module, section, group.sdir),
            .samples = std::move(samples)};
}

// The label of a song of a module of Nintendo Puzzle Collection: the module's name, a hyphen, then the song's name, or
// its ID in two digits if `names` has no name for it.
std::string PuzzleLabel(const char* module, std::span<const char* const> names, uint16_t id)
{
    if (id < names.size())
    {
        return std::string(module) + "-" + names[id];
    }

    return std::string(module) + "-" + ZeroPadded(id, 2);
}

// The settings of MusyX's chorus (SND_AUX_CHORUS): its base delay, its variation and its period, in ms. The chorus's
// block holds them from 0x90.
using PuzzleChorus = std::array<uint32_t, 3>;

// The words that set up studio 0's aux effects as a module of Nintendo Puzzle Collection does (see
// GatherPuzzleCollection). The profile's `params` give aux A MusyX's high-quality reverb and aux B MusyX's chorus. The
// words write the settings of `reverb`. They write the settings of `chorus` into aux B's block. A standard reverb on
// aux A takes the callback and the preparation in the profile's `standard_reverb`. Without a chorus, aux B has no
// effect. The words for a standard reverb and for no chorus change the driver's parameters (kMusyXAuxCallbacks).
std::vector<MusyXCatalogue::Write> PuzzleAuxWrites(const GameProfile& profile, const MusyXReverb& reverb,
                                                   const std::optional<PuzzleChorus>& chorus)
{
    constexpr uint32_t kChorusSettings = 0x90;
    const auto parameter = [](uint32_t index)
    {
        return DriverBlock::kAddress + DriverBlock::kProfileOffset + index * 4;
    };

    std::vector<MusyXCatalogue::Write> writes;
    AddReverb(writes, reverb);
    if (!reverb.high)
    {
        writes.push_back({parameter(kMusyXAuxCallbacks), profile.standard_reverb[0]});
        writes.push_back({parameter(kMusyXAuxPreparations), profile.standard_reverb[1]});
    }

    if (chorus)
    {
        const uint32_t block = profile.params.at(kMusyXAuxBlocks + 1);
        for (uint32_t i = 0; i < chorus->size(); i++)
        {
            writes.push_back({block + kChorusSettings + i * 4, (*chorus)[i]});
        }
    }
    else
    {
        writes.push_back({parameter(kMusyXAuxCallbacks + 1), 0});
        writes.push_back({parameter(kMusyXAuxPreparations + 1), 0});
    }

    return writes;
}

// The size of the menu's table of volumes: two rows of four words, at 0 in the module's section 4.
constexpr std::size_t kPuzzleMenuVolumeRows = 2, kPuzzleMenuVolumeColumns = 4, kPuzzleMenuVolumeWord = 4;

// The volume of the menu's song `id` of group `group`, from the menu's table of volumes `table`. The low byte of a word
// of the table gives a volume. The menu's code at 0x3A44 in section 1 picks a word by the song's group and ID. The code
// at 0x3B68 in section 1 starts the song, then passes the volume to sndVolume with a time of 1 ms and group 0xFD, the
// songs' groups. A rip gives the song its volume through the catalogue's entry instead (sndSeqVolume).
uint8_t PuzzleMenuVolume(std::span<const uint8_t> table, uint16_t group, uint16_t id)
{
    std::size_t row = 0, column = kPuzzleMenuVolumeColumns - 1;
    if (group == 1)
    {
        column = 2;
    }
    else if (group == 2 && id == 1)
    {
        row = 1;
        column = 1;
    }
    else if (group == 2 && (id == 2 || id == 3))
    {
        column = id == 2 ? 0 : 1;
    }
    else if (group == 3)
    {
        row = 1;
        column = id == 4 ? 0 : id == 5 ? 2 : kPuzzleMenuVolumeColumns - 1;
    }

    const std::size_t word = (row * kPuzzleMenuVolumeColumns + column) * kPuzzleMenuVolumeWord;
    return table[word + kPuzzleMenuVolumeWord - 1];
}

// The menu's songs (see GatherPuzzleCollection). menu_relsamp.szp unpacks to four parts. Its first four words give
// their offsets: the menu's module, then the samples of groups 2, 0 and 4. A part runs up to the next part, and the
// last one up to the end of the file. The module's section 4 holds a table of six pointers at 0x448. Pointer g points
// at the table of group g's songs, for g from 1 to 5. A group's table holds a pointer to each of its songs, under the
// song's ID. The menu's code gives the offsets of each group's files in section 4: at 0x56EC in section 1 for groups 0,
// 2 and 4, 0x58B8 for group 1, 0x5988 for group 3 and 0x5A58 for group 5. Files in title/ hold the samples of groups 1,
// 3 and 5. The menu pushes group 0, its effects, at start-up, and a song's group before the song. Each song takes a
// volume from the table at 0 in section 4 (PuzzleMenuVolume). The menu's code (0x5580 in section 1) gives aux A's
// reverb the settings of the profile's reverbs[0]. The code gives the chorus a base delay of 13 ms, a variation of 2 ms
// and a period of 500 ms. A rip labels a song with the song's ID, such as "menu-01".
void AddPuzzleMenu(MusyXCatalogue& catalogue, const AnyCaseFiles& files, const GameProfile& profile)
{
    constexpr uint32_t kData = 4, kVolumes = 0, kSongTables = 0x448;
    constexpr std::size_t kParts = 4;
    constexpr PuzzleChorus kChorus = {13, 2, 500};

    // Each group's files, and the part of menu_relsamp.szp or the file that holds its samples.
    struct Group
    {
        PuzzleGroup files;
        std::size_t part = 0;
        const char* samples = nullptr;
    };
    constexpr Group kGroups[] = {
        {.files = {.id = 0, .project = 0x111884, .pool = 0x1116c0, .sdir = 0x111938}, .part = 2},
        {.files = {.id = 1, .project = 0x135184, .pool = 0x133fd8, .sdir = 0x1352b8},
         .samples = "title/yoshi_title.samp"},
        {.files = {.id = 2, .project = 0x113d84, .pool = 0x111bc4, .sdir = 0x113fec}, .part = 1},
        {.files = {.id = 3, .project = 0x133a50, .pool = 0x1339ac, .sdir = 0x133b60},
         .samples = "title/agb_title.samp"},
        {.files = {.id = 4, .project = 0x11e284, .pool = 0x11d0d4, .sdir = 0x11e42c}, .part = 3},
        {.files = {.id = 5, .project = 0x12842c, .pool = 0x125e4c, .sdir = 0x128600},
         .samples = "title/grand_staff.samp"},
    };

    const std::vector<uint8_t> file = UnpackYay0(files.Read("menu_relsamp.szp"));
    const std::span<const uint8_t> header = Part(file, 0, kParts * 4, "menu_relsamp.szp's header");
    std::array<std::span<const uint8_t>, kParts> parts;
    for (std::size_t i = 0; i < kParts; i++)
    {
        const std::size_t start = xsflib::LoadBe32(header.data() + i * 4);
        const std::size_t end = i + 1 < kParts ? xsflib::LoadBe32(header.data() + i * 4 + 4) : file.size();
        parts[i] = Part(file, start, end < start ? 0 : end - start, "a part of menu_relsamp.szp");
    }

    const RelModule module(parts[0]);
    std::map<uint16_t, uint16_t> set_of_group;
    for (const Group& group : kGroups)
    {
        std::vector<uint8_t> samples = group.samples != nullptr
                                           ? files.Read(group.samples)
                                           : std::vector<uint8_t>(parts[group.part].begin(), parts[group.part].end());
        set_of_group[group.files.id] = static_cast<uint16_t>(catalogue.sets.size());
        catalogue.sets.push_back(PuzzleSet(module, kData, group.files, std::move(samples)));
    }

    // The songs, in the order of their IDs.
    struct Song
    {
        uint16_t group = 0, id = 0;
        std::size_t at = 0;
    };
    std::vector<Song> songs;
    for (const auto& [group, table] : TablePointers(module, module.At(kData, kSongTables)))
    {
        for (const auto& [id, at] : TablePointers(module, table))
        {
            songs.push_back({.group = group, .id = id, .at = at});
        }
    }

    std::stable_sort(songs.begin(), songs.end(), [](const Song& a, const Song& b) { return a.id < b.id; });
    const std::vector<MusyXCatalogue::Write> writes = PuzzleAuxWrites(profile, profile.reverbs[0], kChorus);
    const std::span<const uint8_t> volumes =
        Part(parts[0], module.At(kData, kVolumes),
             kPuzzleMenuVolumeRows * kPuzzleMenuVolumeColumns * kPuzzleMenuVolumeWord, "the menu's volumes");
    for (const Song& song : songs)
    {
        const auto set = set_of_group.find(song.group);
        if (set == set_of_group.end())
        {
            throw xsflib::FormatError("the menu has no files for the group of its song " + std::to_string(song.id));
        }

        const std::span<const uint8_t> data = module.Object(song.at);
        MusyXCatalogue::Entry entry;
        entry.label = PuzzleLabel("menu", {}, song.id);
        entry.volume = PuzzleMenuVolume(volumes, song.group, song.id);
        entry.id = song.id;
        entry.song_group = song.group;
        entry.groups = {{.id = 0, .set = set_of_group.at(0)}, {.id = song.group, .set = set->second}};
        entry.writes = writes;
        AddSong(catalogue, std::move(entry), {data.begin(), data.end()});
    }
}

// One of the games of Nintendo Puzzle Collection whose module holds its songs and its one set of MusyX's files: Dr.
// Mario and Panel de Pon (see AddPuzzleGame).
struct PuzzleGame
{
    const char* name = nullptr;           // the start of the songs' labels
    const char* file = nullptr;           // the module's file
    uint32_t song_section = 0, songs = 0; // the table of songs: the section that holds it, and its offset there
    uint32_t file_section = 0;            // the section that holds MusyX's files
    PuzzleGroup files;                    // their offsets in it
    const char* samples = nullptr;        // the file of the samples
    std::span<const char* const> names;   // the songs' names, by their IDs
    PuzzleChorus chorus = {};
    uint32_t volumes = 0; // the table of the songs' volumes in `song_section`, or 0 for none
};

// The names of Dr. Mario's songs, by their IDs. A name that ends in "f" names the fast form of the song before it.
constexpr const char* kDrMarioSongs[] = {"fever",  "feverf",  "chil",    "chilf",   "game_c",       "game_cf",
                                         "game_d", "game_df", "game_e",  "game_ef", "game_opening", "tile",
                                         "menu",   "staff",   "end_a",   "end_b",   "end_bs",       "end_c",
                                         "end_cs", "story_a", "story_b", "story_c", "ending",       "coffee"};

// Dr. Mario (see AddPuzzleGame). Dr_MARIO.szp unpacks to its module. The module's section 5 holds the table of songs
// at 0x10. Section 4 holds MusyX's files: the project at 0x10E424, the pool at 0x1035B0 and the sample directory at
// 0x10F2F4. dr_test.samp holds the samples. During play, the songs play at MusyX's default volume. The game lowers a
// song's volume during a pause (dm_set_pause_and_volume, 0x13DF4 in section 1), in its manual and in some other states.
// A rip plays a song as during play. The game's code (0x248 in section 1) gives aux A's reverb the settings of the
// profile's reverbs[0]. The code gives the chorus a base delay of 13 ms, a variation of 2 ms and a period of 500 ms.
constexpr PuzzleGame kDrMario = {.name = "dr_mario",
                                 .file = "Dr_MARIO.szp",
                                 .song_section = 5,
                                 .songs = 0x10,
                                 .file_section = 4,
                                 .files = {.project = 0x10e424, .pool = 0x1035b0, .sdir = 0x10f2f4},
                                 .samples = "dr_test.samp",
                                 .names = kDrMarioSongs,
                                 .chorus = {13, 2, 500}};

// The names of Panel de Pon's songs, by their IDs.
constexpr const char* kPanelDePonSongs[] = {"flower_01",
                                            "flower_02",
                                            "jewel_01",
                                            "jewel_02",
                                            "water_01",
                                            "water_02",
                                            "sea_01",
                                            "sea_02",
                                            "cloud_01",
                                            "cloud_02",
                                            "moon_01",
                                            "moon_02",
                                            "green_01",
                                            "green_02",
                                            "fire_01",
                                            "fire_02",
                                            "ice_01",
                                            "ice_02",
                                            "prince_01",
                                            "prince_02",
                                            "lion_01",
                                            "lion_02",
                                            "dragon_01",
                                            "dragon_02",
                                            "joker_01",
                                            "joker_02",
                                            "sanatos_01",
                                            "sanatos_02",
                                            "goddess_01",
                                            "goddess_02",
                                            "whale_01",
                                            "whale_02",
                                            "opening",
                                            "select",
                                            "ending",
                                            "ending_easymode",
                                            "you_lose",
                                            "you_win",
                                            "intro_beginning",
                                            "keyroad_part2",
                                            "puzzle_edit",
                                            "keyload_part1",
                                            "goddess_appear",
                                            "joker_apper",
                                            "which_appear",
                                            "ending_frog",
                                            "obaba_01",
                                            "obaba_02",
                                            "tutorial",
                                            "intro_storm",
                                            "ending_whale_eyes",
                                            "ending_which_photo",
                                            "keyroad_part3",
                                            "ending_staff_credits",
                                            "ending_whale_gets_back_eyes",
                                            "opening_skip",
                                            "reslut",
                                            "reserved1",
                                            "reserved2"};

// Panel de Pon (see AddPuzzleGame). panepon.szp unpacks to its module. The module's section 5 holds the table of songs
// at 0xBD0, and MusyX's files: the project at 0x20350, the pool at 0x8FC0 and the sample directory at 0x23010.
// panepon.samp holds the samples. The game plays a song through PlayMIDI (0xAD230 in section 1). The table at 0x134148
// in section 5 gives each song 4 bytes. The second byte gives the song's volume. A volume above 127 gives the game's
// default, 110 (0x8BA40 in section 1). The game's code (0x8664 in section 1) gives aux A's reverb the settings of the
// profile's reverbs[0]. The code gives the chorus a base delay of 15 ms, no variation and a period of 500 ms.
constexpr PuzzleGame kPanelDePon = {.name = "panepon",
                                    .file = "panepon.szp",
                                    .song_section = 5,
                                    .songs = 0xbd0,
                                    .file_section = 5,
                                    .files = {.project = 0x20350, .pool = 0x8fc0, .sdir = 0x23010},
                                    .samples = "panepon.samp",
                                    .names = kPanelDePonSongs,
                                    .chorus = {15, 0, 500},
                                    .volumes = 0x134148};

// Adds the songs of `game` to the catalogue. The game pushes groups 1 and 0 of the set. The songs play in group 1. A
// song's ID is the song's number in the table of songs.
void AddPuzzleGame(MusyXCatalogue& catalogue, const AnyCaseFiles& files, const GameProfile& profile,
                   const PuzzleGame& game)
{
    constexpr uint16_t kMusic = 1, kEffects = 0;
    constexpr uint8_t kMaxVolume = 0x7f, kDefaultVolume = 110;
    constexpr std::size_t kVolumeEntry = 4;

    const std::vector<uint8_t> file = UnpackYay0(files.Read(game.file));
    const RelModule module(file);
    const auto set = static_cast<uint16_t>(catalogue.sets.size());
    catalogue.sets.push_back(PuzzleSet(module, game.file_section, game.files, files.Read(game.samples)));
    const std::vector<MusyXCatalogue::Write> writes = PuzzleAuxWrites(profile, profile.reverbs[0], game.chorus);
    for (const auto& [id, at] : TablePointers(module, module.At(game.song_section, game.songs)))
    {
        const std::span<const uint8_t> data = module.Object(at);
        MusyXCatalogue::Entry entry;
        entry.label = PuzzleLabel(game.name, game.names, id);
        if (game.volumes != 0)
        {
            const std::size_t row = module.At(game.song_section, game.volumes) + id * kVolumeEntry;
            const uint8_t volume = Part(file, row, kVolumeEntry, "a table of songs' volumes")[1];
            entry.volume = volume > kMaxVolume ? kDefaultVolume : volume;
        }

        entry.id = id;
        entry.song_group = kMusic;
        entry.groups = {{.id = kMusic, .set = set}, {.id = kEffects, .set = set}};
        entry.writes = writes;
        AddSong(catalogue, std::move(entry), {data.begin(), data.end()});
    }
}

// Yoshi's Cookie's songs (see GatherPuzzleCollection). ycookie.szp unpacks to the game's module. ycookie.bin holds the
// game's sound files as items. The table at 0xD16D0 in the module's section 5 gives each item's offset in units of 32
// bytes. An item runs up to the next item's offset. Items 0 to 8 hold the project, the pool and the sample directory of
// groups 0, 1 and 2, in that order. Items 9 to 34 hold songs 0 to 25, and items 35 to 37 the samples of groups 0 to 2
// (0x4574 in section 1). The table of songs at 0x17928C in section 5 gives each song 44 bytes: its group and its ID in
// big-endian words, then the path of its file in 32 bytes. A rip labels a song with the path's file name. Group 1 holds
// the game's effects. The game pushes groups 0 and 1 at start-up, and group 2 before a song of that group. The songs
// play at the volume for music in the game's options. The table at 0x179B74 in section 5 gives the volume of each of
// the options' levels. A rip plays the songs at the default level, 5. The game's code (0x41E18 in section 1) gives aux
// A MusyX's standard reverb, and aux B no effect. At start-up, the game also limits MusyX to 40 voices for music and 24
// for effects (sndSetMaxVoices, 0x414E4 in section 1). A rip writes the limits into MusyX's state, at the profile's
// `musyx_voice_limits`.
void AddYoshisCookie(MusyXCatalogue& catalogue, const AnyCaseFiles& files, const GameProfile& profile)
{
    constexpr uint32_t kData = 5, kItems = 0xd16d0, kSongs = 0x17928c, kLevels = 0x179b74;
    constexpr std::size_t kItemUnit = 32, kSongEntry = 44, kPathOffset = 8, kPathSize = 32, kDefaultLevel = 5;
    constexpr uint32_t kGroups = 3, kFirstSong = 9, kSongCount = 26, kFirstSamples = 35;
    constexpr uint16_t kStartGroups[] = {0, 1};
    constexpr uint32_t kMusicVoices = 40, kEffectVoices = 24;

    const std::vector<uint8_t> file = UnpackYay0(files.Read("ycookie.szp"));
    const RelModule module(file);
    const std::vector<uint8_t> items = files.Read("ycookie.bin");
    const std::size_t offsets = module.At(kData, kItems);
    const auto item = [&](uint32_t number)
    {
        const std::span<const uint8_t> bounds = Part(file, offsets + number * 4, 8, "ycookie.bin's table of items");
        const std::size_t start = xsflib::LoadBe32(bounds.data()) * kItemUnit;
        const std::size_t end = xsflib::LoadBe32(bounds.data() + 4) * kItemUnit;
        const std::span<const uint8_t> bytes =
            Part(items, start, end < start ? 0 : end - start, "an item of ycookie.bin");
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };

    const auto first_set = static_cast<uint16_t>(catalogue.sets.size());
    for (uint32_t g = 0; g < kGroups; g++)
    {
        catalogue.sets.push_back({.project = item(g * 3),
                                  .pool = item(g * 3 + 1),
                                  .sdir = item(g * 3 + 2),
                                  .samples = item(kFirstSamples + g)});
    }

    const uint8_t volume =
        Part(file, module.At(kData, kLevels), kDefaultLevel + 1, "Yoshi's Cookie's volumes")[kDefaultLevel];
    std::vector<MusyXCatalogue::Write> writes = PuzzleAuxWrites(profile, profile.other_reverb, std::nullopt);
    const uint32_t limits = profile.params.at(kMusyXVoices) << 24 | kMusicVoices << 16 | kEffectVoices << 8 |
                            profile.params.at(kMusyXStudios);
    writes.push_back({profile.musyx_voice_limits, limits});
    const std::size_t songs = module.At(kData, kSongs);
    for (uint32_t s = 0; s < kSongCount; s++)
    {
        const std::span<const uint8_t> row = Part(file, songs + s * kSongEntry, kSongEntry, "Yoshi's Cookie's songs");
        const uint32_t group = xsflib::LoadBe32(row.data()), id = xsflib::LoadBe32(row.data() + 4);
        if (group >= kGroups || id > 0xffff)
        {
            throw xsflib::FormatError("Yoshi's Cookie's song " + std::to_string(s) + " has no group");
        }

        const std::string path = TextBeforeZero(row.subspan(kPathOffset, kPathSize));
        const std::string name = path.substr(path.find_last_of('/') + 1);
        MusyXCatalogue::Entry entry;
        entry.label = "ycookie-" + name.substr(0, name.find_last_of('.'));
        entry.volume = volume;
        entry.id = static_cast<uint16_t>(id);
        entry.song_group = static_cast<uint16_t>(group);
        for (const uint16_t start_group : kStartGroups)
        {
            entry.groups.push_back({.id = start_group, .set = static_cast<uint16_t>(first_set + start_group)});
        }

        if (std::find(std::begin(kStartGroups), std::end(kStartGroups), group) == std::end(kStartGroups))
        {
            entry.groups.push_back({.id = entry.song_group, .set = static_cast<uint16_t>(first_set + group)});
        }

        entry.writes = writes;
        AddSong(catalogue, std::move(entry), item(kFirstSong + s));
    }
}

// Nintendo Puzzle Collection's songs (see MusyXFiles::kPuzzleCollection). The disc holds a menu and three games: Dr.
// Mario, Panel de Pon and Yoshi's Cookie. main.dol loads each of them as a module of code, from a file that Yay0 packs
// (UnpackYay0). Some of the paths in the code differ in case from the disc's names. The disc's library matches
// names in any case. Each module sets up studio 0's aux effects for its songs (PuzzleAuxWrites). The menu, Dr. Mario
// and Panel de Pon give aux A the high-quality reverb of the profile's reverbs[0], and aux B a chorus. Yoshi's Cookie
// gives aux A the standard reverb of the profile's `other_reverb`, and aux B no effect. A rip holds the menu's songs,
// then those of Dr. Mario, Panel de Pon and Yoshi's Cookie. A song's label starts with the module's name, such as
// "panepon-flower_01".
MusyXCatalogue GatherPuzzleCollection(const disc::GameFiles& game, const GameProfile& profile)
{
    const AnyCaseFiles files(game);
    MusyXCatalogue catalogue;
    AddPuzzleMenu(catalogue, files, profile);
    AddPuzzleGame(catalogue, files, profile, kDrMario);
    AddPuzzleGame(catalogue, files, profile, kPanelDePon);
    AddYoshisCookie(catalogue, files, profile);

    return catalogue;
}

// The NDDEMO tech demo's sounds (see MusyXFiles::kNdDemo). The demo's code holds the addresses of the songs' paths at
// `musyx_songs`: `musyx_song_count` words, in the order of the songs' IDs. A song takes its file's name. One set of
// MusyX's files, audio/nddemo, holds the demo's two groups: group 0 for the songs and group 1 for the effects. The demo
// pushes both at start-up, in that order, and keeps them. It plays a song through sndSeqPlayEx with group 0 and the
// song's ID, then sets the song's volume to 0x7F with sndSeqVolume.
//
// The effects follow the songs, in the order of the effects group's table. The demo starts each of its four effects
// through sndFXStartEx with the effect's ID, the volume and the pan that the group's table gives it, and studio 0. The
// disc's header audio/nddemo.h names the effects (HeaderEffectNames).
MusyXCatalogue GatherNdDemo(const disc::GameFiles& game, const GameProfile& profile)
{
    constexpr uint8_t kSongVolume = 0x7f;
    constexpr uint16_t kSongGroup = 0, kEffectsGroup = 1;

    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    MusyXCatalogue catalogue;
    catalogue.sets.push_back({.project = ReadGameFile(game, "audio/nddemo.prj"),
                              .pool = ReadGameFile(game, "audio/nddemo.pol"),
                              .sdir = ReadGameFile(game, "audio/nddemo.sdr"),
                              .samples = ReadGameFile(game, "audio/nddemo.smp")});
    for (uint32_t id = 0; id < profile.musyx_song_count; id++)
    {
        const std::span<const uint8_t> row = Part(DolBytes(dol, profile.musyx_songs + id * 4), 0, 4, "the song table");
        const std::string path = TextBeforeZero(DolBytes(dol, xsflib::LoadBe32(row.data())));
        const std::string file = path.substr(path.find_last_of('/') + 1);

        MusyXCatalogue::Entry entry;
        entry.label = file.substr(0, file.find_last_of('.'));
        entry.volume = kSongVolume;
        entry.id = static_cast<uint16_t>(id);
        entry.song_group = kSongGroup;
        entry.groups = {{kSongGroup, 0}, {kEffectsGroup, 0}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
        AddSong(catalogue, std::move(entry), ReadGameFile(game, path));
    }

    const std::map<uint16_t, std::string> names = HeaderEffectNames(ReadGameFile(game, "audio/nddemo.h"));
    for (const ProjectEffect& effect : ProjectEffects(catalogue.sets[0].project))
    {
        MusyXCatalogue::Entry entry;
        const auto name = names.find(effect.id);
        entry.label = name != names.end() ? name->second : EffectLabel("nddemo", effect.id);
        entry.kind = MusyXCatalogue::Kind::kEffect;
        entry.id = effect.id;
        entry.groups = {{kSongGroup, 0}, {kEffectsGroup, 0}};
        entry.writes = SoundWrites(profile, profile.reverbs[0]);
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
        return GatherRetro(game, profile);
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
    case MusyXFiles::kPokemonChannel:
        return GatherPokemonChannel(game, profile);
    case MusyXFiles::kRebelStrike:
        return GatherRebelStrike(game, profile);
    case MusyXFiles::kPuzzleCollection:
        return GatherPuzzleCollection(game, profile);
    case MusyXFiles::kNdDemo:
        return GatherNdDemo(game, profile);
    case MusyXFiles::kNone:
        break;
    }

    throw xsflib::FormatError(std::string(profile.name) + " has no catalogue of MusyX sounds");
}

std::vector<uint8_t> UnpackYay0(std::span<const uint8_t> file)
{
    constexpr uint32_t kMagic = 0x59617930; // "Yay0"
    constexpr std::size_t kHeader = 16;
    constexpr uint32_t kMaxSize = 64u << 20;
    constexpr uint32_t kTopBit = 0x80000000u;
    if (file.size() < kHeader || xsflib::LoadBe32(file.data()) != kMagic)
    {
        throw xsflib::FormatError("a file isn't packed with Yay0");
    }

    const uint32_t size = xsflib::LoadBe32(file.data() + 4);
    if (size > kMaxSize)
    {
        throw xsflib::FormatError("a Yay0 file unpacks to more than 64 MiB");
    }

    // The next `count` bytes of the table at `at`. Moves `at` past them.
    const auto take = [file](std::size_t& at, std::size_t count)
    {
        const uint8_t* bytes = Part(file, at, count, "a Yay0 file's table").data();
        at += count;

        return bytes;
    };
    std::size_t masks = kHeader;
    std::size_t copies = xsflib::LoadBe32(file.data() + 8);
    std::size_t bytes = xsflib::LoadBe32(file.data() + 12);
    std::vector<uint8_t> out;
    out.reserve(size);
    uint32_t mask = 0;
    int bits = 0;
    while (out.size() < size)
    {
        if (bits == 0)
        {
            mask = xsflib::LoadBe32(take(masks, 4));
            bits = 32;
        }

        const bool literal = (mask & kTopBit) != 0;
        mask <<= 1;
        bits--;
        if (literal)
        {
            out.push_back(*take(bytes, 1));
            continue;
        }

        const uint16_t copy = xsflib::LoadBe16(take(copies, 2));
        const std::size_t distance = (copy & 0xfffu) + std::size_t{1};
        const std::size_t length =
            (copy >> 12) != 0 ? (copy >> 12) + std::size_t{2} : *take(bytes, 1) + std::size_t{18};
        if (distance > out.size())
        {
            throw xsflib::FormatError("a Yay0 file copies bytes from before its start");
        }

        for (std::size_t i = 0; i < length && out.size() < size; i++)
        {
            out.push_back(out[out.size() - distance]);
        }
    }

    return out;
}

namespace
{

// The catalogue's file (docs/gcsf.md, "MusyX catalogue"). Its header holds eight words: the magic, the version, then
// the number and the offset of the sounds' entries, of the group entries and of the words. A group entry holds the
// group's ID and the number of its file set, a half each. A word holds an address and a value. Every value is
// big-endian.
constexpr uint32_t kCatalogueMagic = 0x47434d58; // 'GCMX'
constexpr uint32_t kCatalogueVersion = 1;
constexpr uint32_t kHeaderSize = 32, kSoundSize = 20, kGroupSize = 4, kWriteSize = 8;
constexpr uint32_t kHeaderMagic = 0, kHeaderVersion = 4, kHeaderSounds = 8, kHeaderSoundsAt = 12, kHeaderGroups = 16,
                   kHeaderGroupsAt = 20, kHeaderWrites = 24, kHeaderWritesAt = 28;

// The fields of a sound's entry: its kind, its volume, its pan and its studio in a byte each, then halves. The halves
// give MusyX's ID, the song's group, the song's file, the first group entry, the number of group entries, the first
// word, the number of words and the number of controller settings.
constexpr uint32_t kSoundKind = 0, kSoundVolume = 1, kSoundPan = 2, kSoundStudio = 3, kSoundId = 4, kSoundSongGroup = 6,
                   kSoundSong = 8, kSoundFirstGroup = 10, kSoundGroups = 12, kSoundFirstWrite = 14, kSoundWrites = 16,
                   kSoundControls = 18;

} // namespace

std::vector<uint8_t> CatalogueFile(const MusyXCatalogue& catalogue)
{
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

    constexpr uint32_t kSoundsAt = kHeaderSize;
    const uint32_t groups_at = kSoundsAt + static_cast<uint32_t>(catalogue.entries.size()) * kSoundSize;
    const uint32_t writes_at = groups_at + static_cast<uint32_t>(groups) * kGroupSize;
    std::vector<uint8_t> file(writes_at + writes * kWriteSize);
    uint8_t* p = file.data();
    xsflib::StoreBe32(p + kHeaderMagic, kCatalogueMagic);
    xsflib::StoreBe32(p + kHeaderVersion, kCatalogueVersion);
    xsflib::StoreBe32(p + kHeaderSounds, static_cast<uint32_t>(catalogue.entries.size()));
    xsflib::StoreBe32(p + kHeaderSoundsAt, kSoundsAt);
    xsflib::StoreBe32(p + kHeaderGroups, static_cast<uint32_t>(groups));
    xsflib::StoreBe32(p + kHeaderGroupsAt, groups_at);
    xsflib::StoreBe32(p + kHeaderWrites, static_cast<uint32_t>(writes));
    xsflib::StoreBe32(p + kHeaderWritesAt, writes_at);

    uint32_t group = 0, write = 0;
    for (std::size_t i = 0; i < catalogue.entries.size(); i++)
    {
        const MusyXCatalogue::Entry& entry = catalogue.entries[i];
        uint8_t* sound = p + kSoundsAt + i * kSoundSize;
        sound[kSoundKind] = static_cast<uint8_t>(entry.kind);
        sound[kSoundVolume] = entry.volume;
        sound[kSoundPan] = entry.pan;
        sound[kSoundStudio] = entry.studio;
        xsflib::StoreBe16(sound + kSoundId, entry.id);
        xsflib::StoreBe16(sound + kSoundSongGroup, entry.song_group);
        xsflib::StoreBe16(sound + kSoundSong, entry.song);
        xsflib::StoreBe16(sound + kSoundFirstGroup, static_cast<uint16_t>(group));
        xsflib::StoreBe16(sound + kSoundGroups, static_cast<uint16_t>(entry.groups.size()));
        xsflib::StoreBe16(sound + kSoundFirstWrite, static_cast<uint16_t>(write));
        xsflib::StoreBe16(sound + kSoundWrites, static_cast<uint16_t>(entry.writes.size()));
        xsflib::StoreBe16(sound + kSoundControls, static_cast<uint16_t>(entry.controls.size()));
        for (const MusyXCatalogue::Group& g : entry.groups)
        {
            xsflib::StoreBe16(p + groups_at + group * kGroupSize, g.id);
            xsflib::StoreBe16(p + groups_at + group * kGroupSize + 2, g.set);
            group++;
        }

        for (const MusyXCatalogue::Write& w : entry.writes)
        {
            xsflib::StoreBe32(p + writes_at + write * kWriteSize, w.address);
            xsflib::StoreBe32(p + writes_at + write * kWriteSize + 4, w.value);
            write++;
        }

        // The controllers follow the words. Bit 8 of the first value picks sndFXCtrl14.
        for (const MusyXCatalogue::Control& c : entry.controls)
        {
            xsflib::StoreBe32(p + writes_at + write * kWriteSize, c.controller | (c.fine ? 0x100u : 0u));
            xsflib::StoreBe32(p + writes_at + write * kWriteSize + 4, c.value);
            write++;
        }
    }

    return file;
}

std::optional<CatalogueSound> ReadCatalogueSound(std::span<const uint8_t> file, uint32_t number)
{
    if (file.size() < kHeaderSoundsAt + 4 || number >= xsflib::LoadBe32(file.data() + kHeaderSounds))
    {
        return std::nullopt;
    }

    const uint64_t at = uint64_t{xsflib::LoadBe32(file.data() + kHeaderSoundsAt)} + uint64_t{number} * kSoundSize;
    if (at + kSoundSize > file.size())
    {
        return std::nullopt;
    }

    const uint8_t* sound = file.data() + at;
    return CatalogueSound{.kind = static_cast<MusyXCatalogue::Kind>(sound[kSoundKind]),
                          .song = xsflib::LoadBe16(sound + kSoundSong)};
}

MusyXFilesInUse FilesInUse(const MusyXCatalogue& catalogue, std::span<const uint32_t> sounds)
{
    MusyXFilesInUse use{.sets = std::vector<bool>(catalogue.sets.size()),
                        .songs = std::vector<bool>(catalogue.songs.size())};
    for (const uint32_t number : sounds)
    {
        if (number >= catalogue.entries.size())
        {
            continue;
        }

        const MusyXCatalogue::Entry& entry = catalogue.entries[number];
        for (const MusyXCatalogue::Group& group : entry.groups)
        {
            if (group.set < use.sets.size())
            {
                use.sets[group.set] = true;
            }
        }

        // An entry without a song holds kNoSong. That number lies past the last song.
        if (entry.song < use.songs.size())
        {
            use.songs[entry.song] = true;
        }
    }

    return use;
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
