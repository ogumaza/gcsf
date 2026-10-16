// SPDX-License-Identifier: MIT

// The catalogue of a MusyX game's sounds for driver/musyx.c (docs/gcsf.md, "MusyX catalogue"). The ripper gathers each
// sound's song and groups from the game's files. The library holds them as files that the driver reads. The length
// analysis reads how a song's tracks loop from its file.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

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

// The catalogue's path in the library.
inline constexpr char kMusyXCatalogue[] = "MusyX/sounds.bin";

// The catalogue's file, which the library holds at kMusyXCatalogue. The driver reads a sound's entry from it.
std::vector<uint8_t> CatalogueFile(const MusyXCatalogue& catalogue);

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
