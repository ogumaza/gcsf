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
// A set plays the game's code in game mode. A game therefore requires a driver profile (src/rip/rip.cpp). Sounds that
// make no sound are listed and skipped. The files of the sequence archive that no sound plays are ripped too, after the
// sound table's sounds. The inputs are ripped one after another. An input that fails doesn't stop the rest. Files
// dropped on the program become inputs.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/jaudio/archive.h"
#include "ntdlib/jaudio/wave_system.h"
#include "xsflib/binary.h"
#include "xsflib/cli/rip.h"
#include "xsflib/files.h"
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

// One sound to rip: its place in the list, the sound, its label, and its length once analysed. A sequence that no
// sound plays has its file's name and the start-up archive in which the sound plays it.
struct Job
{
    uint32_t index;
    rip::Sound sound;
    std::string label;
    std::optional<rip::Timing> timing;
    std::string file_name;
    std::optional<FileChunk> archive;
    std::vector<uint32_t> waves; // the waves that the driver loads, for a profile whose driver NamesWaves
};

// Prints the game's sounds, one per line, with their IDs and whether each is music or a stream, then the sequences
// that no sound plays. Only a game with a driver profile gets here: the profile names the archive that lists the
// sounds.
void ListSounds(const disc::GameFiles& game, const std::vector<rip::Sound>& sounds,
                const std::vector<rip::UnplayedSequence>& unplayed)
{
    std::printf("%s (%s), driver profile found\n", game.Title().c_str(), game.GameId().c_str());
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        const char* kind = sounds[i].music ? "music" : sounds[i].stream ? "stream" : "";
        std::printf("%04zu  %08X  %-6s  %s\n", i, static_cast<unsigned>(sounds[i].id), kind, sounds[i].label.c_str());
    }

    for (std::size_t i = 0; i < unplayed.size(); i++)
    {
        std::printf("%04zu  unplayed  music   %s (%s)\n", sounds.size() + i, unplayed[i].sound.name.c_str(),
                    unplayed[i].file_name.c_str());
    }
}

// For a game that loads its wave groups as its scenes require them: the groups that hold each sound's waves. The sounds
// play with no group loaded, on a library that holds no wave archive (see LengthAnalyzer::FindWaves).
template <typename Job>
void FindWaveGroups(std::vector<Job>& jobs, const std::vector<uint8_t>& library, const rip::GameProfile& profile,
                    const std::vector<jaudio::WaveSystem>& systems, unsigned threads)
{
    const rip::LengthAnalyzer analyzer(library, profile);
    const auto find = [&](Job& job)
    {
        const auto waves = analyzer.FindWaves(job.sound.id, job.archive ? &*job.archive : nullptr);
        if (!waves)
        {
            std::fprintf(stderr, "warning: the waves of %s can't be found; it plays without them\n", job.label.c_str());
            return false;
        }

        job.waves = jaudio::WaveGroups(systems, *waves);
        if (job.waves.size() > rip::kMaxMiniWaves)
        {
            std::fprintf(stderr, "warning: %s plays waves from %zu groups; it loads the first %u\n", job.label.c_str(),
                         job.waves.size(), rip::kMaxMiniWaves);
            job.waves.resize(rip::kMaxMiniWaves);
        }

        return false;
    };
    xsflib::cli::AnalyzeLengths(jobs, threads, find,
                                {.out_of_memory = "out of memory while finding the sounds' waves; try fewer --jobs",
                                 .progress_every = 50,
                                 .progress = "found the waves of"});
}

// Takes the wave groups whose archives `game` can't read out of the jobs' lists. The sounds then play without those
// waves. Another release can lack an archive that its start-up archive names: Twilight Princess HD has no
// HomeMenu_0.aw.
template <typename Job>
void DropMissingWaveArchives(const disc::GameFiles& game, const rip::GameProfile& profile,
                             const std::vector<jaudio::WaveSystem>& systems, std::vector<Job>& jobs)
{
    std::vector<uint32_t> groups;
    for (const Job& job : jobs)
    {
        groups.insert(groups.end(), job.waves.begin(), job.waves.end());
    }

    for (const std::string& name : jaudio::GroupArchives(systems, groups))
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

// Lists a JAudio game's sounds and the sequences that no sound plays. Reads the start-up archive into `sound_archive`.
// Returns 0, or an exit code on an error.
int ListJAudio(const disc::GameFiles& game, const rip::GameProfile& profile, std::vector<uint8_t>& sound_archive,
               std::vector<rip::Sound>& sounds, std::vector<rip::UnplayedSequence>& unplayed)
{
    if (!game.read_file(profile.sound_archive, sound_archive))
    {
        std::fprintf(stderr, "error: can't read %s\n", profile.sound_archive);
        return 1;
    }

    // A JAudio1 game's sound list requires its sequence archive. Otherwise a sequence archive that can't be read loses
    // only the sequences that no sound plays.
    std::vector<uint8_t> sequence_archive;
    const bool has_sequences = game.read_file(profile.sequence_archive, sequence_archive);
    if (!has_sequences && profile.library == rip::SoundLibrary::kJAudio1)
    {
        std::fprintf(stderr, "error: can't read %s\n", profile.sequence_archive);
        return 1;
    }

    std::vector<jaudio::SoundEntry> entries;
    try
    {
        entries = rip::ListSounds(profile, sound_archive, sequence_archive);
    }
    catch (const xsflib::FormatError& e)
    {
        std::fprintf(stderr, "error: %s: %s\n", profile.sound_archive, e.what());
        return 1;
    }

    // The sequences that no sound plays.
    if (!has_sequences)
    {
        std::fprintf(stderr, "warning: can't read %s\n", profile.sequence_archive);
    }
    else
    {
        try
        {
            unplayed = rip::UnplayedSequences(profile, sound_archive, entries, sequence_archive);
        }
        catch (const xsflib::FormatError& e)
        {
            std::fprintf(stderr, "warning: %s: %s; the sequences that no sound plays are left out\n",
                         profile.sequence_archive, e.what());
        }
    }

    for (const jaudio::SoundEntry& entry : entries)
    {
        sounds.push_back(rip::SoundOf(entry));
    }

    return 0;
}

// Rips one game. Its output folder takes its registry of file names from `registries` (see xsflib::cli::NamesIn).
// Returns 0, or an exit code on an error.
int RipInput(const std::string& input, const Options& o, xsflib::cli::FileNameRegistries& registries)
{
    disc::GameFiles game;
    if (auto err = disc::LoadGame(input, game, rip::kDiscOptions))
    {
        std::fprintf(stderr, "error: %s: %s\n", input.c_str(), err->c_str());
        return 1;
    }

    const rip::GameProfile* profile = rip::FindProfile(game);
    if (!profile)
    {
        std::fprintf(stderr,
                     "error: %s (%s) has no driver profile. GCSF rips games in game mode. Game mode requires a "
                     "profile for the game's code. Supported:",
                     game.Title().c_str(), game.GameId().c_str());
        const char* separator = " ";
        for (const rip::GameProfile& known : rip::KnownProfiles())
        {
            std::fprintf(stderr, "%s%s (%s)", separator, known.name, known.game_id);
            separator = ", ";
        }

        std::fprintf(stderr, "\n");
        return 1;
    }

    // Another release's sound files, played with this release's code.
    if (!o.sound_files.empty())
    {
        // The sound files are found by the start-up archive's name. A MusyX game has no start-up archive.
        if (profile->library == rip::SoundLibrary::kMusyX)
        {
            std::fprintf(stderr, "error: --sound-files doesn't work with %s\n", profile->name);
            return 1;
        }

        std::string error;
        const std::optional<fs::path> folder =
            rip::UseSoundFiles(game, *profile, xsflib::rip::InputPath(o.sound_files), &error);
        if (!folder)
        {
            std::fprintf(stderr, "error: --sound-files: %s\n", error.c_str());
            return 1;
        }

        std::fprintf(stderr, "note: the sound files come from %s\n", xsflib::Utf8(*folder).c_str());
    }

    std::vector<uint8_t> sound_archive;
    std::vector<rip::Sound> sounds;
    std::vector<rip::UnplayedSequence> unplayed;
    if (profile->library == rip::SoundLibrary::kMusyX)
    {
        try
        {
            sounds = rip::ListSongs(game, *profile);
        }
        catch (const xsflib::FormatError& e)
        {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    else if (const int code = ListJAudio(game, *profile, sound_archive, sounds, unplayed))
    {
        return code;
    }

    if (o.list)
    {
        ListSounds(game, sounds, unplayed);
        return 0;
    }

    // The sounds that --only and --bgm pick, then the sequences that no sound plays. Those are music. Streams are left
    // out.
    std::vector<Job> jobs;
    std::size_t streams = 0;
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        if (std::regex_search(sounds[i].label, o.only) && (!o.bgm || sounds[i].music))
        {
            if (sounds[i].stream)
            {
                streams++;
                continue;
            }

            jobs.push_back({static_cast<uint32_t>(i), sounds[i], sounds[i].label, std::nullopt, {}, std::nullopt, {}});
        }
    }

    if (streams)
    {
        std::fprintf(stderr, "note: %zu %s left out: GCSF doesn't rip streams\n", streams,
                     streams == 1 ? "stream" : "streams");
    }

    for (std::size_t i = 0; i < unplayed.size(); i++)
    {
        rip::UnplayedSequence& u = unplayed[i];
        const rip::Sound sound = rip::SoundOf(u.sound);
        if (std::regex_search(sound.label, o.only))
        {
            jobs.push_back({static_cast<uint32_t>(sounds.size() + i),
                            sound,
                            sound.label,
                            std::nullopt,
                            u.file_name,
                            FileChunk{profile->sound_archive, std::move(u.sound_archive)},
                            {}});
        }
    }

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
    std::string error;

    // A game that loads its wave groups as its scenes require them: the groups that each sound's waves are in, found
    // with a library that holds no wave archive. The library then holds the archives of those groups.
    std::optional<std::vector<std::string>> wave_archives;
    std::vector<jaudio::WaveSystem> systems;
    const bool finds_waves = profile->wave_handle_functions[0] != 0;
    if (finds_waves)
    {
        try
        {
            systems = rip::WaveSystems(*profile, sound_archive);
        }
        catch (const xsflib::FormatError& e)
        {
            std::fprintf(stderr, "error: %s: %s\n", profile->sound_archive, e.what());
            return 1;
        }

        const std::vector<std::string> none;
        const std::optional<std::vector<uint8_t>> bare = rip::BuildLibrary(game, *profile, common, &error, &none);
        if (!bare)
        {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }

        FindWaveGroups(jobs, *bare, *profile, systems, o.jobs);

        // A disc holds every archive that its start-up archive names. Another release's files may not.
        if (!o.sound_files.empty())
        {
            DropMissingWaveArchives(game, *profile, systems, jobs);
        }

        std::vector<uint32_t> groups;
        for (const Job& job : jobs)
        {
            groups.insert(groups.end(), job.waves.begin(), job.waves.end());
        }

        wave_archives = jaudio::GroupArchives(systems, groups);
    }

    const std::optional<std::vector<uint8_t>> library =
        rip::BuildLibrary(game, *profile, common, &error, wave_archives ? &*wave_archives : nullptr);
    if (!library)
    {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // The sounds that never make a sound are left out.
    const bool names_waves = rip::NamesWaves(*profile);
    std::size_t silent = 0;
    if (o.lengths)
    {
        // Each sound plays in game mode. The progress line comes every 50 sounds.
        const rip::LengthAnalyzer analyzer(*library, *profile);
        // For a game whose wave groups were found, the analysis also checks that the sound's groups hold every wave
        // that its notes looked up. A wave that no group holds doesn't count. The game has no samples for it either.
        const auto analyze = [&](Job& job)
        {
            std::vector<jaudio::WaveUse> played;
            job.timing = analyzer.Analyze(job.sound.id, job.archive ? &*job.archive : nullptr,
                                          names_waves ? &job.waves : nullptr, finds_waves ? &played : nullptr);
            if (const std::size_t outside = jaudio::MissingWaves(systems, job.waves, played))
            {
                std::fprintf(stderr, "warning: %s looked up %zu waves outside its wave groups\n", job.label.c_str(),
                             outside);
            }

            return true;
        };
        xsflib::cli::AnalyzeLengths(
            jobs, o.jobs, analyze,
            {.out_of_memory = "out of memory during length analysis; try fewer --jobs", .progress_every = 50});

        const auto is_silent = [&silent](const Job& job)
        {
            if (!job.timing || !job.timing->silent)
            {
                return false;
            }

            std::fprintf(stderr, "skipped %s: it makes no sound\n", job.label.c_str());
            silent++;

            return true;
        };
        std::erase_if(jobs, is_silent);
    }

    if (jobs.empty())
    {
        std::fprintf(stderr, "error: none of the selected sounds makes a sound\n");
        return 1;
    }

    // The library, then one .minigcsf per sound.
    const fs::path out_dir = o.out_dir.empty() ? xsflib::rip::DefaultOutputDir(input, "_gcsf") : fs::path(o.out_dir);
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    xsflib::rip::FileNames& names = xsflib::cli::NamesIn(registries, out_dir, ".gcsflib", ".minigcsf");
    const std::string lib_name =
        names.Library(profile->library_name ? profile->library_name : fs::path(profile->sound_archive).stem().string());
    if (!xsflib::rip::WriteFile(out_dir / xsflib::PathFromUtf8(lib_name), *library))
    {
        std::fprintf(stderr, "error: can't write %s\n", xsflib::Utf8(out_dir / xsflib::PathFromUtf8(lib_name)).c_str());
        return 1;
    }

    // A sequence that no sound plays gives its file's name in gcsf_sound. A driver that NamesWaves gets the sound's
    // waves.
    const auto build = [&lib_name, names_waves](const Job& job, Tags tags)
    {
        const std::vector<uint32_t>* waves = names_waves ? &job.waves : nullptr;
        if (!job.archive)
        {
            return rip::BuildMini(job.sound.id, lib_name, std::move(tags), nullptr, waves);
        }

        tags["gcsf_sound"] = job.file_name;

        return rip::BuildMini(job.sound.id, lib_name, std::move(tags), &*job.archive, waves);
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
