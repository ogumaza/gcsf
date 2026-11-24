// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is MusyX: the sounds of a game for driver/musyx.c and the songs of
// another MusyX game, the library's sound files, and the length analysis of a song or an effect. The catalogue for
// driver/musyx.c comes from rip/musyx_catalogue.h.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/musyx/catalogue.h"
#include "ntdlib/musyx/game_tables.h"
#include "ntdlib/musyx/project.h"
#include "ntdlib/ppc/cpu.h"
#include "ntdlib/zero_padded.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/rip/length.h"

#include "gc/system.h"
#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/musyx_catalogue.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{
namespace
{

// A song of a Camelot game: its list, 0 for the songs and 1 for the jingles, its number in the list, and the list's
// entry.
struct SongEntry
{
    uint32_t table = 0;
    uint32_t number = 0;
    ntdlib::musyx::CamelotSong song;
};

// The songs of a Camelot game's lists, the songs then the jingles (GameProfile::song_table). Throws xsflib::FormatError
// if a list or a name is outside the game's code.
std::vector<SongEntry> SongEntries(const ntdlib::Dol& dol, const GameProfile& profile)
{
    // An entry contains the name or its address, then three words: the file number, the song's ID and its volume.
    constexpr uint32_t kSongWords = 12;
    const uint32_t name_size = profile.song_name_size != 0 ? profile.song_name_size : 4;
    const bool name_inline = profile.song_name_size != 0;
    const ntdlib::musyx::CamelotSongList lists[2] = {
        {.address = profile.song_table,
         .count = profile.song_count,
         .entry_size = name_size + kSongWords,
         .name_inline = name_inline},
        {.address = profile.jingle_table,
         .count = profile.jingle_count,
         .entry_size = name_size + kSongWords,
         .name_inline = name_inline},
    };
    std::vector<SongEntry> songs;
    for (uint32_t table = 0; table < 2; table++)
    {
        std::vector<ntdlib::musyx::CamelotSong> listed = ntdlib::musyx::ReadCamelotSongs(dol, lists[table]);
        for (uint32_t number = 0; number < listed.size(); number++)
        {
            songs.push_back({table, number, std::move(listed[number])});
        }
    }

    return songs;
}

// The songs that a MusyX game's sound room names (GameProfile::named_songs), in the order of its table. Throws
// xsflib::FormatError if the table or a name is outside the game's code.
std::vector<Sound> NamedSongs(const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<Sound> songs;
    for (ntdlib::musyx::SoundRoomSong& song :
         ntdlib::musyx::ReadSoundRoom(dol, profile.named_songs, profile.named_song_count))
    {
        songs.push_back({song.entry_word, std::move(song.name), true});
    }

    return songs;
}

// The effects that a game's play function starts by number (GameProfile::effect_songs), after the `songs` that its
// code names: the songs that the game plays as effects, then MusyX's effects. Each is named after its number, such as
// "se-0013". A song's ID is as in NamedSongs. An effect's ID is the profile's `musyx_effect_bit` and the number. A
// number that a sound takes already, or a song's number that an effect takes, isn't listed again. Throws
// xsflib::FormatError if a table is outside the game's code.
std::vector<Sound> PlayedEffects(const ntdlib::Dol& dol, const GameProfile& profile, const std::vector<Sound>& songs)
{
    std::set<uint32_t> listed;
    for (const Sound& song : songs)
    {
        listed.insert(song.id & 0xffff);
    }

    std::vector<uint16_t> effects;
    for (const ntdlib::musyx::PlayedEffect& effect :
         ntdlib::musyx::ReadPlayedEffects(dol, profile.play_effects, profile.play_effect_count))
    {
        effects.push_back(effect.number);
    }

    std::vector<Sound> sounds;
    for (ntdlib::musyx::SoundRoomSong& song :
         ntdlib::musyx::ReadEffectSongs(dol, profile.effect_songs, profile.effect_song_count))
    {
        if (std::find(effects.begin(), effects.end(), song.number) == effects.end() &&
            listed.insert(song.number).second)
        {
            sounds.push_back({uint32_t{song.group_id} << 24 | song.number, std::move(song.name), false});
        }
    }

    for (const uint16_t number : effects)
    {
        if (listed.insert(number).second)
        {
            sounds.push_back({profile.musyx_effect_bit | number, ntdlib::musyx::EffectName("se", number), false});
        }
    }

    return sounds;
}

// The sections of driver/mario_golf.c's sound IDs for effects, in the top byte (see CamelotEffects).
constexpr uint32_t kSectionEffects = 0x80, kSectionNarrations = 0x81, kSectionCourtEffects = 0x82,
                   kSectionGroupEffects = 0x83;

// A group's four files: its pool, its project, its sample directory and its samples, two file numbers apart.
constexpr uint32_t kFileStep = 2, kGroupFiles = 4;

// The voice banks of a Camelot game (GameProfile::voice_banks). Throws xsflib::FormatError if the table or a name is
// outside the game's code.
std::vector<ntdlib::musyx::VoiceBank> VoiceBanks(const ntdlib::Dol& dol, const GameProfile& profile)
{
    return ntdlib::musyx::ReadVoiceBanks(dol, {.address = profile.voice_banks,
                                               .count = profile.voice_bank_count,
                                               .entry_size = profile.voice_bank_size,
                                               .name_size = profile.voice_bank_name_size});
}

// The four files of each group that holds a game's effects beside its sound banks (see GameProfile::voice_banks): the
// voice banks' groups, the narrations' groups, the group of the narrations that the game keeps, the courts' groups and
// the groups that no table lists. Each group's files are listed once. Throws xsflib::FormatError if a table is
// outside the game's code.
std::vector<std::string> CamelotEffectFiles(const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<uint32_t> firsts;
    for (const ntdlib::musyx::VoiceBank& bank : VoiceBanks(dol, profile))
    {
        firsts.push_back(bank.first_file);
    }

    for (const ntdlib::musyx::Narration& narration :
         ntdlib::musyx::ReadNarrations(dol, profile.narrations, profile.narration_count))
    {
        firsts.push_back(narration.first_file);
    }

    firsts.push_back(profile.narration_files != 0 ? profile.narration_files : ntdlib::musyx::kCamelotNoFiles);
    for (const ntdlib::musyx::CourtBank& court :
         ntdlib::musyx::ReadCourtBanks(dol, profile.court_banks, profile.court_bank_count))
    {
        firsts.push_back(court.first_file);
    }

    for (uint32_t g = 0; g < profile.other_group_count; g++)
    {
        firsts.push_back(profile.other_groups + g * kGroupFiles * kFileStep);
    }

    std::vector<std::string> paths;
    std::set<uint32_t> listed;
    for (const uint32_t first : firsts)
    {
        if (first == ntdlib::musyx::kCamelotNoFiles || !listed.insert(first).second)
        {
            continue;
        }

        for (uint32_t f = 0; f < kGroupFiles; f++)
        {
            paths.push_back(ntdlib::musyx::CamelotPath(first + f * kFileStep));
        }
    }

    return paths;
}

// The effects of a Camelot game by ID, each with its sound ID and label (see CamelotEffects).
using CamelotEffectMap = std::map<uint32_t, Sound>;

// Adds effect `id` of `bank` with the sound ID `sound` to `effects`, unless `effects` has the ID. Its label is the
// bank, a hyphen and the ID in four digits.
void AddCamelotEffect(CamelotEffectMap& effects, uint32_t id, uint32_t sound, const std::string& bank)
{
    effects.try_emplace(id, Sound{sound, bank + "-" + ntdlib::ZeroPadded(id, 4), false});
}

// The project of the group whose first file is number `first`. Throws xsflib::FormatError if the project is missing or
// too short for a project.
std::vector<uint8_t> ReadCamelotProject(const disc::GameFiles& game, uint32_t first)
{
    constexpr uint32_t kProjectFile = 2, kGroupHeader = 6;
    const std::string path = ntdlib::musyx::CamelotPath(first + kProjectFile);
    std::vector<uint8_t> project = DiscFile(game, path);
    if (project.size() < kGroupHeader)
    {
        throw xsflib::FormatError(path + " is too short for a project");
    }

    return project;
}

// The effects of the banks that the game loads at start-up: the ranges of IDs in the table at `effect_ranges`.
void AddStartUpEffects(const ntdlib::Dol& dol, const GameProfile& profile, CamelotEffectMap& effects)
{
    if (profile.effect_ranges == 0)
    {
        return;
    }

    for (const ntdlib::musyx::CamelotRange& range : ntdlib::musyx::ReadCamelotRanges(dol, profile.effect_ranges))
    {
        for (uint32_t id = range.first; id <= range.last && id <= 0xffff; id++)
        {
            AddCamelotEffect(effects, id, kSectionEffects << 24 | id, "se");
        }
    }
}

// The effects of the voice banks, each labelled with its bank's name.
void AddVoiceBankEffects(const ntdlib::Dol& dol, const GameProfile& profile, CamelotEffectMap& effects)
{
    const std::vector<ntdlib::musyx::VoiceBank> banks = VoiceBanks(dol, profile);
    for (uint32_t b = 0; b < banks.size(); b++)
    {
        const ntdlib::musyx::VoiceBank& bank = banks[b];
        for (uint32_t id = bank.first_effect; id < bank.first_effect + bank.effect_count && id <= 0xffff; id++)
        {
            AddCamelotEffect(effects, id, kSectionEffects << 24 | (b + 1) << 16 | id, bank.name);
        }
    }
}

// The narrations' effects.
void AddNarrationEffects(const ntdlib::Dol& dol, const GameProfile& profile, CamelotEffectMap& effects)
{
    const std::vector<ntdlib::musyx::Narration> narrations =
        ntdlib::musyx::ReadNarrations(dol, profile.narrations, profile.narration_count);
    for (uint32_t n = 0; n < narrations.size(); n++)
    {
        AddCamelotEffect(effects, narrations[n].effect_id, kSectionNarrations << 24 | n, "narration");
    }
}

// The effects of the courts' banks: the effects of each bank's group in its project.
void AddCourtEffects(const ntdlib::Dol& dol, const disc::GameFiles& game, const GameProfile& profile,
                     CamelotEffectMap& effects)
{
    const std::vector<ntdlib::musyx::CourtBank> courts =
        ntdlib::musyx::ReadCourtBanks(dol, profile.court_banks, profile.court_bank_count);
    std::set<uint32_t> groups;
    for (uint32_t c = 0; c < courts.size(); c++)
    {
        const ntdlib::musyx::CourtBank& court = courts[c];
        if (court.first_file == ntdlib::musyx::kCamelotNoFiles || !groups.insert(court.group_id).second)
        {
            continue;
        }

        for (const ntdlib::musyx::ProjectEffect& effect :
             ntdlib::musyx::ProjectEffects(ntdlib::musyx::ReadProject(ReadCamelotProject(game, court.first_file))))
        {
            if (effect.group == court.group_id)
            {
                AddCamelotEffect(effects, effect.row.id, kSectionCourtEffects << 24 | c << 16 | effect.row.id, "court");
            }
        }
    }
}

// The effects of the groups that no table lists: those of the first group in each group's project.
void AddOtherGroupEffects(const disc::GameFiles& game, const GameProfile& profile, CamelotEffectMap& effects)
{
    constexpr uint32_t kMaxGroupField = 0xfff;
    for (uint32_t g = 0; g < profile.other_group_count; g++)
    {
        const uint32_t first = profile.other_groups + g * kGroupFiles * kFileStep;
        const std::vector<uint8_t> project = ReadCamelotProject(game, first);
        const uint16_t group = xsflib::LoadBe16(project.data() + 4);
        for (const ntdlib::musyx::ProjectEffect& effect :
             ntdlib::musyx::ProjectEffects(ntdlib::musyx::ReadProject(project)))
        {
            if (effect.group == group && effect.row.id <= kMaxGroupField && first / 2 <= kMaxGroupField)
            {
                AddCamelotEffect(effects, effect.row.id, kSectionGroupEffects << 24 | first / 2 << 12 | effect.row.id,
                                 "se");
            }
        }
    }
}

// The effects of a game for driver/mario_golf.c (see GameProfile::voice_banks), in the order of their IDs. A sound
// ID's top byte gives the driver's section for the effect. An effect of the banks that the game loads at start-up
// gives its ID below. An effect of a voice bank adds the bank's number plus 1 in bits 16 to 23. An effect of a court's
// bank gives the court's number there. A narration gives its number in the game's table. The effects of a court's
// bank are those of its group's project. An effect of a group that no table lists gives its ID in bits 0 to 11 and
// half the number of the group's first file in bits 12 to 23. The effects of such a group are those of the first group
// in its project. An effect's label is its bank's name, "se" for the banks of the start-up and the groups that no
// table lists, or "court" for a court's bank, then a hyphen and its ID in four digits, such as "mario-0154". A
// narration's label is "narration-" and its effect's ID. Of two tables that give an ID, the first in that order wins.
// Throws xsflib::FormatError if a table is outside the game's code, or if a group's project is missing or damaged.
std::vector<Sound> CamelotEffects(const ntdlib::Dol& dol, const disc::GameFiles& game, const GameProfile& profile)
{
    CamelotEffectMap effects;
    AddStartUpEffects(dol, profile, effects);
    AddVoiceBankEffects(dol, profile, effects);
    AddNarrationEffects(dol, profile, effects);
    AddCourtEffects(dol, game, profile, effects);
    AddOtherGroupEffects(game, profile, effects);

    std::vector<Sound> sounds;
    for (auto& [id, sound] : effects)
    {
        sounds.push_back(std::move(sound));
    }

    return sounds;
}

} // namespace

std::vector<Sound> MusyXSongs(const disc::GameFiles& game, const GameProfile& profile)
{
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    if (profile.named_songs)
    {
        std::vector<Sound> sounds = NamedSongs(dol, profile);
        std::vector<Sound> effects = PlayedEffects(dol, profile, sounds);
        sounds.insert(sounds.end(), std::make_move_iterator(effects.begin()), std::make_move_iterator(effects.end()));

        return sounds;
    }

    std::vector<Sound> songs;
    for (SongEntry& song : SongEntries(dol, profile))
    {
        std::string label = std::move(song.song.name);
        if (label.size() > 4 && xsflib::AsciiLower(label.substr(label.size() - 4)) == ".sng")
        {
            label.resize(label.size() - 4);
        }

        songs.push_back({song.table << 24 | song.number, std::move(label), true});
    }

    std::vector<Sound> effects = CamelotEffects(dol, game, profile);
    songs.insert(songs.end(), std::make_move_iterator(effects.begin()), std::make_move_iterator(effects.end()));

    return songs;
}

std::vector<Sound> CatalogueSounds(const disc::GameFiles& game, const GameProfile& profile)
{
    const MusyXCatalogue catalogue = GatherMusyX(game, profile);
    std::vector<Sound> sounds;
    for (std::size_t i = 0; i < catalogue.entries.size(); i++)
    {
        const MusyXCatalogue::Entry& entry = catalogue.entries[i];
        const bool song = entry.kind == MusyXCatalogue::Kind::kSong;
        const uint32_t key = uint32_t{entry.song_group} << 16 | entry.id;
        const std::vector<uint32_t>& streamed = profile.musyx_streamed_songs;
        const bool stream = song && std::find(streamed.begin(), streamed.end(), key) != streamed.end();
        const bool music = song ? !entry.sound_effect : entry.music;
        sounds.push_back({static_cast<uint32_t>(i), entry.label, music, stream});
    }

    return sounds;
}

std::optional<std::string> AddMusyXCatalogue(ProgramBuilder& program, const disc::GameFiles& game,
                                             const GameProfile& profile, const std::vector<uint32_t>* sounds)
{
    try
    {
        const MusyXCatalogue catalogue = GatherMusyX(game, profile);
        program.AddFile(kMusyXCatalogue, CatalogueFile(catalogue));

        // A file that none of `sounds` uses keeps its size, but holds zeros.
        const std::optional<MusyXFilesInUse> use =
            sounds ? std::optional<MusyXFilesInUse>(FilesInUse(catalogue, *sounds)) : std::nullopt;
        const auto add = [&program](uint32_t number, const char* extension, const std::vector<uint8_t>& data, bool used)
        {
            program.AddFile(MusyXPath(number, extension), used ? data : std::vector<uint8_t>(data.size(), 0));
        };

        for (std::size_t i = 0; i < catalogue.sets.size(); i++)
        {
            const MusyXCatalogue::FileSet& set = catalogue.sets[i];
            const uint32_t number = static_cast<uint32_t>(i);
            const bool used = !use || use->sets[i];
            add(number, ".proj", set.project, used);
            add(number, ".pool", set.pool, used);
            add(number, ".sdir", set.sdir, used);
            add(number, ".samp", set.samples, used);
        }

        for (std::size_t i = 0; i < catalogue.songs.size(); i++)
        {
            add(static_cast<uint32_t>(i), ".sng", catalogue.songs[i], !use || use->songs[i]);
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return e.what();
    }

    return std::nullopt;
}

std::optional<std::string> AddMusyXFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                         const GameProfile& profile, const std::vector<uint32_t>* sounds)
{
    std::vector<std::string> paths(profile.bank_files.begin(), profile.bank_files.end());
    std::vector<std::string> effect_files;
    try
    {
        const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
        for (const SongEntry& song : SongEntries(dol, profile))
        {
            paths.push_back(ntdlib::musyx::CamelotPath(song.song.file_number));
        }

        effect_files = CamelotEffectFiles(dol, profile);
    }
    catch (const xsflib::FormatError& e)
    {
        return e.what();
    }

    if (const std::optional<std::string> problem = AddDiscFiles(program, game, paths))
    {
        return problem;
    }

    // Only an effect reads the files of the effects' groups. Without one, each file keeps its size, but holds zeros.
    const auto effect = [&profile](uint32_t id)
    {
        return (id & profile.musyx_effect_bit) != 0;
    };
    const bool any_effect = !sounds || std::any_of(sounds->begin(), sounds->end(), effect);

    return AddDiscFiles(program, game, effect_files, any_effect);
}

namespace
{

// A MusyX song (see MeasuredSound). It plays while it's in one of MusyX's lists of songs. A jingle's thread waits half
// a second before it starts the jingle. Each section of the song counts its loops (see MusyXSections). The song has
// looped once more when each section that loops has.
class MusyXSong : public MeasuredSound
{
public:
    MusyXSong(Player& player, const GameProfile& profile, const MusyXSections& sections)
        : MeasuredSound(player), profile_(profile), sections_(sections)
    {
        last_ticks_.assign(profile.track_slot_count, 0);
    }

private:
    bool Playing() override
    {
        return Word(profile_.song_lists[0]) != 0 || Word(profile_.song_lists[1]) != 0;
    }

    bool Looped() override
    {
        CountSectionLoops();

        // The song's count is the least of the counts of its sections that loop.
        uint32_t least = UINT32_MAX;
        for (std::size_t s = 0; s < MusyXSections::kSections; s++)
        {
            if (sections_.looping >> s & 1)
            {
                least = std::min(least, section_loops_[s]);
            }
        }

        if (sections_.looping == 0 || least <= song_loops_)
        {
            return false;
        }

        song_loops_ = least;

        return true;
    }

    // Brings each section's count of its loops up to date. MusyX keeps the counts where the profile gives them.
    // Otherwise a section loops in a frame in which the slots of its tracks fall. A loop moves the section's tracks
    // back, all in one frame. A slot's new tick depends on the pass. A section whose tracks reach their loop entries in
    // the frame of the loop shows no fall.
    void CountSectionLoops()
    {
        if (profile_.section_loops != 0)
        {
            for (uint32_t s = 0; s < MusyXSections::kSections; s++)
            {
                section_loops_[s] = Word(profile_.section_loops + s * profile_.section_loop_size) >> 16;
            }

            return;
        }

        uint32_t fell = 0;
        for (uint32_t k = 0; k < profile_.track_slot_count && k < MusyXSections::kTracks; k++)
        {
            const uint32_t ticks = Word(profile_.track_slots + k * profile_.track_slot_size);
            if (ticks < last_ticks_[k] && sections_.of_track[k] < MusyXSections::kSections)
            {
                fell |= 1u << sections_.of_track[k];
            }

            last_ticks_[k] = ticks;
        }

        for (std::size_t s = 0; s < MusyXSections::kSections; s++)
        {
            section_loops_[s] += fell >> s & 1;
        }
    }

    const GameProfile& profile_;
    const MusyXSections sections_;
    std::vector<uint32_t> last_ticks_;
    std::array<uint32_t, MusyXSections::kSections> section_loops_ = {};
    uint32_t song_loops_ = 0;
};

// Sound `id` of the catalogue in `library`, or nothing if the library has no catalogue or the catalogue no such sound.
std::optional<CatalogueSound> LibrarySound(const LoadedSet& library, uint32_t id)
{
    const std::vector<uint8_t>* catalogue = library.File(kMusyXCatalogue);
    return catalogue != nullptr ? ReadCatalogueSound(*catalogue, id) : std::nullopt;
}

// The sections of the song that sound `id` plays, from the catalogue in `library` (see MusyXSections). A sound without
// a song, or a library without a catalogue, gets the defaults.
MusyXSections SoundSections(const LoadedSet& library, uint32_t id)
{
    const std::optional<CatalogueSound> sound = LibrarySound(library, id);
    if (!sound)
    {
        return {};
    }

    const std::vector<uint8_t>* file = library.File(MusyXPath(sound->song, ".sng"));
    return file != nullptr ? SectionsOf(*file) : MusyXSections{};
}

// Whether sound `id` of the catalogue in `library` is an effect.
bool IsMusyXEffect(const LoadedSet& library, uint32_t id)
{
    const std::optional<CatalogueSound> sound = LibrarySound(library, id);
    return sound && sound->kind == MusyXCatalogue::Kind::kEffect;
}

// A MusyX effect (see MeasuredSound and MusyXVoices). The effect plays while one of MusyX's voices is busy: while its
// synthesizer voice is in use, or while its DSP voice plays. A rip sends an effect no events, such as a key-off. A
// voice whose macro has ended, or waits for an event, therefore changes no more. The voice has settled. The sound
// changes when a voice starts or stops, moves to another step of its macro, or starts to wait. A voice's macro loops
// when a LOOP step without a count (a count of 0xFFFF) jumps back. The voices' loops give the effect's loops (see
// PartLoops). A voice holds the fade back only while it's busy and hasn't settled.
//
// An effect whose voices have all settled holds. xsflib's MeasureLength loops it at the later of two points: 10 s after
// its sound last changed, and the second time since then that each playing DSP voice has passed the start of its
// sample's loop. An effect whose voices have all settled while no DSP voice plays has ended.
class MusyXEffect : public MeasuredSound
{
public:
    MusyXEffect(Player& player, const GameProfile& profile)
        : MeasuredSound(player),
          voices_(profile.musyx_voices),
          cpu_(player.System()->Cpu()),
          now_(voices_.count),
          loops_(voices_.count),
          watch_(cpu_, {voices_.loop_handler}, [this](uint32_t) { StepLoops(); })
    {
    }

    std::size_t VoiceCount() const override
    {
        return voices_.count;
    }

    xsflib::rip::VoicePlace Voice(std::size_t i) const override
    {
        const VoiceState& v = now_[i];
        return {.playing = v.state != 0, .loops = v.loop != 0, .position = v.position};
    }

    bool Settled() const override
    {
        return settled_ && frame_ - changed_frame_ >= kHoldFrames;
    }

    bool Stuck() const override
    {
        return settled_;
    }

    uint64_t ChangedAt() const override
    {
        return changes_;
    }

    int ActiveChannels() const override
    {
        return sampling_;
    }

private:
    // A voice's state in a frame.
    struct VoiceState
    {
        uint32_t id = kFree;
        uint32_t macro = 0;
        uint32_t step = 0;
        uint64_t wait = 0;
        uint8_t state = 0;
        int64_t position = 0;
        uint32_t loop = 0;

        bool Busy() const
        {
            return id != kFree || state != 0;
        }

        // Whether the voice's macro has ended or waits for an event.
        bool Settled() const
        {
            return id == kFree || macro == 0 || wait == kForever;
        }

        // Whether the voice's sound has changed since `last`. The position of its sample moves on.
        bool ChangedFrom(const VoiceState& last) const
        {
            return id != last.id || macro != last.macro || step != last.step || wait != last.wait ||
                   (state != 0) != (last.state != 0);
        }
    };

    static constexpr uint32_t kFree = 0xffffffff;
    static constexpr uint64_t kForever = UINT64_MAX;

    // A held effect loops 10 s after its sound last changed at the earliest.
    static constexpr uint64_t kHoldFrames = 10 * kLengthRules.sample_rate / kLengthRules.block_frames;

    // Reads the voices. MeasuredSound calls this once a frame.
    bool Playing() override
    {
        frame_ = frames_++;
        const uint32_t synth = Word(voices_.synth);
        const uint32_t dsp = Word(voices_.dsp);
        if (synth == 0 || dsp == 0)
        {
            return false;
        }

        bool busy = false;
        bool changed = false;
        settled_ = true;
        sampling_ = 0;
        for (uint32_t i = 0; i < voices_.count; i++)
        {
            const uint32_t s = synth + i * voices_.synth_size;
            const uint32_t d = dsp + i * voices_.dsp_size;
            VoiceState v;
            v.id = Word(s + voices_.id_offset);
            v.macro = Word(s + voices_.macro_offset);
            v.step = Word(s + voices_.step_offset);
            v.wait = uint64_t{Word(s + voices_.wait_offset)} << 32 | Word(s + voices_.wait_offset + 4);
            v.state = Byte(d + voices_.state_offset);
            v.position = Word(d + voices_.position_offset);
            v.loop = Word(d + voices_.loop_offset);

            changed = changed || v.ChangedFrom(now_[i]);
            busy = busy || v.Busy();
            settled_ = settled_ && v.Settled();
            sampling_ += v.state != 0 ? 1 : 0;
            now_[i] = v;
        }

        if (changed)
        {
            ++changes_;
            changed_frame_ = frame_;
        }

        return busy;
    }

    // MusyX runs a LOOP step of the macro of the voice in r3. The step is in r4: two words. The step's count is the
    // high half of the second word. Bit 16 of the first word makes the count random.
    void StepLoops()
    {
        const uint32_t synth = Word(voices_.synth);
        const uint32_t voice = cpu_.state_.gpr[3];
        const uint32_t step = cpu_.state_.gpr[4];
        if (synth == 0 || voice < synth || voices_.synth_size == 0)
        {
            return;
        }

        const uint32_t i = (voice - synth) / voices_.synth_size;
        if (i >= voices_.count || Word(step + 4) >> 16 != 0xffff || (Word(step) >> 16 & 1) != 0)
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
        for (uint32_t i = 0; i < voices_.count; i++)
        {
            const VoiceState& v = now_[i];
            if (loops_.HoldsBack(i) && v.Busy() && !v.Settled())
            {
                return false;
            }
        }

        return true;
    }

    const MusyXVoices voices_;
    ppc::Cpu& cpu_;
    std::vector<VoiceState> now_; // each voice's state in this frame
    PartLoops loops_;             // the voices' loops
    bool settled_ = false;        // whether every voice has settled
    int sampling_ = 0;            // the DSP voices that play
    uint64_t changes_ = 0;        // the changes of the sound so far
    uint64_t frames_ = 0;         // the frames so far
    uint64_t frame_ = 0;          // this frame
    uint64_t changed_frame_ = 0;  // the frame of the sound's last change
    ScopedWatch watch_;           // the LOOP steps
};

} // namespace

std::optional<Timing> MeasureMusyXSound(Player& player, const GameProfile& profile, const LoadedSet& library,
                                        uint32_t id, xsflib::rip::Limit limit)
{
    if (IsMusyXEffect(library, id) || (id & profile.musyx_effect_bit) != 0)
    {
        if (profile.musyx_voices.count == 0)
        {
            return std::nullopt;
        }

        MusyXEffect effect(player, profile);
        return MeasureSound(effect);
    }

    MusyXSong song(player, profile, SoundSections(library, id));
    return MeasureSound(song, limit);
}

} // namespace gcsf::rip
