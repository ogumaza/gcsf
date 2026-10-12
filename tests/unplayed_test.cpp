// SPDX-License-Identifier: MIT

// Tests of the sequences that no sound plays (rip::UnplayedSequences), with made-up start-up archives of both kinds
// (BAA and AAF) and a made-up sequence archive.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ntdlib/jaudio/archive.h"
#include "ntdlib/testing/jaudio_files.h"
#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

#include "rip/rip.h"

using namespace gcsf;
namespace files = ntdlib::testing::jaudio_files;
using xsflib::testing::Check;
using xsflib::testing::ErrorOf;

namespace
{

// The profile of a game whose start-up archive is a BAA.
const rip::GameProfile& Baa()
{
    return rip::KnownProfiles().front();
}

// A piece of music of the sound table: the file of the sequence archive that it plays, and its volume.
struct Music
{
    uint16_t file;
    uint8_t volume;
};

// A start-up archive whose sound table has one section, BGM, with one group of music of the given type. Type 0x11 has
// the earlier layout of a sound's entry, and type 0x60 the later one (see jaudio::LayoutOf).
std::vector<uint8_t> MakeSoundArchive(const std::vector<Music>& music, uint8_t type)
{
    std::vector<files::SoundSpec> sounds;
    for (std::size_t i = 0; i < music.size(); i++)
    {
        sounds.push_back({type, music[i].volume, music[i].file, "BGM_" + std::to_string(i)});
    }

    return files::StartUpArchive(files::BankV0File(0, 100), {}, files::SoundTable({{"BGM", {{"FIELD", sounds}}}}));
}

// A file of the sequence archive: its id, its name and its size.
struct File
{
    uint16_t id;
    std::string name;
    std::size_t size;
};

std::vector<uint8_t> MakeSequenceArchive(const std::vector<File>& list)
{
    std::vector<files::RarcFileSpec> specs;
    for (const File& file : list)
    {
        specs.push_back({file.id, file.name, std::vector<uint8_t>(file.size, 0xc1)});
    }

    return files::RarcFile(specs);
}

// The data as Yaz0 with every byte copied, eight to a group.
std::vector<uint8_t> Yaz0Bytes(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> b = {'Y', 'a', 'z', '0', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    xsflib::StoreBe32(&b[4], static_cast<uint32_t>(data.size()));
    for (std::size_t i = 0; i < data.size(); i++)
    {
        if (i % 8 == 0)
        {
            b.push_back(0xff);
        }

        b.push_back(data[i]);
    }

    return b;
}

void TestUnplayed(uint8_t type)
{
    // Sounds play files 0, 2 and 3. Two of the three play at volume 55. Files 1 and 4 have no sound, and file 6 is
    // empty.
    const std::vector<uint8_t> baa = MakeSoundArchive({{0, 80}, {2, 55}, {3, 55}}, type);
    const std::vector<uint8_t> arc = MakeSequenceArchive({{0, "zero.bms", 4},
                                                          {1, "one.bms", 4},
                                                          {2, "two.bms", 4},
                                                          {3, "three.bms", 4},
                                                          {4, "four", 4},
                                                          {6, "x.bms", 0}});
    const std::vector<jaudio::SoundEntry> sounds = rip::ListSounds(Baa(), baa, arc);

    Check(sounds.size() == 3 && sounds[0].resource == 0 && sounds[2].resource == 3,
          "the table's sounds and their files");
    if (sounds.size() != 3)
    {
        return;
    }

    const std::vector<rip::UnplayedSequence> unplayed = rip::UnplayedSequences(Baa(), baa, sounds, arc);

    Check(unplayed.size() == 2, "two files of the archive have no sound");
    if (unplayed.size() != 2)
    {
        return;
    }

    Check(unplayed[0].sound.name == "one" && unplayed[0].file_name == "one.bms", "a file named without its extension");
    Check(unplayed[1].sound.name == "four" && unplayed[1].file_name == "four", "a file without an extension");

    // Each plays as the first sound, at the volume that most sounds have, with the file's id in its entry. Nothing
    // else in the copy changes.
    const uint32_t entry = sounds[0].offset;
    const jaudio::ItemLayout layout = jaudio::LayoutOf(type);
    const uint32_t file = entry + layout.file.value_or(0);

    for (const rip::UnplayedSequence& u : unplayed)
    {
        Check(u.sound.Id() == sounds[0].Id() && rip::IsMusic(u.sound), u.file_name + ": plays as the first sound");
        const std::vector<jaudio::SoundEntry> changed = rip::ListSounds(Baa(), u.sound_archive, arc);
        Check(changed.size() == 3 && changed[0].volume == 55 && changed[0].resource == u.sound.resource &&
                  changed[1].volume == 55 && changed[1].resource == 2,
              u.file_name + ": the copy's first sound plays the file at volume 55");

        bool elsewhere = u.sound_archive.size() != baa.size();
        for (std::size_t i = 0; i < baa.size() && i < u.sound_archive.size(); i++)
        {
            elsewhere =
                elsewhere || (baa[i] != u.sound_archive[i] && i != entry + layout.volume && i != file && i != file + 1);
        }

        Check(!elsewhere, u.file_name + ": the copy changes only the entry's volume and file");
    }

    // A sequence archive compressed with Yaz0 gives the same files.
    const std::vector<rip::UnplayedSequence> compressed = rip::UnplayedSequences(Baa(), baa, sounds, Yaz0Bytes(arc));

    Check(compressed.size() == 2 && compressed[1].file_name == "four" &&
              compressed[1].sound_archive == unplayed[1].sound_archive,
          "a sequence archive compressed with Yaz0");

    // A table whose sounds play no file of the archive offers nothing.
    Check(rip::UnplayedSequences(Baa(), baa, {}, arc).empty(), "no sound plays a file of the archive");

    // A sequence archive that isn't one is refused.
    const std::vector<uint8_t> cut(arc.begin(), arc.begin() + 16);

    Check(!ErrorOf([&] { rip::UnplayedSequences(Baa(), baa, sounds, cut); }).empty(),
          "a damaged sequence archive is refused");
}

void TestUnplayedAaf()
{
    // A JAudio1 table: the effects sequence plays file 7, and music plays files 3 and 9, two of three at volume 40.
    // File 5 has no sound. The effects sequence's file isn't unplayed.
    const auto jaudio1 =
        std::find_if(rip::KnownProfiles().begin(), rip::KnownProfiles().end(),
                     [](const rip::GameProfile& p) { return p.library == rip::SoundLibrary::kJAudio1; });
    Check(jaudio1 != rip::KnownProfiles().end(), "a profile for a JAudio1 game");
    if (jaudio1 == rip::KnownProfiles().end())
    {
        return;
    }

    const std::vector<uint8_t> aaf = files::AafFile({{16, 127, 7}, {16, 90, 3}, {16, 40, 9}, {16, 40, 9}, {0, 80}}, {});
    const std::vector<uint8_t> arc =
        MakeSequenceArchive({{3, "field.bms", 4}, {5, "cave.bms", 4}, {7, "se.bms", 4}, {9, "boss.bms", 4}});
    const std::vector<jaudio::SoundEntry> sounds = rip::ListSounds(*jaudio1, aaf, arc);
    const std::vector<rip::UnplayedSequence> unplayed = rip::UnplayedSequences(*jaudio1, aaf, sounds, arc);

    Check(unplayed.size() == 1, "one file of the archive has no sound");
    if (unplayed.size() != 1)
    {
        return;
    }

    // It plays as the first piece of music, at the volume that most music has.
    const rip::UnplayedSequence& u = unplayed[0];
    Check(u.sound.name == "cave" && u.file_name == "cave.bms" && u.sound.Id() == 0x80000001,
          "the unplayed file plays as the first piece of music");

    const std::vector<jaudio::SoundEntry> patched = rip::ListSounds(*jaudio1, u.sound_archive, arc);
    const auto music =
        std::find_if(patched.begin(), patched.end(), [](const jaudio::SoundEntry& s) { return s.Id() == 0x80000001; });

    Check(music != patched.end() && music->resource == 5 && music->volume == 40 && u.sound_archive.size() == aaf.size(),
          "the start-up archive plays the file at the volume that most music has");
}

} // namespace

int main()
{
    TestUnplayed(0x11);
    TestUnplayed(0x60);
    TestUnplayedAaf();

    return xsflib::testing::Summary("unplayed");
}
