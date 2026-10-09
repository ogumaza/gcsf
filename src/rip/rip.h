// SPDX-License-Identifier: MIT

// GCSF ripper. Builds a .gcsflib for a game and one .minigcsf per sound. Rips are in game mode: the library holds the
// game's code, GCSF's driver with the game's profile, and the sound files from the game's disc.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "disc/game.h"
#include "gcsf/format.h"
#include "jaudio/baa.h"

namespace gcsf::rip
{

// Game-specific values for the driver (see driver/driver.c), and where the game's sound files are. The driver's
// parameter block holds the values in the same order.
struct GameProfile
{
    const char* name;
    const char* game_id;  // "G4SE01"
    const char* dol_sha1; // the main.dol that the addresses belong to
    uint32_t main;        // patched to branch to the driver

    // The sound files on the disc: the start-up archive, the sequence archive and the folder of wave archives.
    const char* sound_archive;
    const char* sequence_archive;
    const char* wave_folder;

    // The driver's parameters, from frame_retraces to wave_arc_state_offset.
    uint32_t frame_retraces;
    uint32_t max_heaps, system_heap_size, aram_audio_size, aram_graph_size, aram_priorities, audio_heap_size,
        disc_heap_size, audio_object_size;
    uint32_t vi_init, vi_wait_for_retrace, get_current_thread, get_thread_priority;
    uint32_t create_root_heap, create_exp_heap, create_solid_heap, create_aram, alloc, alloc_from, thread_ctor,
        become_current_heap, system_heap;
    uint32_t game_globals, audio_object_ctor, create_disc_thread, audio_setup, audio_frame;
    uint32_t sound_manager_offset, section_offset, load_wave_bank, section_data_offset, wave_banks_offset,
        wave_arc_state_offset;

    // What the length analysis reads of a playing sound: a sequence's and an effect's JAISound, by their vtables, and
    // where each holds its root track. A track's sequence reader starts the track: its data's start, its position and
    // its stack depth. The track's sequence control holds the ticks left to wait, and whether the track waits for its
    // notes to finish.
    uint32_t sequence_vtable, sequence_track_offset, effect_vtable, effect_track_offset;
    uint32_t wait_offset, note_wait_offset;

    // The sequence parser's functions that the length analysis watches. The parser runs `parse_command` once for each
    // command of each track. `jump_handler` runs the jump command. Each takes the track in r4. The jump's handler takes
    // the list of its arguments in r5, and its target is the first argument.
    uint32_t parse_command;
    uint32_t jump_handler;
};

const std::vector<GameProfile>& KnownProfiles();

// The profile for a game's main.dol, or nullptr if it isn't supported.
const GameProfile* FindProfile(const disc::GameFiles& game);

// The game's sounds, from its start-up archive. Throws jaudio::Error if the archive is damaged.
std::vector<jaudio::Sound> ListSounds(const std::vector<uint8_t>& sound_archive);

// Whether a sound is music: whether its section in the sound table is named BGM. JAudio2 games name the music's section
// that way.
bool IsMusic(const jaudio::Sound& sound);

// Builds the .gcsflib: the game's code with main patched to branch to the driver, the driver with the profile, the
// disc's header, the start-up and sequence archives and every wave archive that the start-up archive names. Returns
// nullopt and sets `error` if a file can't be read from the disc.
std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error = nullptr);

// A .minigcsf that plays the sound with JAudio sound ID `id` from `lib_name`.
std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags);

// Filenames for the rips in one folder, valid on Windows, macOS and Linux and unique even on case-insensitive
// filesystems.
class FileNames
{
public:
    // "<base>.gcsflib".
    std::string Library(const std::string& base);

    // "<index, four digits> <label>.minigcsf".
    std::string Mini(uint32_t index, const std::string& label);

private:
    std::string Unique(const std::string& stem, const std::string& extension);

    std::set<std::string> taken_; // the names used so far, in lowercase
};

// A sound's length and fade in milliseconds, and whether it never makes a sound.
struct Timing
{
    long long length_ms;
    long long fade_ms;
    bool silent = false;
};

// Works out how long sounds play by playing them in game mode: the library's game code with the driver, on the emulated
// GameCube. Analyze can be called from several threads at once.
class LengthAnalyzer
{
public:
    // `library` is the .gcsflib's contents. Throws std::runtime_error if it can't be read.
    LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile);
    ~LengthAnalyzer();

    // A sound that loops gets two loops and a 10 s fade. A sound loops when one of its tracks jumps back to a command
    // it has already played, twice to the same command. A pass that changes nothing doesn't count. The track idles in
    // that loop. A track's loop counts only while each track that opened before it idles, has finished, or has run no
    // command for 10 s with no ticks left to wait. A sound holds when a track idles, every track has settled that way,
    // and nothing has moved for 10 s. It loops there if it still sounds. Any other sound runs until the game releases
    // it or until it falls silent for good, for at most 600 s. It then ends 0.5 s after its last sound. A sound that
    // never makes a sound is marked silent. Returns nullopt when the sound can't be played.
    std::optional<Timing> Analyze(uint32_t id) const;

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace gcsf::rip
