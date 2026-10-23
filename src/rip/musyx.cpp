// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is MusyX: the sounds of a game for driver/musyx.c and the songs of
// another MusyX game, the library's sound files, and the length analysis of a song or an effect. The catalogue for
// driver/musyx.c comes from rip/musyx_catalogue.h.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/ppc/cpu.h"
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

// A MusyX game's song, from a table in its code.
struct SongEntry
{
    uint32_t table;  // 0 for the songs, 1 for the jingles
    uint32_t number; // in the table
    uint32_t file_number;
    std::string name; // the file's name
};

// The songs of a MusyX game's tables, the songs then the jingles. Throws xsflib::FormatError if a table or a name lies
// outside the game's code.
std::vector<SongEntry> SongEntries(const ntdlib::Dol& dol, const GameProfile& profile)
{
    // An entry holds the name or its address, then the file number, the song's number and its volume.
    const uint32_t name_size = profile.song_name_size != 0 ? profile.song_name_size : 4;
    const uint32_t entry_size = name_size + 12;
    std::vector<SongEntry> songs;
    const uint32_t tables[2][2] = {{profile.song_table, profile.song_count},
                                   {profile.jingle_table, profile.jingle_count}};
    for (uint32_t table = 0; table < 2; table++)
    {
        for (uint32_t number = 0; number < tables[table][1]; number++)
        {
            const std::span<const uint8_t> entry = DolBytes(dol, tables[table][0] + number * entry_size);
            if (entry.size() < entry_size)
            {
                throw xsflib::FormatError("main.dol's song table ends early");
            }

            const std::span<const uint8_t> name =
                profile.song_name_size != 0 ? entry.first(name_size) : DolBytes(dol, xsflib::LoadBe32(entry.data()));
            std::string label =
                TerminatedText(name, profile.song_name_size != 0 ? "main.dol's song name runs past its entry"
                                                                 : "main.dol's song name runs past its section");
            songs.push_back({table, number, xsflib::LoadBe32(entry.data() + name_size), std::move(label)});
        }
    }

    return songs;
}

// The disc path of a MusyX game's song. Camelot's file number holds the file's index above its low bit. The low bit is
// a flag that the game's loader (0x8000b214) tests. The game's path function (0x8001bbf0) drops the flag and writes the
// index in base 26 as three letters, from "A/A/A". Every song's number has the flag clear.
std::string SongPath(uint32_t file_number)
{
    const uint32_t n = file_number >> 1;
    return {static_cast<char>('A' + n / 676 % 26), '/', static_cast<char>('A' + n / 26 % 26), '/',
            static_cast<char>('A' + n % 26)};
}

// The songs that a MusyX game's code names (GameProfile::named_songs), in the order of its table. Throws
// xsflib::FormatError if the table or a name lies outside the game's code.
std::vector<Sound> NamedSongs(const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<Sound> songs;
    for (uint32_t i = 0; i < profile.named_song_count; i++)
    {
        const std::span<const uint8_t> entry = DolBytes(dol, profile.named_songs + i * profile.named_song_size);
        if (entry.size() < profile.named_song_size)
        {
            throw xsflib::FormatError("main.dol's song table ends early");
        }

        const std::span<const uint8_t> name = DolBytes(dol, xsflib::LoadBe32(entry.data() + profile.named_song_name));
        std::string label = TerminatedText(name, "main.dol's song name runs past its section");

        // The game's text marks the space between two words with the bytes 0x94 0xBC.
        for (std::size_t at = label.find("\x94\xbc"); at != std::string::npos; at = label.find("\x94\xbc", at))
        {
            label.replace(at, 2, " ");
        }

        songs.push_back({xsflib::LoadBe32(entry.data()), std::move(label), true});
    }

    return songs;
}

// The effects that a game's play function starts by number (GameProfile::effect_songs), after the `songs` that its
// code names: the songs that the game plays as effects, then MusyX's effects. Each is named after its number, such as
// "se-0013". A song's ID is as in NamedSongs. An effect's ID is the profile's `musyx_effect_bit` and the number. A
// number that a sound takes already, or a song's number that an effect takes, isn't listed again. Throws
// xsflib::FormatError if a table lies outside the game's code.
std::vector<Sound> PlayedEffects(const ntdlib::Dol& dol, const GameProfile& profile, const std::vector<Sound>& songs)
{
    constexpr uint32_t kSongEntry = 8, kEffectEntry = 6;
    const auto entry = [&dol](uint32_t address, uint32_t size)
    {
        const std::span<const uint8_t> bytes = DolBytes(dol, address);
        if (bytes.size() < size)
        {
            throw xsflib::FormatError("main.dol's table of effects ends early");
        }

        return bytes;
    };

    std::set<uint32_t> listed;
    for (const Sound& song : songs)
    {
        listed.insert(song.id & 0xffff);
    }

    std::vector<uint32_t> effects;
    for (uint32_t i = 0; i < profile.play_effect_count; i++)
    {
        effects.push_back(xsflib::LoadBe16(entry(profile.play_effects + i * kEffectEntry, kEffectEntry).data()));
    }

    std::vector<Sound> sounds;
    for (uint32_t i = 0; i < profile.effect_song_count; i++)
    {
        const std::span<const uint8_t> bytes = entry(profile.effect_songs + i * kSongEntry, kSongEntry);
        const uint32_t number = xsflib::LoadBe16(bytes.data() + 2);
        if (std::find(effects.begin(), effects.end(), number) == effects.end() && listed.insert(number).second)
        {
            sounds.push_back({uint32_t{bytes[0]} << 24 | number, "se-" + ZeroPadded(number, 4), false});
        }
    }

    for (const uint32_t number : effects)
    {
        if (listed.insert(number).second)
        {
            sounds.push_back({profile.musyx_effect_bit | number, "se-" + ZeroPadded(number, 4), false});
        }
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
        sounds.insert(sounds.end(), effects.begin(), effects.end());
        return sounds;
    }

    std::vector<Sound> songs;
    for (SongEntry& song : SongEntries(dol, profile))
    {
        std::string label = std::move(song.name);
        if (label.size() > 4 && xsflib::AsciiLower(label.substr(label.size() - 4)) == ".sng")
        {
            label.resize(label.size() - 4);
        }

        songs.push_back({song.table << 24 | song.number, std::move(label), true});
    }

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
        sounds.push_back({static_cast<uint32_t>(i), entry.label, song || entry.music, stream});
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
                                         const GameProfile& profile)
{
    std::vector<std::string> paths(profile.bank_files.begin(), profile.bank_files.end());
    try
    {
        const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
        for (const SongEntry& song : SongEntries(dol, profile))
        {
            paths.push_back(SongPath(song.file_number));
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return e.what();
    }

    return AddDiscFiles(program, game, paths);
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
                                        uint32_t id)
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
    return MeasureSound(song);
}

} // namespace gcsf::rip
