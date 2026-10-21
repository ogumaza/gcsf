// SPDX-License-Identifier: MIT

// Unit tests for rip::UseSoundFiles: where it finds another release's sound files, and which of the game's files it
// then reads from them, The Wind Waker HD's sequence table among them. A profile may have no sequence archive. The
// folders are made up in a temporary folder.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/jaudio/rarc.h"
#include "ntdlib/testing/jaudio_files.h"
#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/temp_folder.h"

#include "rip/rip.h"

using namespace gcsf;
namespace files = ntdlib::testing::jaudio_files;

using xsflib::testing::Check;
namespace fs = std::filesystem;

namespace
{

void WriteText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
}

// A game whose disc holds every file. Each file holds "disc:" and its path.
disc::GameFiles DiscGame()
{
    disc::GameFiles game;
    game.read_file = [](const std::string& path, std::vector<uint8_t>& data)
    {
        const std::string text = "disc:" + path;
        data.assign(text.begin(), text.end());

        return true;
    };

    return game;
}

// What `game` reads at `path`, or "none".
std::string Read(const disc::GameFiles& game, const std::string& path)
{
    std::vector<uint8_t> data;
    return game.read_file(path, data) ? std::string(data.begin(), data.end()) : "none";
}

rip::GameProfile Profile()
{
    rip::GameProfile profile{};
    profile.sound_archive = "Audiores/Z2Sound.baa";
    profile.sequence_archive = "Audiores/Seqs/Z2SoundSeqs.arc";
    profile.wave_folder = "Audiores/Waves/";

    return profile;
}

// A title's folder: the start-up archive deep inside it, next to the other sound files.
void TestTitle(const fs::path& dir)
{
    const fs::path title = dir / "title";
    const fs::path sounds = title / "content" / "Cafe" / "US" / "AudioRes" / "JAudioRes";
    WriteText(sounds / "Z2Sound.baa", "baa");
    WriteText(sounds / "Seqs" / "Z2SoundSeqs.arc", "arc");
    WriteText(sounds / "Waves" / "A.aw", "a");
    WriteText(sounds / "Z2CSRes.arc", "other release");
    WriteText(title / "code" / "app.xml", "x");

    disc::GameFiles game = DiscGame();
    std::string error;
    const std::optional<fs::path> found = rip::UseSoundFiles(game, Profile(), title, &error);

    Check(found && fs::equivalent(*found, sounds), "the title's sound folder: " + error);
    XSFLIB_CHECK(Read(game, "Audiores/Z2Sound.baa") == "baa");
    XSFLIB_CHECK(Read(game, "Audiores/Seqs/Z2SoundSeqs.arc") == "arc");
    XSFLIB_CHECK(Read(game, "Audiores/Waves/A.aw") == "a");

    // A wave archive that the folder lacks belongs to the other release. The disc's isn't read in its place.
    XSFLIB_CHECK(Read(game, "Audiores/Waves/B.aw") == "none");

    // The game's other files still come from the disc, those next to the sound files included.
    XSFLIB_CHECK(Read(game, "Audiores/Z2CSRes.arc") == "disc:Audiores/Z2CSRes.arc");
    XSFLIB_CHECK(Read(game, "Audiores/Seqs/Other.arc") == "disc:Audiores/Seqs/Other.arc");
    XSFLIB_CHECK(Read(game, "res/Object/Alink.arc") == "disc:res/Object/Alink.arc");

    // The sound folder itself.
    disc::GameFiles direct = DiscGame();
    const std::optional<fs::path> same = rip::UseSoundFiles(direct, Profile(), sounds, &error);

    Check(same && fs::equivalent(*same, sounds), "the sound folder itself: " + error);
    XSFLIB_CHECK(Read(direct, "Audiores/Seqs/Z2SoundSeqs.arc") == "arc");
}

// An extracted disc, with its files in a DATA folder as a Wii disc's extraction has them. A folder that has the
// start-up archive's name isn't the archive.
void TestExtractedDisc(const fs::path& dir)
{
    const fs::path disc = dir / "disc";
    WriteText(disc / "DATA" / "sys" / "main.dol", "dol");
    WriteText(disc / "DATA" / "files" / "Audiores" / "Z2Sound.baa", "wii baa");
    fs::create_directories(disc / "DATA" / "files" / "Z2Sound.baa");

    disc::GameFiles game = DiscGame();
    std::string error;
    const std::optional<fs::path> found = rip::UseSoundFiles(game, Profile(), disc, &error);

    Check(found && fs::equivalent(*found, disc / "DATA" / "files" / "Audiores"), "the disc's sound folder: " + error);
    XSFLIB_CHECK(Read(game, "Audiores/Z2Sound.baa") == "wii baa");
}

// A profile without a sequence archive (Mario Kart: Double Dash!!). The folder's start-up archive and wave archives
// replace the game's. The game's other files, those of the sequence archive's usual place among them, stay the game's.
void TestNoSequenceArchive(const fs::path& dir)
{
    const fs::path sounds = dir / "blocks" / "AudioRes";
    WriteText(sounds / "GCKart.baa", "baa");
    WriteText(sounds / "Waves" / "A.aw", "a");
    WriteText(sounds / "Seqs" / "JaiSeqs.arc", "folder's arc");

    rip::GameProfile profile{};
    profile.sound_archive = "AudioRes/GCKart.baa";
    profile.wave_folder = "AudioRes/Waves/";

    disc::GameFiles game = DiscGame();
    std::string error;
    const std::optional<fs::path> found = rip::UseSoundFiles(game, profile, dir / "blocks", &error);

    Check(found && fs::equivalent(*found, sounds), "the sound folder without a sequence archive: " + error);
    XSFLIB_CHECK(Read(game, "AudioRes/GCKart.baa") == "baa");
    XSFLIB_CHECK(Read(game, "AudioRes/Waves/A.aw") == "a");
    XSFLIB_CHECK(Read(game, "AudioRes/Seqs/JaiSeqs.arc") == "disc:AudioRes/Seqs/JaiSeqs.arc");
}

void TestErrors(const fs::path& dir)
{
    disc::GameFiles game = DiscGame();
    std::string error;

    // No start-up archive. The game still reads everything from the disc.
    WriteText(dir / "empty" / "readme.txt", "x");

    XSFLIB_CHECK(!rip::UseSoundFiles(game, Profile(), dir / "empty", &error));
    Check(error.find("holds no Z2Sound.baa") != std::string::npos, "no start-up archive: " + error);
    XSFLIB_CHECK(Read(game, "Audiores/Z2Sound.baa") == "disc:Audiores/Z2Sound.baa");

    // Two start-up archives, such as two regions' files in one title.
    WriteText(dir / "two" / "EU" / "Z2Sound.baa", "eu");
    WriteText(dir / "two" / "US" / "Z2Sound.baa", "us");

    XSFLIB_CHECK(!rip::UseSoundFiles(game, Profile(), dir / "two", &error));
    Check(error.find("more than one Z2Sound.baa") != std::string::npos, "two start-up archives: " + error);

    // A folder that doesn't exist.
    error.clear();

    XSFLIB_CHECK(!rip::UseSoundFiles(game, Profile(), dir / "missing", &error));
    XSFLIB_CHECK(!error.empty());
}

// The disc's sequence archive in the tests of a sequence table.
std::vector<uint8_t> SequenceArchive()
{
    const auto bytes = [](const std::string& text)
    {
        return std::vector<uint8_t>(text.begin(), text.end());
    };

    return files::RarcFile(
        {{2, "se.bms", bytes("old se")}, {0, "field.bms", bytes("old field")}, {1, "b.bms", bytes("b")}});
}

// A sequence table as The Wind Waker HD has one: the count, zeros up to 0x20, then each sequence's offset and size.
std::vector<uint8_t> SequenceTable(const std::vector<std::string>& sequences)
{
    std::vector<uint8_t> table(0x20 + sequences.size() * 8);
    xsflib::StoreBe32(table.data(), static_cast<uint32_t>(sequences.size()));
    for (std::size_t i = 0; i < sequences.size(); i++)
    {
        xsflib::StoreBe32(table.data() + 0x20 + i * 8, static_cast<uint32_t>(table.size()));
        xsflib::StoreBe32(table.data() + 0x24 + i * 8, static_cast<uint32_t>(sequences[i].size()));
        table.insert(table.end(), sequences[i].begin(), sequences[i].end());
    }

    return table;
}

rip::GameProfile WindWakerProfile()
{
    rip::GameProfile profile{};
    profile.sound_archive = "Audiores/JaiInit.aaf";
    profile.sequence_archive = "Audiores/Seqs/JaiSeqs.arc";
    profile.wave_folder = "Audiores/Banks/";

    return profile;
}

// A game whose disc holds a sequence archive, and "disc:" and its path in each other file.
disc::GameFiles WindWakerGame()
{
    disc::GameFiles game;
    game.read_file = [](const std::string& path, std::vector<uint8_t>& data)
    {
        if (path == "Audiores/Seqs/JaiSeqs.arc")
        {
            data = SequenceArchive();
            return true;
        }

        const std::string text = "disc:" + path;
        data.assign(text.begin(), text.end());

        return true;
    };

    return game;
}

// The Wind Waker HD keeps its sequences in a table, `Seqs/JaiSeq.bdt`, and has no sequence archive. The sequence
// archive is then the disc's, with the table's sequences.
void TestSequenceTable(const fs::path& dir)
{
    const std::vector<uint8_t> table = SequenceTable({"the field sequence from the table", "boss", "se"});
    const fs::path sounds = dir / "hd" / "content" / "Cafe" / "US" / "AudioRes" / "JAudioRes";
    WriteText(sounds / "JaiInit.aaf", "aaf");
    WriteText(sounds / "Seqs" / "JaiSeq.bdt", std::string(table.begin(), table.end()));
    WriteText(sounds / "Banks" / "A.aw", "a");

    disc::GameFiles game = WindWakerGame();
    std::string error;
    std::vector<uint8_t> archive;

    Check(rip::UseSoundFiles(game, WindWakerProfile(), dir / "hd", &error).has_value(), "the table's title: " + error);
    XSFLIB_CHECK(game.read_file("Audiores/Seqs/JaiSeqs.arc", archive));
    XSFLIB_CHECK(archive == jaudio::RepackSequenceTable(SequenceArchive(), table));
    XSFLIB_CHECK(Read(game, "Audiores/JaiInit.aaf") == "aaf" && Read(game, "Audiores/Banks/A.aw") == "a");

    // macOS writes an AppleDouble file beside each file that it copies to a FAT or exFAT drive. Those don't count.
    const fs::path drive = dir / "drive";
    WriteText(drive / "JaiInit.aaf", "aaf");
    WriteText(drive / "._JaiInit.aaf", "apple double");
    WriteText(drive / "Seqs" / "JaiSeq.bdt", std::string(table.begin(), table.end()));
    WriteText(drive / "Seqs" / "._JaiSeq.bdt", "apple double");
    disc::GameFiles copied = WindWakerGame();

    Check(rip::UseSoundFiles(copied, WindWakerProfile(), drive, &error).has_value(), "the copied title: " + error);
    XSFLIB_CHECK(copied.read_file("Audiores/Seqs/JaiSeqs.arc", archive));
    XSFLIB_CHECK(archive == jaudio::RepackSequenceTable(SequenceArchive(), table));

    // With no table, or with two, the folder has no sequence archive.
    WriteText(dir / "none" / "JaiInit.aaf", "aaf");
    fs::create_directories(dir / "none" / "Seqs");
    disc::GameFiles no_table = WindWakerGame();

    XSFLIB_CHECK(rip::UseSoundFiles(no_table, WindWakerProfile(), dir / "none", &error).has_value());
    XSFLIB_CHECK(Read(no_table, "Audiores/Seqs/JaiSeqs.arc") == "none");

    WriteText(dir / "two_tables" / "JaiInit.aaf", "aaf");
    WriteText(dir / "two_tables" / "Seqs" / "JaiSeq.bdt", std::string(table.begin(), table.end()));
    WriteText(dir / "two_tables" / "Seqs" / "Other.bdt", std::string(table.begin(), table.end()));
    disc::GameFiles two = WindWakerGame();

    XSFLIB_CHECK(rip::UseSoundFiles(two, WindWakerProfile(), dir / "two_tables", &error).has_value());
    XSFLIB_CHECK(Read(two, "Audiores/Seqs/JaiSeqs.arc") == "none");
}

} // namespace

int main()
{
    const xsflib::testing::TempFolder temp("gcsf_sound_files_test");
    const fs::path& dir = temp.Path();
    TestTitle(dir);
    TestExtractedDisc(dir);
    TestNoSequenceArchive(dir);
    TestErrors(dir);
    TestSequenceTable(dir);

    return xsflib::testing::Summary("sound_files");
}
