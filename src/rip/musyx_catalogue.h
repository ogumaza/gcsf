// SPDX-License-Identifier: MIT

// The catalogue of a MusyX game's sounds for driver/musyx.c (docs/gcsf.md, "MusyX catalogue"). The ripper gathers each
// sound's song and groups from the game's files. The library holds them as files that the driver reads. The length
// analysis reads how a song's tracks loop from its file.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ntdlib/disc/game.h"

#include "rip/rip.h"

namespace gcsf::rip
{

struct MusyXCatalogue
{
    // MusyX's four files for one or more groups: the project, the pool, the sample directory and the samples.
    struct FileSet
    {
        std::vector<uint8_t> project, pool, sdir, samples;
    };

    // A group that a sound pushes: its MusyX group ID and the file set that holds it.
    struct Group
    {
        uint16_t id = 0;
        uint16_t set = 0;
    };

    // A word that the driver writes into the game's memory before it sets up the aux effects.
    struct Write
    {
        uint32_t address = 0;
        uint32_t value = 0;
    };

    // A MIDI controller that the driver sets on an effect once the effect starts: through sndFXCtrl, or with `fine`
    // through sndFXCtrl14. That one takes a 14-bit value, such as the pitch wheel's.
    struct Control
    {
        uint8_t controller = 0;
        bool fine = false;
        uint16_t value = 0;
    };

    enum class Kind : uint8_t
    {
        kSong = 0,
        kEffect = 1,
    };

    // A sound: its label, what MusyX plays, the groups that it pushes first, its words, and an effect's controllers.
    // The rip lists a song as music, and an effect whose `music` is set. The game plays such an effect as its music.
    struct Entry
    {
        std::string label;
        Kind kind = Kind::kSong;
        uint8_t volume = kDefaultVolume;
        uint8_t pan = kDefaultPan;
        uint8_t studio = 0;
        uint16_t id = 0;         // MusyX's song ID or effect ID
        uint16_t song_group = 0; // a song's group, which sndSeqPlayEx takes
        uint16_t song = kNoSong; // the song's file in `songs`
        std::vector<Group> groups;
        std::vector<Write> writes;
        std::vector<Control> controls;
        bool music = false;
    };

    // The volume and the pan that leave a sound at MusyX's defaults, and a song number that means no song.
    static constexpr uint8_t kDefaultVolume = 0xff;
    static constexpr uint8_t kDefaultPan = 0xff;
    static constexpr uint16_t kNoSong = 0xffff;

    std::vector<FileSet> sets;
    std::vector<std::vector<uint8_t>> songs;
    std::vector<Entry> entries;
};

// The catalogue of the game's sounds, from its files as the profile's `musyx_files` says. Throws xsflib::FormatError if
// a file is missing or damaged.
MusyXCatalogue GatherMusyX(const disc::GameFiles& game, const GameProfile& profile);

// The contents of a file that Yay0 packs, such as a module of Nintendo Puzzle Collection's code. The header holds
// "Yay0", the unpacked size, the offset of the table of copies and the offset of the table of bytes, as big-endian
// words. Mask words follow the header. Each bit of a mask, from the top, gives the next step. A set bit takes the next
// byte of the table of bytes. A clear bit copies earlier output. The copy takes the next big-endian half of the table
// of copies: the distance back, less 1, in its low 12 bits, and the length, less 2, in its top 4. A length of 0 there
// takes the length, less 18, from the table of bytes. Throws xsflib::FormatError if the file is damaged, or if it
// unpacks to more than 64 MiB.
std::vector<uint8_t> UnpackYay0(std::span<const uint8_t> file);

// An effect of a project's effects group: the group's ID, the effect's ID and the volume of the effect's row.
struct ProjectEffect
{
    uint16_t group = 0;
    uint16_t id = 0;
    uint8_t volume = 0;
};

// The effects of the effects groups in `project`, a MusyX project file, in the file's order. A group's header starts
// with the offset of the next group (0xFFFFFFFF ends the list), then the group's ID and its type (1 for effects) in
// big-endian halves. An effects group's header gives the offset of its table of effects at 0x1C. The table holds the
// number of effects in a big-endian half, two more bytes, and 10 bytes for each effect, starting with the effect's ID
// in a big-endian half. The byte at +4 of an effect's row limits the voices that play the effect at once. MusyX never
// starts an effect whose limit is 0. The list leaves such an effect out. The byte at +6 is the effect's volume. Every
// offset counts from the start of the file. Throws xsflib::FormatError if the file is damaged.
std::vector<ProjectEffect> ProjectEffects(std::span<const uint8_t> project);

// The catalogue's path in the library.
inline constexpr char kMusyXCatalogue[] = "MusyX/sounds.bin";

// The catalogue's file, which the library holds at kMusyXCatalogue. The driver reads a sound's entry from it.
std::vector<uint8_t> CatalogueFile(const MusyXCatalogue& catalogue);

// What the length analysis reads of a sound in the catalogue's file: its kind, and the number of its song's file
// (MusyXPath).
struct CatalogueSound
{
    MusyXCatalogue::Kind kind = MusyXCatalogue::Kind::kSong;
    uint16_t song = MusyXCatalogue::kNoSong;
};

// Sound `number` of a catalogue's file (CatalogueFile), or nothing if the file has no such sound.
std::optional<CatalogueSound> ReadCatalogueSound(std::span<const uint8_t> file, uint32_t number);

// The file sets and the songs of a catalogue that some sounds use, by their numbers in `sets` and `songs`.
struct MusyXFilesInUse
{
    std::vector<bool> sets;
    std::vector<bool> songs;
};

// The file sets and the songs that the sounds numbered in `sounds` use: the sets of their groups and their songs'
// files. The driver reads no other set or song for them. A number outside the catalogue uses none.
MusyXFilesInUse FilesInUse(const MusyXCatalogue& catalogue, std::span<const uint32_t> sounds);

// The path of a file set's file or a song's file in the library: "MusyX/", the number, and the extension (".proj",
// ".pool", ".sdir", ".samp" or ".sng").
std::string MusyXPath(uint32_t number, const char* extension);

// How the tracks of a MusyX song loop. A song whose info word, at 0x10 in its SNG file, has bit 31 set puts each of its
// 64 tracks in one of 16 sections. The word at 0x54 gives the offset of the table of the tracks' sections. Each section
// has a loop point and loops apart from the others. Any other song keeps every track in section 0. A section loops when
// one of its tracks ends in a loop entry: a region whose pattern is 0xFFFE. The defaults keep every track in section 0,
// and section 0 loops.
struct MusyXSections
{
    static constexpr std::size_t kTracks = 64;
    static constexpr std::size_t kSections = 16;

    std::array<uint8_t, kTracks> of_track = {}; // each track's section
    uint16_t looping = 1;                       // a bit for each section that loops
};

// The sections of the song in `song`, an SNG file. A track whose regions run past the end of the file, or whose section
// is 16 or more, doesn't loop.
MusyXSections SectionsOf(std::span<const uint8_t> song);

} // namespace gcsf::rip
