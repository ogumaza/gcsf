// SPDX-License-Identifier: MIT

// Tests of how a MusyX song's tracks loop (rip::SectionsOf), with made-up SNG files: songs without sections, songs in
// sections, and damaged songs. Then a test of the catalogue's file (rip::CatalogueFile) with an effect's controllers.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

#include "rip/musyx_catalogue.h"

using namespace gcsf;
using xsflib::testing::Check;

namespace
{

constexpr uint16_t kNoTrack = 0, kEnd = 0xffff, kLoop = 0xfffe;
constexpr std::size_t kHeader = 0x58, kTrackTable = kHeader, kSectionTable = kTrackTable + 64 * 4;
constexpr std::size_t kRegions = kSectionTable + 64, kEntry = 12;

// A made-up song. `last[k]` is the last entry of track k: kEnd, kLoop, or kNoTrack for a track that the song lacks.
// Each track has one region of pattern 0 before it. The song puts its tracks in `sections` if that isn't empty.
std::vector<uint8_t> MakeSong(const std::vector<uint16_t>& last, const std::vector<uint8_t>& sections)
{
    std::vector<uint8_t> song(kRegions + last.size() * 2 * kEntry);
    xsflib::StoreBe32(song.data(), static_cast<uint32_t>(kTrackTable));
    xsflib::StoreBe32(song.data() + 0x10, (sections.empty() ? 0 : 0x80000000u) | 120);
    xsflib::StoreBe32(song.data() + 0x54, static_cast<uint32_t>(kSectionTable));
    for (std::size_t k = 0; k < sections.size(); k++)
    {
        song[kSectionTable + k] = sections[k];
    }

    for (std::size_t k = 0; k < last.size(); k++)
    {
        if (last[k] == kNoTrack)
        {
            continue;
        }

        const std::size_t regions = kRegions + k * 2 * kEntry;
        xsflib::StoreBe32(song.data() + kTrackTable + 4 * k, static_cast<uint32_t>(regions));
        xsflib::StoreBe32(song.data() + regions + kEntry, 3072);
        xsflib::StoreBe16(song.data() + regions + kEntry + 8, last[k]);
    }

    return song;
}

void TestWithoutSections()
{
    const rip::MusyXSections looping = rip::SectionsOf(MakeSong({kNoTrack, kLoop, kEnd, kLoop}, {}));
    bool all_in_0 = true;
    for (const uint8_t section : looping.of_track)
    {
        all_in_0 = all_in_0 && section == 0;
    }
    Check(all_in_0 && looping.looping == 1, "a song without sections keeps its tracks in section 0, which loops");

    const rip::MusyXSections ends = rip::SectionsOf(MakeSong({kEnd, kNoTrack, kEnd}, {}));
    Check(ends.looping == 0, "a song whose tracks all end doesn't loop");
}

void TestSections()
{
    const rip::MusyXSections sections =
        rip::SectionsOf(MakeSong({kLoop, kEnd, kLoop, kLoop, kEnd, kLoop}, {2, 3, 15, 16, 2, 2}));

    Check(sections.of_track[0] == 2 && sections.of_track[1] == 3 && sections.of_track[2] == 15,
          "each track has the section that the table gives it");
    Check(sections.looping == (1u << 2 | 1u << 15), "a section loops when one of its tracks loops");
}

void TestDamaged()
{
    std::vector<uint8_t> cut = MakeSong({kLoop, kLoop}, {1, 4});
    cut.resize(cut.size() - kEntry);
    Check(rip::SectionsOf(cut).looping == 1u << 1, "a track whose regions run past the end of the file doesn't loop");

    const std::vector<uint8_t> header(0x10);
    Check(rip::SectionsOf(header).looping == 0, "a file shorter than a header has no loops");

    std::vector<uint8_t> no_table = MakeSong({kLoop}, {5});
    xsflib::StoreBe32(no_table.data() + 0x54, static_cast<uint32_t>(no_table.size()));
    Check(rip::SectionsOf(no_table).looping == 1, "without the table of sections, every track is in section 0");
}

// The catalogue's file (rip::CatalogueFile) for a song without controllers and an effect with a word and two
// controllers. The layout is the one in docs/gcsf.md, "MusyX catalogue": a header of 32 bytes, 20 bytes for each sound,
// 4 for each group entry and 8 for each word. An effect's controllers follow its words. Bit 8 of a controller's first
// value picks sndFXCtrl14.
void TestControllers()
{
    rip::MusyXCatalogue catalogue;
    rip::MusyXCatalogue::Entry song;
    song.id = 3;
    song.song = 0;
    song.groups = {{1, 0}};
    catalogue.entries.push_back(song);

    rip::MusyXCatalogue::Entry effect;
    effect.kind = rip::MusyXCatalogue::Kind::kEffect;
    effect.volume = 0x7f;
    effect.pan = 0x40;
    effect.id = 0x26d;
    effect.groups = {{5, 0}};
    effect.writes = {{0x803e17ec, 10}};
    effect.controls = {{.controller = 7, .value = 90}, {.controller = 0x80, .fine = true, .value = 0x2aaa}};
    catalogue.entries.push_back(effect);

    const std::vector<uint8_t> file = rip::CatalogueFile(catalogue);
    Check(file.size() == 32 + 2 * 20 + 2 * 4 + 3 * 8, "the file holds two sounds, two group entries and three words");
    if (file.size() != 32 + 2 * 20 + 2 * 4 + 3 * 8)
    {
        return;
    }

    const uint8_t* sounds = file.data() + 32;
    Check(xsflib::LoadBe16(sounds + 0x12) == 0, "a sound without controllers has 0 at 0x12");
    Check(xsflib::LoadBe32(file.data() + 0x18) == 3, "the header counts the controllers among the words");
    const uint8_t* entry = sounds + 20;
    Check(entry[0] == 1 && entry[1] == 0x7f && entry[2] == 0x40 && xsflib::LoadBe16(entry + 4) == 0x26d,
          "the effect's kind, volume, pan and ID");
    Check(xsflib::LoadBe16(entry + 0x0e) == 0 && xsflib::LoadBe16(entry + 0x10) == 1 &&
              xsflib::LoadBe16(entry + 0x12) == 2,
          "the effect's first word, its number of words and its number of controllers");
    const uint8_t* words = file.data() + xsflib::LoadBe32(file.data() + 0x1c);
    Check(xsflib::LoadBe32(words) == 0x803e17ec && xsflib::LoadBe32(words + 4) == 10, "the word comes first");
    Check(xsflib::LoadBe32(words + 8) == 7 && xsflib::LoadBe32(words + 12) == 90, "then controller 7 and its value");
    Check(xsflib::LoadBe32(words + 16) == 0x180 && xsflib::LoadBe32(words + 20) == 0x2aaa,
          "then the pitch wheel, with bit 8 for sndFXCtrl14");
}

} // namespace

int main()
{
    TestWithoutSections();
    TestSections();
    TestDamaged();
    TestControllers();

    return xsflib::testing::Summary("musyx");
}
