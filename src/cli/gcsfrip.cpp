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
//   --jobs N         the number of length analyses run in parallel (default: the number of CPU threads, at most 8 in
//                    a 32-bit build)
//   --game NAME      game tag (default: the profile's name)
//   --artist NAME, --year YEAR, --copyright TEXT, --by NAME (gcsfby)
//   --version        print the version
//
// A set plays the game's code in game mode. A game therefore requires a driver profile (src/rip/rip.cpp). Sounds that
// make no sound are listed and skipped. The inputs are ripped one after another. An input that fails doesn't stop the
// rest. Files dropped on the program become inputs.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <new>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "cli/console.h"
#include "common/ascii.h"
#include "disc/game.h"
#include "gcsf/format.h"
#include "jaudio/baa.h"
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
    "\n"
    "options:\n"
    "  -o, --output DIR     put the rips in DIR\n"
    "  --list               only list a game's sounds and whether it has a driver profile\n"
    "  --only REGEX         rip only the sounds whose label matches\n"
    "  --bgm                rip only the music: the sounds in the sound table's BGM section\n"
    "  --no-length          skip length analysis and omit length and fade tags. The analysis also\n"
    "                       finds the sounds that make no sound.\n"
    "  --jobs N             the number of length analyses that run at once (default: the number\n"
    "                       of CPU threads, and at most 8 in a 32-bit build)\n"
    "  --game NAME, --artist NAME, --year YEAR, --copyright TEXT, --by NAME\n"
    "                       tags (the game defaults to the profile's name)\n"
    "  --version            show the version\n"
    "  -h, --help           show this help\n";

struct Options
{
    std::regex only{".*"};
    std::string only_text; // the pattern given with --only, or empty
    std::string out_dir, game_name, artist, year, copyright, by = "gcsfrip";
    bool list = false, lengths = true, bgm = false;
    unsigned jobs = 0;
};

// One sound to rip: its place in the sound table, the sound, and its length once analysed.
struct Job
{
    uint32_t index;
    jaudio::Sound sound;
    std::optional<rip::Timing> timing;
};

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    f.close();

    return !f.fail();
}

// A sound's label for files and tags: the name table's, or its ID when the table has none.
std::string Label(const jaudio::Sound& sound)
{
    if (!sound.label.empty())
    {
        return sound.label;
    }

    char id[16];
    std::snprintf(id, sizeof(id), "%08X", static_cast<unsigned>(sound.id));

    return id;
}

Tags CommonTags(const Options& o, const std::string& game_name)
{
    Tags common;
    common["game"] = game_name;

    if (!o.artist.empty())
    {
        common["artist"] = o.artist;
    }

    if (!o.year.empty())
    {
        common["year"] = o.year;
    }

    if (!o.copyright.empty())
    {
        common["copyright"] = o.copyright;
    }

    common["gcsfby"] = o.by;
    common["gcsf_mode"] = "game";
    common["utf8"] = "1"; // tags are UTF-8 (PSF convention)

    return common;
}

// Length analysis plays every selected sound in game mode. The sounds are independent. The analysis spreads them over
// several threads.
void AnalyzeLengths(std::vector<Job>& jobs, const std::vector<uint8_t>& library, const rip::GameProfile& profile,
                    unsigned threads)
{
    const rip::LengthAnalyzer analyzer(library, profile);
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::atomic<bool> out_of_memory{false};
    const auto worker = [&]
    {
        for (std::size_t i; (i = next++) < jobs.size();)
        {
            try
            {
                jobs[i].timing = analyzer.Analyze(jobs[i].sound.id);
            }
            catch (const std::bad_alloc&)
            {
                out_of_memory = true;
                next = jobs.size(); // the other threads take no more jobs
                return;
            }

            if (!jobs[i].timing)
            {
                std::fprintf(stderr, "warning: length analysis failed for %s; no length tag written\n",
                             Label(jobs[i].sound).c_str());
            }

            const std::size_t n = ++done;
            if (n % 50 == 0 || n == jobs.size())
            {
                std::fprintf(stderr, "  analyzed %zu of %zu\n", n, jobs.size());
            }
        }
    };

    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(jobs.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; t++)
    {
        // If another thread can't start (the system is out of threads or memory), the threads already running do the
        // work.
        try
        {
            pool.emplace_back(worker);
        }
        catch (const std::exception&)
        {
            break;
        }
    }

    worker();

    for (auto& t : pool)
    {
        t.join();
    }

    if (out_of_memory)
    {
        throw std::runtime_error("out of memory during length analysis; try fewer --jobs");
    }
}

// Returns the input's absolute path, without the trailing separator of a folder given as "game/" or ".".
fs::path InputPath(const std::string& input)
{
    const fs::path path = fs::absolute(input).lexically_normal();
    return path.has_filename() ? path : path.parent_path();
}

// The name of `input`: the image's file name without its extension, or the folder's name.
std::string NameOfInput(const std::string& input)
{
    const fs::path path = InputPath(input);
    return fs::is_directory(path) ? path.filename().string() : path.stem().string();
}

// Returns the output folder for `input` when -o isn't given. The folder is next to the input and named after it with
// "_gcsf" added.
fs::path DefaultOutputDir(const std::string& input)
{
    return InputPath(input).parent_path() / (NameOfInput(input) + "_gcsf");
}

// The registry of file names for the rips in `folder`. Rips in one folder share the registry. Their files then don't
// overwrite each other. Folders whose names differ only in case count as one. Windows and macOS treat them as one
// folder: two inputs named "Game.rvz" and "game.iso" rip into one folder there.
rip::FileNames& NamesIn(std::map<std::string, rip::FileNames>& registries, const fs::path& folder)
{
    return registries[AsciiLower(fs::absolute(folder).lexically_normal().string())];
}

// Prints the game's sounds, one per line, with their IDs and whether each is music. Only a game with a driver profile
// gets here: the profile names the archive that lists the sounds.
void ListSounds(const disc::GameFiles& game, const std::vector<jaudio::Sound>& sounds)
{
    std::printf("%s (%s), driver profile found\n", game.Title().c_str(), game.GameId().c_str());
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        std::printf("%04zu  %08X  %-5s  %s\n", i, static_cast<unsigned>(sounds[i].id),
                    rip::IsMusic(sounds[i]) ? "music" : "", Label(sounds[i]).c_str());
    }
}

// Rips one game. Its output folder takes its registry of file names from `registries` (see NamesIn). Returns 0, or an
// exit code on an error.
int RipInput(const std::string& input, const Options& o, std::map<std::string, rip::FileNames>& registries)
{
    disc::GameFiles game;
    if (auto err = disc::LoadGame(input, game))
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
        for (const rip::GameProfile& known : rip::KnownProfiles())
        {
            std::fprintf(stderr, " %s (%s)", known.name, known.game_id);
        }

        std::fprintf(stderr, "\n");
        return 1;
    }

    std::vector<uint8_t> sound_archive;
    if (!game.read_file(profile->sound_archive, sound_archive))
    {
        std::fprintf(stderr, "error: can't read %s from the disc\n", profile->sound_archive);
        return 1;
    }

    std::vector<jaudio::Sound> sounds;
    try
    {
        sounds = rip::ListSounds(sound_archive);
    }
    catch (const jaudio::Error& e)
    {
        std::fprintf(stderr, "error: %s: %s\n", profile->sound_archive, e.what());
        return 1;
    }

    if (o.list)
    {
        ListSounds(game, sounds);
        return 0;
    }

    // The sounds that --only and --bgm pick.
    std::vector<Job> jobs;
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        if (std::regex_search(Label(sounds[i]), o.only) && (!o.bgm || rip::IsMusic(sounds[i])))
        {
            jobs.push_back({static_cast<uint32_t>(i), sounds[i], std::nullopt});
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
    const Tags common = CommonTags(o, game_name);
    std::string error;
    const std::optional<std::vector<uint8_t>> library = rip::BuildLibrary(game, *profile, common, &error);
    if (!library)
    {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // The sounds that never make a sound are left out.
    std::size_t silent = 0;
    if (o.lengths)
    {
        AnalyzeLengths(jobs, *library, *profile, o.jobs);

        const auto is_silent = [&silent](const Job& job)
        {
            if (!job.timing || !job.timing->silent)
            {
                return false;
            }

            std::fprintf(stderr, "skipped %s: it makes no sound\n", Label(job.sound).c_str());
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
    const fs::path out_dir = o.out_dir.empty() ? DefaultOutputDir(input) : fs::path(o.out_dir);
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    rip::FileNames& names = NamesIn(registries, out_dir);
    const std::string lib_name = names.Library(fs::path(profile->sound_archive).stem().string());
    if (!WriteFile(out_dir / lib_name, *library))
    {
        std::fprintf(stderr, "error: can't write %s\n", (out_dir / lib_name).string().c_str());
        return 1;
    }

    for (const Job& job : jobs)
    {
        Tags tags = common;
        tags["title"] = Label(job.sound);
        tags["gcsf_sound"] = Label(job.sound);
        if (job.timing)
        {
            tags["length"] = FormatTime(job.timing->length_ms);
            tags["fade"] = FormatTime(job.timing->fade_ms);
        }

        const std::string name = names.Mini(job.index, Label(job.sound));
        if (!WriteFile(out_dir / name, rip::BuildMini(job.sound.id, lib_name, tags)))
        {
            std::fprintf(stderr, "error: can't write %s\n", name.c_str());
            return 1;
        }

        std::fprintf(stderr, "%s%s%s\n", name.c_str(), tags.count("length") ? "  length " : "",
                     tags.count("length") ? tags["length"].c_str() : "");
    }

    std::fprintf(stderr, "%zu %s written%s\n", jobs.size(), jobs.size() == 1 ? "sound" : "sounds",
                 silent ? ("; " + std::to_string(silent) + " skipped (no sound)").c_str() : "");
    std::fprintf(stderr, "output: %s\n", fs::absolute(out_dir).lexically_normal().string().c_str());

    return 0;
}

// Returns 0 to go on, -1 to stop with success (help or version), or an exit code.
int ParseArgs(int argc, char** argv, Options& o, std::vector<std::string>& inputs)
{
    std::string only;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];

        bool missing_value = false;
        const auto next = [&]() -> std::string
        {
            if (i + 1 >= argc)
            {
                missing_value = true;
                return "";
            }

            return argv[++i];
        };

        if (a == "-h" || a == "--help")
        {
            std::fputs(kUsage, stdout);
            return -1;
        }
        else if (a == "--version")
        {
            std::puts("gcsfrip " GCSF_VERSION);
            return -1;
        }
        else if (a == "-o" || a == "--output")
        {
            o.out_dir = next();
        }
        else if (a == "--list")
        {
            o.list = true;
        }
        else if (a == "--only")
        {
            only = next();
        }
        else if (a == "--bgm")
        {
            o.bgm = true;
        }
        else if (a == "--no-length")
        {
            o.lengths = false;
        }
        else if (a == "--jobs")
        {
            const std::string v = next();
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || n < 1 || n > 1024)
            {
                std::fprintf(stderr, "error: --jobs takes a number of threads from 1 to 1024, not '%s'\n", v.c_str());
                return 2;
            }

            o.jobs = static_cast<unsigned>(n);
        }
        else if (a == "--game")
        {
            o.game_name = next();
        }
        else if (a == "--artist")
        {
            o.artist = next();
        }
        else if (a == "--year")
        {
            o.year = next();
        }
        else if (a == "--copyright")
        {
            o.copyright = next();
        }
        else if (a == "--by")
        {
            o.by = next();
        }
        else if (a.size() > 1 && a[0] == '-')
        {
            std::fprintf(stderr, "error: unknown option %s (see --help)\n", a.c_str());
            return 2;
        }
        else
        {
            inputs.push_back(a);
        }

        if (missing_value)
        {
            std::fprintf(stderr, "error: %s requires a value\n", a.c_str());
            return 2;
        }
    }

    if (inputs.empty())
    {
        std::fprintf(stderr, "%s", kUsage);
        return 2;
    }

    // Report a pattern that doesn't compile before anything is written.
    if (!only.empty())
    {
        try
        {
            o.only = std::regex(only);
            o.only_text = only;
        }
        catch (const std::regex_error& e)
        {
            std::fprintf(stderr, "error: --only: %s\n", e.what());
            return 2;
        }
    }

    // Each analysis emulates 40 MiB of main memory and ARAM. A 32-bit build has only 2 GiB of address space. It runs at
    // most 8 analyses.
    if (o.jobs == 0)
    {
        constexpr unsigned kMaxJobs32 = 8;
        o.jobs = std::max(1u, std::thread::hardware_concurrency());
        if constexpr (sizeof(void*) < 8)
        {
            o.jobs = std::min(o.jobs, kMaxJobs32);
        }
    }

    return 0;
}

int Run(int argc, char** argv)
{
    Options o;
    std::vector<std::string> inputs;
    if (const int parsed = ParseArgs(argc, argv, o, inputs))
    {
        return parsed < 0 ? 0 : parsed;
    }

    // Check all paths before writing anything. Dropped files always exist. This check catches typos, and output paths
    // passed as positional arguments instead of with -o.
    for (std::size_t i = 0; i < inputs.size(); i++)
    {
        std::error_code ec;
        if (!fs::exists(inputs[i], ec))
        {
            std::fprintf(stderr, "error: %s: no such file or folder%s\n", inputs[i].c_str(),
                         i ? " (-o names the output folder)" : "");
            return 1;
        }
    }

    // Rip each input once and continue after errors. Return the highest exit code.
    std::map<std::string, rip::FileNames> registries;
    std::set<fs::path> done;
    int result = 0;
    for (std::size_t i = 0; i < inputs.size(); i++)
    {
        if (inputs.size() > 1)
        {
            std::fprintf(stderr, "%s%s:\n", i ? "\n" : "", InputPath(inputs[i]).filename().string().c_str());
        }

        std::error_code ec;
        const fs::path canonical = fs::weakly_canonical(inputs[i], ec);
        if (!done.insert(ec ? InputPath(inputs[i]) : canonical).second)
        {
            std::fprintf(stderr, "skipped duplicate input\n");
            continue;
        }

        try
        {
            result = std::max(result, RipInput(inputs[i], o, registries));
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "error: %s: %s\n", inputs[i].c_str(), e.what());
            result = std::max(result, 1);
        }
    }

    return result;
}

} // namespace

int main(int argc, char** argv)
{
    SetUpConsole();

    // Report unhandled errors, including allocation failures.
    int result = 1;
    try
    {
        result = Run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
    }

    KeepConsoleOpen();

    return result;
}
