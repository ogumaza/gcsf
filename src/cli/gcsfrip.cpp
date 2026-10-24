// SPDX-License-Identifier: MIT

// gcsfrip: builds GCSF sets (.gcsflib + .minigcsf files) from GameCube games.
//
// usage: gcsfrip [options] <input> [<input> ...]
//   <input>          a GameCube disc image (ISO, NKit ISO, RVZ, WIA, GCZ, CISO or WDF) or an extracted disc
//   -o, --output DIR the folder for the rips (default: a folder next to each input, named after the input with "_gcsf"
//                    added)
//   --list           only list the game's sounds and whether it has a driver profile
//   --only REGEX     rip only the sounds whose label matches (ECMAScript regex)
//   --bgm            rip only the music: the sounds of the sound table's BGM section
//   --no-length      skip length analysis (no length or fade tags, and no sound is found to be silent)
//   --sound-files DIR
//                    take the sound files from another release of the game in DIR, and play them with the input's
//                    code (rip::UseSoundFiles)
//   --jobs N         the number of length analyses run in parallel (default: the number of CPU threads, at most 8 in
//                    a 32-bit build)
//   --game NAME      game tag (default: the profile's name)
//   --artist NAME, --year YEAR, --copyright TEXT, --by NAME (gcsfby)
//   --version        print the version
//
// A set plays the game's code in game mode. A game therefore requires a driver profile (src/rip/profiles.cpp). Sounds
// that make no sound are listed and skipped. The files of the sequence archive that no sound plays are ripped too,
// after the sound table's sounds. The inputs are ripped one after another. An input that fails doesn't stop the rest.
// Files dropped on the program become inputs.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/jaudio/wave_system.h"
#include "xsflib/binary.h"
#include "xsflib/cli/rip.h"
#include "xsflib/files.h"
#include "xsflib/psf/psf.h"
#include "xsflib/rip/file_names.h"
#include "xsflib/rip/tool.h"

#include "gcsf/format.h"
#include "rip/rip.h"

using namespace gcsf;
namespace fs = std::filesystem;

namespace
{

constexpr char kUsage[] =
    "usage: gcsfrip [options] <input> [<input> ...]\n"
    "\n"
    "Rips each input into a .gcsflib and a .minigcsf per sound. The output folder is next to the\n"
    "input and has the input's name with \"_gcsf\" added. An input is a GameCube disc image (ISO,\n"
    "NKit, RVZ, WIA, GCZ, CISO or WDF) or an extracted disc. A game requires a driver profile.\n"
    "You can also drop files on the program. Sounds that make no sound are listed and skipped.\n"
    "Sequences that no sound of the game plays are ripped as music too. Streams are left out.\n"
    "\n"
    "options:\n"
    "  -o, --output DIR     put the rips in DIR\n"
    "  --list               only list a game's sounds and whether it has a driver profile\n"
    "  --only REGEX         rip only the sounds whose label matches\n"
    "  --bgm                rip only the music: the sounds in the sound table's BGM section\n"
    "  --no-length          skip length analysis and omit length and fade tags. The analysis also\n"
    "                       finds the sounds that make no sound.\n"
    "  --sound-files DIR    play another release's sound files with the input's code. DIR holds\n"
    "                       that release's files: an extracted disc, a title's folder, or only\n"
    "                       the sound files. Twilight Princess's Wii and Wii U releases work.\n"
    "                       So does The Wind Waker HD.\n"
    "  --jobs N             the number of length analyses that run at once (default: the number\n"
    "                       of CPU threads, and at most 8 in a 32-bit build)\n"
    "  --game NAME, --artist NAME, --year YEAR, --copyright TEXT, --by NAME\n"
    "                       tags (the game defaults to the profile's name)\n"
    "  --version            show the version\n"
    "  -h, --help           show this help\n";

// GCSF's rip tool has the options that every rip tool has (xsflib/cli/rip.h), and --sound-files.
struct Options : xsflib::cli::RipOptions
{
    std::string sound_files; // the folder given with --sound-files, or empty
};

// One sound to rip: its number in the file names (SoundNumbers), its sound ID, its label, and its length once
// analysed. A sequence that no sound plays has its file's name and the start-up archive in which the sound plays it.
struct Job
{
    uint32_t index;
    uint32_t sound_id;
    std::string label;
    std::optional<rip::Timing> timing;
    std::string stop; // the emulation's error, for a sound that stops the machine before it makes a sound
    std::string file_name;
    std::optional<FileChunk> archive;
    std::vector<uint32_t> waves;  // the waves that the driver loads, for a profile whose driver NamesWaves
    xsflib::psf::Variables ports; // the ports that the driver sets (rip::Sound::ports)
};

// The number of each of the game's sounds in the file names: its place in the list without the variants of port
// effects (rip::Sound::ports). A variant takes its effect's number. The sequences that no sound plays come next.
std::vector<uint32_t> SoundNumbers(const std::vector<rip::Sound>& sounds)
{
    std::vector<uint32_t> numbers;
    uint32_t next = 0;
    for (const rip::Sound& sound : sounds)
    {
        numbers.push_back(sound.ports.empty() || numbers.empty() ? next++ : numbers.back());
    }

    return numbers;
}

// A game's library, and the start-up archive's wave systems for a game that loads its wave groups as its scenes require
// them (rip::FindsWaves). The library then holds the archives of the jobs' wave groups. Other games have no systems.
struct GameLibrary
{
    std::vector<uint8_t> bytes;
    std::vector<jaudio::WaveSystem> systems;
};

// A game's ID for messages, with \xNN for each byte outside printable ASCII. The NDDEMO disc's ID holds a NUL.
std::string PrintableId(std::string_view id)
{
    std::string text;
    for (const char c : id)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte < 0x7f)
        {
            text += c;
            continue;
        }

        char escape[8];
        std::snprintf(escape, sizeof(escape), "\\x%02x", static_cast<unsigned>(byte));
        text += escape;
    }

    return text;
}

// Prints the game's sounds, one per line, with their IDs and whether each is music or a stream, then the sequences
// that no sound plays. A stream that plays as music shows as a stream: a rip leaves it out. Only a game with a driver
// profile gets here: the profile names the archive that lists the sounds.
void PrintSounds(const disc::GameFiles& game, const std::vector<rip::Sound>& sounds,
                 const std::vector<rip::UnplayedSequence>& unplayed)
{
    std::printf("%s (%s), driver profile found\n", game.Title().c_str(), PrintableId(game.GameId()).c_str());
    const std::vector<uint32_t> numbers = SoundNumbers(sounds);
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        const char* kind = sounds[i].stream ? "stream" : sounds[i].music ? "music" : "";
        std::printf("%04u  %08X  %-6s  %s\n", static_cast<unsigned>(numbers[i]), static_cast<unsigned>(sounds[i].id),
                    kind, sounds[i].label.c_str());
    }

    const std::size_t first_unplayed = numbers.empty() ? 0 : numbers.back() + 1;
    for (std::size_t i = 0; i < unplayed.size(); i++)
    {
        std::printf("%04zu  unplayed  music   %s (%s)\n", first_unplayed + i, unplayed[i].sound.name.c_str(),
                    unplayed[i].file_name.c_str());
    }
}

// For a game that loads its wave groups as its scenes require them: the groups that the driver loads for each sound
// (rip::LengthAnalyzer::FindWaveGroups). `library` holds no wave archive.
void FindWaveGroups(std::vector<Job>& jobs, const std::vector<uint8_t>& library, const rip::GameProfile& profile,
                    const std::vector<jaudio::WaveSystem>& systems, unsigned threads)
{
    // xsflib's AnalyzeLengths runs the search on its threads. The search sets no job's timing. `find` therefore
    // returns false for every job. For a job that returned true without a timing, AnalyzeLengths would warn that the
    // length analysis failed.
    const rip::LengthAnalyzer analyzer(library, profile);
    const auto find = [&](Job& job)
    {
        std::size_t group_count = 0;
        std::optional<std::vector<uint32_t>> groups = analyzer.FindWaveGroups(
            job.sound_id, job.archive ? &*job.archive : nullptr, systems, &group_count, &job.ports);
        if (!groups)
        {
            std::fprintf(stderr, "warning: the waves of %s can't be found; it plays without them\n", job.label.c_str());
            return false;
        }

        if (group_count > groups->size())
        {
            std::fprintf(stderr, "warning: %s plays waves from %zu groups; it loads the first %zu\n", job.label.c_str(),
                         group_count, groups->size());
        }

        job.waves = std::move(*groups);

        return false;
    };
    xsflib::cli::AnalyzeLengths(jobs, threads, find,
                                {.out_of_memory = "out of memory while finding the sounds' waves; try fewer --jobs",
                                 .progress_every = 50,
                                 .progress = "found the waves of"});
}

// The wave archives that hold the jobs' wave groups.
std::vector<std::string> JobArchives(const std::vector<jaudio::WaveSystem>& systems, const std::vector<Job>& jobs)
{
    std::vector<uint32_t> groups;
    for (const Job& job : jobs)
    {
        groups.insert(groups.end(), job.waves.begin(), job.waves.end());
    }

    return jaudio::GroupArchives(systems, groups);
}

// Takes the wave groups whose archives `game` can't read out of the jobs' lists. The sounds then play without those
// waves. Another release can lack an archive that its start-up archive names: Twilight Princess HD has no
// HomeMenu_0.aw.
void DropMissingWaveArchives(const disc::GameFiles& game, const rip::GameProfile& profile,
                             const std::vector<jaudio::WaveSystem>& systems, std::vector<Job>& jobs)
{
    for (const std::string& name : JobArchives(systems, jobs))
    {
        std::vector<uint8_t> bytes;
        if (game.read_file(profile.wave_folder + name, bytes))
        {
            continue;
        }

        std::size_t sounds = 0;
        for (Job& job : jobs)
        {
            const auto in_archive = [&](uint32_t entry)
            {
                const std::vector<std::string> archive = jaudio::GroupArchives(systems, std::span(&entry, 1));
                return !archive.empty() && archive.front() == name;
            };
            sounds += std::erase_if(job.waves, in_archive) ? 1 : 0;
        }

        std::fprintf(stderr, "warning: can't read %s%s; %zu %s without its waves\n", profile.wave_folder, name.c_str(),
                     sounds, sounds == 1 ? "sound plays" : "sounds play");
    }
}

// Loads the game in `input`, opens the program of a TGC on its disc, and finds the game's driver profile. A profile can
// rip a program from a TGC. The program then takes the disc's place. Returns the profile, or nullptr after an error
// message.
const rip::GameProfile* OpenGame(const std::string& input, disc::GameFiles& game)
{
    if (auto err = disc::LoadGame(input, game, rip::kDiscOptions))
    {
        std::fprintf(stderr, "error: %s: %s\n", input.c_str(), err->c_str());
        return nullptr;
    }

    if (auto err = rip::OpenProgram(game))
    {
        std::fprintf(stderr, "error: %s: %s\n", input.c_str(), err->c_str());
        return nullptr;
    }

    const rip::GameProfile* profile = rip::FindProfile(game);
    if (!profile)
    {
        std::fprintf(stderr,
                     "error: %s (%s) has no driver profile. GCSF rips games in game mode. Game mode requires a "
                     "profile for the game's code. Supported:",
                     game.Title().c_str(), PrintableId(game.GameId()).c_str());
        const char* separator = " ";
        for (const rip::GameProfile& known : rip::KnownProfiles())
        {
            std::fprintf(stderr, "%s%s (%s)", separator, known.name, PrintableId(known.game_id).c_str());
            separator = ", ";
        }

        std::fprintf(stderr, "\n");
    }

    return profile;
}

// Makes `game` read its sound files as the rip plays them: a start-up archive from the archive that packs it, then
// another release's sound files if --sound-files gives them. Returns false after an error message.
bool OpenSoundFiles(disc::GameFiles& game, const rip::GameProfile& profile, const Options& o)
{
    rip::UnpackSoundArchive(game, profile);
    if (o.sound_files.empty())
    {
        return true;
    }

    // The sound files are found by the start-up archive's name.
    if (!rip::HasStartUpArchive(profile))
    {
        std::fprintf(stderr, "error: --sound-files doesn't work with %s\n", profile.name);
        return false;
    }

    std::string error;
    const std::optional<fs::path> folder =
        rip::UseSoundFiles(game, profile, xsflib::rip::InputPath(o.sound_files), &error);
    if (!folder)
    {
        std::fprintf(stderr, "error: --sound-files: %s\n", error.c_str());
        return false;
    }

    std::fprintf(stderr, "note: the sound files come from %s\n", xsflib::Utf8(*folder).c_str());

    return true;
}

// The jobs of the sounds that --only and --bgm pick, then of the sequences that no sound plays. Those are music.
// Streams are left out. A sequence's job takes its copy of the start-up archive from `unplayed`.
std::vector<Job> PickJobs(const std::vector<rip::Sound>& sounds, std::vector<rip::UnplayedSequence>& unplayed,
                          const rip::GameProfile& profile, const Options& o)
{
    std::vector<Job> jobs;
    std::size_t streams = 0;
    const std::vector<uint32_t> numbers = SoundNumbers(sounds);
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        if (std::regex_search(sounds[i].label, o.only) && (!o.bgm || sounds[i].music))
        {
            if (sounds[i].stream)
            {
                streams++;
                continue;
            }

            jobs.push_back(
                {.index = numbers[i], .sound_id = sounds[i].id, .label = sounds[i].label, .ports = sounds[i].ports});
        }
    }

    if (streams)
    {
        std::fprintf(stderr, "note: %zu %s left out: GCSF doesn't rip streams\n", streams,
                     streams == 1 ? "stream" : "streams");
    }

    const uint32_t first_unplayed = numbers.empty() ? 0 : numbers.back() + 1;
    for (std::size_t i = 0; i < unplayed.size(); i++)
    {
        rip::UnplayedSequence& u = unplayed[i];
        const rip::Sound sound = rip::SoundOf(u.sound);
        if (std::regex_search(sound.label, o.only))
        {
            jobs.push_back({.index = first_unplayed + static_cast<uint32_t>(i),
                            .sound_id = sound.id,
                            .label = sound.label,
                            .file_name = u.file_name,
                            .archive = FileChunk{profile.sound_archive, std::move(u.sound_archive)}});
        }
    }

    return jobs;
}

// Builds the library. A game that loads its wave groups as its scenes require them (rip::FindsWaves) first gets the
// start-up archive's wave systems. Each job then gets its wave groups, found with a library that holds no wave archive.
// A library that depends on its sounds (see rip::BuildLibrary) holds only the data that the jobs' sounds read. Returns
// nothing after an error message.
std::optional<GameLibrary> BuildGameLibrary(const disc::GameFiles& game, const rip::GameProfile& profile,
                                            const Options& o, const Tags& common,
                                            const std::vector<uint8_t>& sound_archive, std::vector<Job>& jobs)
{
    GameLibrary library;
    std::string error;
    std::optional<std::vector<std::string>> wave_archives;
    if (rip::FindsWaves(profile))
    {
        try
        {
            library.systems = rip::WaveSystems(profile, sound_archive);
        }
        catch (const xsflib::FormatError& e)
        {
            std::fprintf(stderr, "error: %s: %s\n", profile.sound_archive, e.what());
            return std::nullopt;
        }

        const std::vector<std::string> kNone;
        const std::optional<std::vector<uint8_t>> bare = rip::BuildLibrary(game, profile, common, &error, &kNone);
        if (!bare)
        {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return std::nullopt;
        }

        FindWaveGroups(jobs, *bare, profile, library.systems, o.jobs);

        // A disc holds every archive that its start-up archive names. Another release's files may not.
        if (!o.sound_files.empty())
        {
            DropMissingWaveArchives(game, profile, library.systems, jobs);
        }

        wave_archives = JobArchives(library.systems, jobs);
    }

    std::vector<uint32_t> sounds;
    for (const Job& job : jobs)
    {
        sounds.push_back(job.sound_id);
    }

    std::optional<std::vector<uint8_t>> bytes =
        rip::BuildLibrary(game, profile, common, &error, wave_archives ? &*wave_archives : nullptr, &sounds);
    if (!bytes)
    {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return std::nullopt;
    }

    library.bytes = std::move(*bytes);

    return library;
}

// Analyses the length of each job's sound, then takes the sounds that never make a sound out of the jobs. Each sound
// plays in game mode. The progress line comes every 50 sounds. For a game whose wave groups were found, the analysis
// also checks that the sound's groups hold every wave that its notes looked up. A wave that no group holds doesn't
// count. The game has no samples for it either. Returns the number of sounds taken out.
std::size_t MeasureJobs(std::vector<Job>& jobs, const GameLibrary& library, const rip::GameProfile& profile,
                        unsigned threads)
{
    const bool names_waves = rip::NamesWaves(profile);
    const bool finds_waves = rip::FindsWaves(profile);
    const rip::LengthAnalyzer analyzer(library.bytes, profile);
    const auto analyze = [&](Job& job)
    {
        std::vector<jaudio::WaveUse> played;
        job.timing =
            analyzer.Analyze(job.sound_id, job.archive ? &*job.archive : nullptr, names_waves ? &job.waves : nullptr,
                             finds_waves ? &played : nullptr, &job.stop, &job.ports);
        if (const std::size_t outside = jaudio::MissingWaves(library.systems, job.waves, played))
        {
            std::fprintf(stderr, "warning: %s looked up %zu waves outside its wave groups\n", job.label.c_str(),
                         outside);
        }

        return true;
    };
    xsflib::cli::AnalyzeLengths(
        jobs, threads, analyze,
        {.out_of_memory = "out of memory during length analysis; try fewer --jobs", .progress_every = 50});

    std::size_t silent = 0;
    const auto is_silent = [&silent](const Job& job)
    {
        if (!job.timing || !job.timing->silent)
        {
            return false;
        }

        if (job.stop.empty())
        {
            std::fprintf(stderr, "skipped %s: it makes no sound\n", job.label.c_str());
        }
        else
        {
            std::fprintf(stderr, "skipped %s: the game's code stops before it makes a sound (%s)\n", job.label.c_str(),
                         job.stop.c_str());
        }

        silent++;

        return true;
    };
    std::erase_if(jobs, is_silent);

    return silent;
}

// Writes the library, then one .minigcsf per job, into the output folder. An earlier rip's library there stays (see
// xsflib::rip::FileNames::LibraryIn). `silent` sounds were left out. Returns 0, or an exit code on an error.
int WriteRip(const std::string& input, const Options& o, xsflib::cli::FileNameRegistries& registries,
             const disc::GameFiles& game, const rip::GameProfile& profile, const std::vector<uint8_t>& library,
             const std::vector<Job>& jobs, const Tags& common, std::size_t silent)
{
    const fs::path out_dir = o.out_dir.empty() ? xsflib::rip::DefaultOutputDir(input, "_gcsf") : fs::path(o.out_dir);
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    xsflib::rip::FileNames& names = xsflib::cli::NamesIn(registries, out_dir, ".gcsflib", ".minigcsf");
    std::string library_base =
        profile.library_name ? profile.library_name : fs::path(profile.sound_archive).stem().string();
    if (profile.library_per_disc && game.boot.size() > 6)
    {
        // The disc's number is byte 6 of its header, from 0.
        library_base += "Disc" + std::to_string(game.boot[6] + 1);
    }

    const std::string lib_name = names.LibraryIn(out_dir, library_base, library);
    if (!xsflib::rip::WriteFile(out_dir / xsflib::PathFromUtf8(lib_name), library))
    {
        std::fprintf(stderr, "error: can't write %s\n", xsflib::Utf8(out_dir / xsflib::PathFromUtf8(lib_name)).c_str());
        return 1;
    }

    // A sequence that no sound plays gives its file's name in gcsf_sound. A driver that NamesWaves gets the sound's
    // waves. A variant of a port effect gets its ports in gcsf_var.
    const bool names_waves = rip::NamesWaves(profile);
    const auto build = [&lib_name, names_waves](const Job& job, Tags tags)
    {
        const std::vector<uint32_t>* waves = names_waves ? &job.waves : nullptr;
        if (!job.archive)
        {
            return rip::BuildMini(job.sound_id, lib_name, std::move(tags), nullptr, waves, &job.ports);
        }

        tags["gcsf_sound"] = job.file_name;

        return rip::BuildMini(job.sound_id, lib_name, std::move(tags), &*job.archive, waves, &job.ports);
    };
    if (xsflib::cli::WriteMinis(out_dir, names, jobs, common, "gcsf_sound", build) < 0)
    {
        return 1;
    }

    std::fprintf(stderr, "%zu %s written%s\n", jobs.size(), jobs.size() == 1 ? "sound" : "sounds",
                 xsflib::cli::SkippedText(0, "", silent, "").c_str());
    std::fprintf(stderr, "output: %s\n", fs::absolute(out_dir).lexically_normal().string().c_str());

    return 0;
}

// Rips one game. Its output folder takes its registry of file names from `registries` (see xsflib::cli::NamesIn).
// Returns 0, or an exit code on an error.
int RipInput(const std::string& input, const Options& o, xsflib::cli::FileNameRegistries& registries)
{
    disc::GameFiles game;
    const rip::GameProfile* profile = OpenGame(input, game);
    if (!profile || !OpenSoundFiles(game, *profile, o))
    {
        return 1;
    }

    std::string error;
    std::optional<rip::GameSounds> listed = rip::ListGameSounds(game, *profile, &error);
    if (!listed)
    {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    for (const std::string& warning : listed->warnings)
    {
        std::fprintf(stderr, "warning: %s\n", warning.c_str());
    }

    if (o.list)
    {
        PrintSounds(game, listed->sounds, listed->unplayed);
        return 0;
    }

    std::vector<Job> jobs = PickJobs(listed->sounds, listed->unplayed, *profile, o);
    if (jobs.empty())
    {
        if (o.only_text.empty())
        {
            std::fprintf(stderr, "error: no music found (--bgm)\n");
        }
        else
        {
            std::fprintf(stderr, "error: no %s matches '%s'\n", o.bgm ? "music" : "sound", o.only_text.c_str());
        }

        return 1;
    }

    const std::string game_name = o.game_name.empty() ? profile->name : o.game_name;
    const Tags common = xsflib::cli::RipTags(o, game_name, "gcsf", "game");
    std::optional<GameLibrary> library = BuildGameLibrary(game, *profile, o, common, listed->sound_archive, jobs);
    if (!library)
    {
        return 1;
    }

    const std::size_t silent = o.lengths ? MeasureJobs(jobs, *library, *profile, o.jobs) : 0;
    if (jobs.empty())
    {
        std::fprintf(stderr, "error: none of the selected sounds makes a sound\n");
        return 1;
    }

    // A library with zeros in place of what its sounds don't read (rip::TrimsLibrary) is built again without the
    // sounds that make no sound.
    if (silent && rip::TrimsLibrary(*profile))
    {
        library = BuildGameLibrary(game, *profile, o, common, listed->sound_archive, jobs);
        if (!library)
        {
            return 1;
        }
    }

    return WriteRip(input, o, registries, game, *profile, library->bytes, jobs, common, silent);
}

} // namespace

int main(int argc, char** argv)
{
    xsflib::cli::RipTool<Options> tool;
    tool.name = "gcsfrip";
    tool.version = GCSF_VERSION;
    tool.usage = kUsage;
    tool.modes = false;
    tool.archives = false;
    // Each analysis emulates 40 MiB of main memory and ARAM. A 32-bit build has only 2 GiB of address space. It runs at
    // most 8 analyses.
    tool.max_jobs_32bit = 8;
    tool.parse_option = [](const std::string& option, const std::function<std::string()>& next, Options& o)
    {
        if (option != "--sound-files")
        {
            return -1;
        }

        o.sound_files = next();

        return 0;
    };
    tool.rip_input = RipInput;

    return xsflib::cli::RunRipTool(tool, argc, argv);
}
