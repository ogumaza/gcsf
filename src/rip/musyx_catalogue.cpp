// SPDX-License-Identifier: MIT

#include "rip/musyx_catalogue.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/musyx/catalogue.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"

#include "gcsf/format.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{
namespace
{

using ntdlib::musyx::CatalogueLayout;

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

// The address of driver/musyx.c's parameter `index` in the driver's block (see kMusyXAuxCallbacks). A sound's words can
// change the parameter there.
uint32_t MusyXParameter(uint32_t index)
{
    return DriverBlock::kAddress + DriverBlock::kProfileOffset + index * 4;
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

// The words that make the driver set every group's volume to `volume` after the start-up's sndVolume(127, 0, 0xFF)
// (driver/musyx.c's group_volume). The start-ups of Nintendo Puzzle Collection's menu and Dr. Mario make such a call.
MusyXCatalogue::Write PuzzleGroupVolume(uint8_t volume)
{
    return {MusyXParameter(kMusyXGroupVolume), volume};
}

// The words that make the driver call sndMasterVolume(volume, 0, music, fx) through the profile's sndMasterVolume.
// Yoshi's Cookie's start-up makes such calls for the levels of its options.
std::vector<MusyXCatalogue::Write> PuzzleMasterVolume(uint8_t volume, bool music, bool fx)
{
    return {{MusyXParameter(kMusyXMasterVolume + 1), volume},
            {MusyXParameter(kMusyXMasterVolume + 2), music ? 1u : 0u},
            {MusyXParameter(kMusyXMasterVolume + 3), fx ? 1u : 0u}};
}

// The words that set up studio 0's aux effects as a module of Nintendo Puzzle Collection does (ntdlib::musyx::Module).
// The profile's `params` give aux A MusyX's high-quality reverb and aux B MusyX's chorus. The words write the settings
// of `reverb`. They write the settings of `chorus` into aux B's block. A standard reverb on aux A takes the callback
// and the preparation in the profile's `standard_reverb`. Without a chorus, aux B has no effect. The words for a
// standard reverb and for no chorus change the driver's parameters (kMusyXAuxCallbacks).
std::vector<MusyXCatalogue::Write> PuzzleAuxWrites(const GameProfile& profile, const MusyXReverb& reverb,
                                                   const std::optional<ntdlib::musyx::ChorusSettings>& chorus)
{
    constexpr uint32_t kChorusSettings = 0x90;
    std::vector<MusyXCatalogue::Write> writes;
    AddReverb(writes, reverb);
    if (!reverb.high)
    {
        writes.push_back({MusyXParameter(kMusyXAuxCallbacks), profile.standard_reverb[0]});
        writes.push_back({MusyXParameter(kMusyXAuxPreparations), profile.standard_reverb[1]});
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
        writes.push_back({MusyXParameter(kMusyXAuxCallbacks + 1), 0});
        writes.push_back({MusyXParameter(kMusyXAuxPreparations + 1), 0});
    }

    return writes;
}

// The layout of the catalogue of a game whose profile gives `files`, or nothing for a game without a catalogue.
std::optional<CatalogueLayout> LayoutOf(MusyXFiles files)
{
    switch (files)
    {
    case MusyXFiles::kRetro:
        return CatalogueLayout::kRetro;
    case MusyXFiles::kTales:
        return CatalogueLayout::kTales;
    case MusyXFiles::kStarFox:
        return CatalogueLayout::kStarFox;
    case MusyXFiles::kRogue:
        return CatalogueLayout::kRogueLeader;
    case MusyXFiles::kColosseum:
        return CatalogueLayout::kColosseum;
    case MusyXFiles::kResidentEvil:
        return CatalogueLayout::kResidentEvilZero;
    case MusyXFiles::kResidentEvilSnd:
        return CatalogueLayout::kResidentEvil;
    case MusyXFiles::kPaperMario:
        return CatalogueLayout::kPaperMario;
    case MusyXFiles::kPokemonChannel:
        return CatalogueLayout::kPokemonChannel;
    case MusyXFiles::kRebelStrike:
        return CatalogueLayout::kRebelStrike;
    case MusyXFiles::kPuzzleCollection:
        return CatalogueLayout::kPuzzleCollection;
    case MusyXFiles::kNdDemo:
        return CatalogueLayout::kNdDemo;
    case MusyXFiles::kNone:
        break;
    }

    return std::nullopt;
}

// The places of the game's tables that the catalogue reads, from the profile.
ntdlib::musyx::CatalogueTables TablesOf(const GameProfile& profile)
{
    return {.songs = profile.musyx_songs,
            .song_count = profile.musyx_song_count,
            .effects = profile.musyx_effects,
            .effect_count = profile.musyx_effect_count,
            .embedded_dol = profile.embedded_dol,
            .song_reverbs = profile.song_reverbs,
            .file_table = profile.musyx_file_table,
            .file_count = profile.musyx_file_count,
            .reverb_presets = profile.musyx_reverb_presets,
            .viewer_names = profile.viewer_names,
            .viewer_effects = profile.viewer_effects,
            .song_volumes = profile.musyx_song_volumes};
}

// The words that `sound` plays with in a game whose sounds share one set-up of the aux effects. Aux A takes the
// profile's reverbs[0], or `other_reverb` for a sound that the game gives the other reverb. A sound with settings for
// the reverb gives them to reverbs[0]. An effect adds the profile's `musyx_effect_writes`. A song whose fade-in differs
// from the profile's sets the driver's song_fade_in.
std::vector<MusyXCatalogue::Write> CatalogueWrites(const GameProfile& profile, const ntdlib::musyx::Sound& sound)
{
    MusyXReverb aux_a = sound.other_reverb ? profile.other_reverb : profile.reverbs[0];
    if (sound.reverb)
    {
        aux_a.coloration = sound.reverb->coloration;
        aux_a.mix = sound.reverb->mix;
        aux_a.time = sound.reverb->time;
        aux_a.damping = sound.reverb->damping;
        aux_a.pre_delay = sound.reverb->pre_delay;
        aux_a.crosstalk = sound.reverb->crosstalk;
    }

    std::vector<MusyXCatalogue::Write> writes = SoundWrites(profile, aux_a);
    if (sound.kind == ntdlib::musyx::SoundKind::kEffect)
    {
        for (const auto& [address, value] : profile.musyx_effect_writes)
        {
            writes.push_back({address, value});
        }
    }

    const uint32_t fade_in = kMusyXSongFadeIn < profile.params.size() ? profile.params[kMusyXSongFadeIn] : 0;
    if (sound.kind == ntdlib::musyx::SoundKind::kSong && sound.fade_in_ms != fade_in)
    {
        writes.push_back({MusyXParameter(kMusyXSongFadeIn), sound.fade_in_ms});
    }

    return writes;
}

// The words that `sound` of Nintendo Puzzle Collection plays with: the set-up of the aux effects of its module
// `module`, then the module's limits of MusyX's voices, then the volume that the module gives every group and the
// master volume that it gives the sound's kind. The profile's `params` give aux A the high-quality reverb of the
// profile's reverbs[0]. A module that gives aux A the standard reverb takes the profile's `other_reverb`. The words
// write the limits into MusyX's state, at the profile's `musyx_voice_limits`.
std::vector<MusyXCatalogue::Write> PuzzleWrites(const GameProfile& profile, const ntdlib::musyx::Module& module,
                                                const ntdlib::musyx::Sound& sound)
{
    std::vector<MusyXCatalogue::Write> writes =
        PuzzleAuxWrites(profile, module.standard_reverb ? profile.other_reverb : profile.reverbs[0], module.chorus);
    if (module.voices)
    {
        const uint32_t limits = profile.params.at(kMusyXVoices) << 24 | module.voices->music << 16 |
                                module.voices->effects << 8 | profile.params.at(kMusyXStudios);
        writes.push_back({profile.musyx_voice_limits, limits});
    }

    if (sound.group_volume)
    {
        writes.push_back(PuzzleGroupVolume(*sound.group_volume));
    }

    if (sound.master_volume)
    {
        const bool song = sound.kind == ntdlib::musyx::SoundKind::kSong;
        const std::vector<MusyXCatalogue::Write> master = PuzzleMasterVolume(*sound.master_volume, song, !song);
        writes.insert(writes.end(), master.begin(), master.end());
    }

    return writes;
}

// The catalogue's entry of `sound`, with the words `writes`. A sound without a volume or a pan takes MusyX's.
MusyXCatalogue::Entry EntryOf(ntdlib::musyx::Sound sound, std::vector<MusyXCatalogue::Write> writes)
{
    MusyXCatalogue::Entry entry;
    entry.label = std::move(sound.name);
    entry.kind =
        sound.kind == ntdlib::musyx::SoundKind::kEffect ? MusyXCatalogue::Kind::kEffect : MusyXCatalogue::Kind::kSong;
    entry.volume = sound.volume.value_or(MusyXCatalogue::kDefaultVolume);
    entry.pan = sound.pan.value_or(MusyXCatalogue::kDefaultPan);
    entry.id = sound.id;
    entry.song_group = sound.song_group;
    entry.song = sound.song == ntdlib::musyx::kNoSong ? MusyXCatalogue::kNoSong : static_cast<uint16_t>(sound.song);
    for (const ntdlib::musyx::PushedGroup& group : sound.groups)
    {
        entry.groups.push_back({.id = group.id, .set = static_cast<uint16_t>(group.set)});
    }

    entry.writes = std::move(writes);
    for (const ntdlib::musyx::Control& control : sound.controls)
    {
        entry.controls.push_back({.controller = control.controller,
                                  .fine = control.fine,
                                  .value = static_cast<uint16_t>(control.value),
                                  .channel = control.channel});
    }

    entry.music = sound.music;
    entry.sound_effect = sound.sound_effect;

    return entry;
}

} // namespace

MusyXCatalogue GatherMusyX(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::optional<CatalogueLayout> layout = LayoutOf(profile.musyx_files);
    if (!layout)
    {
        throw xsflib::FormatError(std::string(profile.name) + " has no catalogue of MusyX sounds");
    }

    ntdlib::musyx::Catalogue read = ntdlib::musyx::ReadCatalogue(*layout, game, TablesOf(profile));
    MusyXCatalogue catalogue;
    catalogue.sets = std::move(read.sets);
    catalogue.songs = std::move(read.songs);
    for (ntdlib::musyx::Sound& sound : read.sounds)
    {
        std::vector<MusyXCatalogue::Write> writes = *layout == CatalogueLayout::kPuzzleCollection
                                                        ? PuzzleWrites(profile, read.modules.at(sound.module), sound)
                                                        : CatalogueWrites(profile, sound);
        catalogue.entries.push_back(EntryOf(std::move(sound), std::move(writes)));
    }

    return catalogue;
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

        // The controllers follow the words. Bit 8 of the first value picks sndFXCtrl14. Bits 16 to 23 give a song's
        // MIDI channel.
        for (const MusyXCatalogue::Control& c : entry.controls)
        {
            xsflib::StoreBe32(p + writes_at + write * kWriteSize,
                              c.controller | (c.fine ? 0x100u : 0u) | static_cast<uint32_t>(c.channel) << 16);
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
