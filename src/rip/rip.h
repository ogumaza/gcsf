// SPDX-License-Identifier: MIT

// GCSF ripper. Builds a .gcsflib for a game and one .minigcsf per sound. Rips are in game mode: the library holds the
// game's code, GCSF's driver with the game's profile, and the sound files from the game's disc.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/containers.h"
#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/jaudio/archive.h"
#include "xsflib/rip/length.h"

#include "gcsf/format.h"

namespace gcsf
{

namespace disc = ntdlib::disc;
namespace jaudio = ntdlib::jaudio;

} // namespace gcsf

namespace gcsf::rip
{

// How GCSF reads discs: GameCube discs in every container, with GCSF's name in the messages.
inline const disc::GameOptions kDiscOptions{disc::Console::kGameCube, "GCSF", {disc::OpenContainer}};

// The driver that a game's rips contain. Each driver is a source file in driver/. driver/build.py builds it into a
// blob in src/rip/. A driver calls the functions of one game's sound layer.
enum class Driver : uint8_t
{
    kFourSwords, // driver/driver.c: Four Swords Adventures' audio object. It loads every wave bank.
    kWindWaker,
    kTwilightPrincess,
    kMarioGolf, // driver/mario_golf.c: Mario Golf: Toadstool Tour's sound set-up. It loads two whole MusyX groups.
    kWarioWare, // driver/warioware.c: WarioWare, Inc.: Mega Party Game$!'s sound set-up. It loads every MusyX group.
    kLuigisMansion,      // driver/luigis_mansion.c: Luigi's Mansion's sound set-up. The sound library loads every wave.
    kSuperMarioSunshine, // driver/super_mario_sunshine.c: Super Mario Sunshine's sound object, and a scene's waves.
    kPikmin,             // driver/pikmin.c: Pikmin's sound library's start-up, and a piece's wave group.
    // driver/jungle_beat.c: Donkey Kong Jungle Beat's audio manager. It loads the waves that a sound uses.
    kJungleBeat,
    // driver/double_dash.c: Mario Kart: Double Dash!!'s audio object. It loads every wave bank of the start-up archive.
    kDoubleDash,
    kPikmin2,  // driver/pikmin2.c: Pikmin 2's sound scenes. It makes a piece's object as the game does.
    kPacManVs, // driver/pacman_vs.c: Pac-Man Vs.'s wrapper of a JAIBasic. The sound library loads every wave system.
    kCollectorsEdition, // driver/collectors_edition.c: Collector's Edition's sound manager. Its set-up loads the waves.
    // driver/crystal_chronicles.c: Final Fantasy Crystal Chronicles' managers. It loads the wave bank that a piece or
    // an effect names, or the blocks of effects.
    kCrystalChronicles,
    // driver/animal_crossing.c: Animal Crossing's sound library. The sound library reads all of the game's sound data.
    kAnimalCrossing,
    // driver/colosseum_bonus.c: the sound library of the Pokémon Colosseum Bonus Disc's download program, with the
    // effects' scripts and samples.
    kColosseumBonus,
    // driver/musyx.c: MusyX's functions, not a game's layer over them. It loads the groups that a sound of the rip's
    // catalogue names (see GameProfile::musyx_files).
    kMusyX,
};

// Where a game for driver/musyx.c keeps its MusyX files. The ripper gathers them into the catalogue of the game's
// sounds (see rip/musyx.h).
enum class MusyXFiles : uint8_t
{
    kNone,
    // Retro Studios' packs: the songs in MidiData.pak, and the audio group sets in AudioGrp.pak.
    kRetro,
    // Tales of Symphonia's files: a song table in the game's code (GameProfile::musyx_songs), the songs' files, and
    // the groups in `bank_files`. A group's file holds MusyX's four files (see GatherMusyX).
    kTales,
    // Star Fox Adventures' files: a song table in the game's code (GameProfile::musyx_songs), the songs in
    // audio/midi.wad, the music's triggers in audio/data/Music.bin, and the groups of two sets of MusyX's files in
    // audio/.
    kStarFox,
    // Star Wars Rogue Squadron II: Rogue Leader's files: audio/data in the pack Rogue2/data.dat holds the banks of
    // groups and their songs, and the game's music hall names the songs (GameProfile::musyx_songs).
    kRogue,
    // Pokémon Colosseum's FSYS archives: the module common_rel in common.fsys holds the table of sounds
    // (GameProfile::musyx_songs), the table of the songs' files and the reverbs' settings. bgm_archive.fsys holds the
    // songs. common.fsys holds the music's projects, pools and sample directories. sound/ holds their samples.
    kColosseum,
    // Resident Evil Zero's files in bio0/sound/: a song table in the game's code (GameProfile::musyx_songs) names each
    // song's file and the archive that holds the song's group. The archive holds MusyX's project, pool and sample
    // directory. A file with the archive's name holds the samples.
    kResidentEvil,
    // Paper Mario: The Thousand-Year Door's files: one set of MusyX's files, sound/proj/pmario, and the table of the
    // game's sounds in its code (GameProfile::musyx_effects).
    kPaperMario,
    // Star Wars Rogue Squadron III: Rebel Strike's files: audio/data in the pack Rogue3/data.dat holds the banks of
    // groups. Rogue Leader's pack has the same layout. The rip takes the effects that the banks' projects list.
    kRebelStrike,
};

// A reverb of MusyX at `block` in the game's memory, and the settings that the game gives it. The reverb is a standard
// one (SND_AUX_REVERBSTD), or with `high` MusyX's high-quality reverb (SND_AUX_REVERBHI). That one also takes the
// crosstalk between its channels. The driver writes the settings into the block before it prepares the reverb.
struct MusyXReverb
{
    uint32_t block = 0;
    float coloration = 0, mix = 0, time = 0, damping = 0, pre_delay = 0;
    bool high = false;
    float crosstalk = 0;
};

// What the length analysis reads of MusyX's voices to follow an effect. Each voice of MusyX has two parts. A
// synthesizer voice runs the voice's macro. A DSP voice plays the voice's sample. The word at `synth` points at the
// `count` synthesizer voices, `synth_size` bytes each. In a synthesizer voice, the word at `id_offset` is 0xFFFFFFFF
// while the voice is free. The word at `macro_offset` is the start of the voice's macro, or 0 once the macro has ended.
// The word at `step_offset` points at the macro's next step. The 64-bit word at `wait_offset` gives the time at which
// the voice's wait ends: 0 for no wait, and all ones for a wait that only an event ends. The word at `dsp` points at
// the DSP voices, `dsp_size` bytes each. In a DSP voice, the byte at `state_offset` is 0 while the voice plays nothing.
// The word at `position_offset` is the voice's place in ARAM, in nibbles. The word at `loop_offset` is the length of
// the sample's loop, or 0 for a sample without a loop. MusyX's handler of a macro's LOOP step starts at
// `loop_handler`. The handler takes the synthesizer voice in r3 and the step in r4. The length analysis measures no
// effect of a profile whose `count` is 0.
struct MusyXVoices
{
    uint32_t count = 0;
    uint32_t synth = 0, synth_size = 0;
    uint32_t id_offset = 0xf4, macro_offset = 0x34, step_offset = 0x38, wait_offset = 0x98;
    uint32_t dsp = 0, dsp_size = 0;
    uint32_t state_offset = 0, position_offset = 0x20, loop_offset = 0x8c;
    uint32_t loop_handler = 0;
};

// A game's sound library. The format of the start-up archive and the commands of the sequences depend on it.
enum class SoundLibrary : uint8_t
{
    kJAudio2, // a start-up archive in BAA
    kJAudio1, // a start-up archive in AAF
    kMusyX,   // songs in SNG files, which tables in the game's code list
    kJac,     // an early JAudio1 that the game runs through its C functions: a sequence index in the game's code
    kRed,     // Square's Red driver: pieces in BGM files, effects in SEP and SEB files, wave banks in WD files
    // Nintendo's sound library for the Nintendo 64, ported to the GameCube inside an early JAudio1: the sequences, the
    // banks and the samples in one file of sound data, which tables in the game's code divide
    kNintendo64,
    // HAL Laboratory's sound library: a script for each effect in a file of scripts, and the effects' samples in a
    // bank file
    kHal,
};

// The most waves that a .minigcsf can name, for a driver that loads only the waves that a sound uses.
inline constexpr uint32_t kMaxMiniWaves = 64;

// A part of a Jac game's piece in parts (see GameProfile::parted_piece): its number, and the wave system and group
// that the game loads for it.
struct JacPart
{
    uint8_t number = 0;
    uint8_t system = 0;
    uint8_t group = 0;
};

// Game-specific values for the driver (see driver/driver.c), and where the game's sound files are. The driver's
// parameter block holds the values in the same order.
struct GameProfile
{
    const char* name = nullptr;
    const char* game_id = nullptr;  // "G4SE01"
    const char* dol_sha1 = nullptr; // the main.dol that the addresses belong to
    uint32_t main = 0;              // patched to branch to the driver

    Driver driver = Driver::kFourSwords;
    SoundLibrary library = SoundLibrary::kJAudio2;

    // The sound files on the disc: the start-up archive, the sequence archive and the folder of wave archives. A game
    // whose start-up archive holds its sequences has no sequence archive (nullptr).
    const char* sound_archive = nullptr;
    const char* sequence_archive = nullptr;
    const char* wave_folder = nullptr;

    // A game that keeps its start-up archive in another archive: that archive on the disc, a RARC that may be
    // compressed with Yaz0, and the start-up archive's file name in it. The rip holds the start-up archive at
    // `sound_archive`, and the sound library reads it from there (see UnpackSoundArchive).
    const char* packed_archive = nullptr;
    const char* packed_sound_archive = nullptr;

    // The sound name table (.asn) on the disc. It names the sounds of a JAudio1 game's sound table. A HAL game's header
    // of names goes here too (see kHal). Null if the game has none.
    const char* sound_names = nullptr;

    // A Jac game's sequence index (a BARC) in its code. The game plays a piece of music by its number in the index.
    // Its sound files are the bank file at `sound_archive`, the sequence archive and the wave archives that the bank
    // file's wave systems name.
    uint32_t sequence_index = 0;

    // A Jac game's piece in parts (0 for none), and the parts that the game plays. The sound library plays that piece
    // from its start-up. The game plays a part by writing the part's number to a port of the piece's root track. The
    // game loads the part's wave group first.
    uint32_t parted_piece = 0;
    std::vector<JacPart> parts = {};

    // The wave groups that the driver loads at start-up, each (system << 16) | group, for a JAudio1 game whose sound
    // library loads none by itself. A JAudio1 library holds their archives.
    std::vector<uint32_t> staying_groups = {};

    // Whether the start-up archive's sound table has the first layouts (jaudio::FirstLayoutOf).
    bool first_layouts = false;

    // Other files on the disc that the driver loads for the sound library, such as Donkey Kong Jungle Beat's chord
    // archive and chord database. The library holds them.
    std::vector<const char*> other_sound_files = {};

    // Other files of the disc that the game's sound layer reads. The library contains them too.
    std::vector<const char*> other_files = {};

    // Four Swords Adventures' driver's parameters, from frame_retraces to wave_arc_state_offset.
    uint32_t frame_retraces = 0;
    uint32_t max_heaps = 0, system_heap_size = 0, aram_audio_size = 0, aram_graph_size = 0, aram_priorities = 0,
             audio_heap_size = 0, disc_heap_size = 0, audio_object_size = 0;
    uint32_t vi_init = 0, vi_wait_for_retrace = 0, get_current_thread = 0, get_thread_priority = 0;
    uint32_t create_root_heap = 0, create_exp_heap = 0, create_solid_heap = 0, create_aram = 0, alloc = 0,
             alloc_from = 0, thread_ctor = 0, become_current_heap = 0, system_heap = 0;
    uint32_t game_globals = 0, audio_object_ctor = 0, create_disc_thread = 0, audio_setup = 0, audio_frame = 0;
    uint32_t sound_manager_offset = 0, section_offset = 0, load_wave_bank = 0, section_data_offset = 0,
             wave_banks_offset = 0, wave_arc_state_offset = 0;

    // Another driver's parameters, in the order of its parameter block after the wave list (see its source).
    std::vector<uint32_t> params = {};

    // What the length analysis reads of a playing sound: a sequence's and an effect's JAISound, by their vtables, and
    // where each holds its root track. A track's sequence reader starts the track: its data's start and its position,
    // and its stack depth at `depth_offset`. With `relative_positions`, the position is an offset from the data's
    // start. The track's sequence control holds the ticks left to wait, and whether the track waits for its notes to
    // finish. In JAudio1, a track that waits for its notes waits -1 ticks.
    uint32_t sequence_vtable = 0, sequence_track_offset = 0, effect_vtable = 0, effect_track_offset = 0;
    uint32_t wait_offset = 0, note_wait_offset = 0;
    uint32_t depth_offset = 8;
    bool relative_positions = false;

    // The sequence parser's code that the length analysis watches. The CPU reaches `parse_command` once for each
    // command of each track. It holds the track in `command_track_register` there. The CPU reaches `jump_handler` for
    // a jump, with the track in `jump_track_register`. JAudio2's jump handler takes the list of its arguments in
    // `jump_target_register`. The jump's target is the first argument. In JAudio1, `jump_handler` is the code that
    // takes the jump, and `jump_target_register` holds the target. JAudio2's conditional jump (0xC8) takes its jump at
    // `conditional_jump`. There, `conditional_track_register` holds the track, and `conditional_arguments_register`
    // points at the jump's arguments: the condition, then the target. With 0 there, the analysis doesn't watch
    // conditional jumps.
    uint32_t parse_command = 0;
    uint32_t jump_handler = 0;
    uint32_t command_track_register = 4;
    uint32_t jump_track_register = 4;
    uint32_t jump_target_register = 5;
    uint32_t conditional_jump = 0;
    uint32_t conditional_track_register = 30;
    uint32_t conditional_arguments_register = 31;

    // For a game that loads its wave groups as its scenes require them: the functions with which a note looks up its
    // wave, and where the game keeps its table of wave banks (see ntdlib's jaudio::GameWatch). The ripper then finds
    // the groups that each sound's waves are in. A game whose driver loads every wave bank leaves them 0.
    uint32_t wave_handle_functions[2] = {};
    uint32_t wave_bank_table = 0;
    uint32_t wave_bank_table_offset = 0;
    uint32_t wave_bank_count_offset = 0;

    // A later JAudio1, such as Pikmin 2's, keeps a track's sequence reader at `reader_offset` in the track. With
    // `parameters_inline`, a sequence's JAISound contains the sequence's parameters from `parameters_offset`. While a
    // track runs an interrupt, its word at `interrupt_offset` holds the position to return to. With `idle_in_calls`, a
    // track can idle in a loop inside a call (see ntdlib's jaudio::GameWatch).
    uint32_t reader_offset = 0;
    bool parameters_inline = false;
    uint32_t interrupt_offset = 0;
    bool idle_in_calls = false;

    // JAudio1's sounds share one vtable. The analysis tells a sequence from an effect by its ID. A sequence's JAISound
    // points to its parameters at `parameters_offset`. They hold its root track at `parameters_track_offset`. A game
    // whose tracks come from a pool keeps the sequences' root tracks in a table of `root_track_count` words at
    // `root_tracks`. The parameters then hold the number of the root track in that table. An effect plays in a track
    // of the effects sequence. The variable `effects_sequence` holds that sequence's JAISound. The effect's slot is a
    // byte at `effect_slot_offset` in the effect's JAISound. Its high four bits pick a child of the effects sequence's
    // root track. Its low four bits pick a child of that child, the effect's track. A track holds its parent at
    // `parent_offset` and its 16 children from `children_offset`. A track with at least `endless_wait` ticks left to
    // wait waits for the game to stop the sound, as a track that waits for its notes does. 0 means no such wait. A
    // track that runs its interrupt routine holds the position to return to at `interrupt_return_offset`. The word is
    // 0 outside the routine. The analysis leaves out the routine's commands. With 0 as the offset, they count.
    uint32_t parameters_offset = 0;
    uint32_t parameters_track_offset = 0;
    uint32_t root_tracks = 0;
    uint32_t root_track_count = 0;
    uint32_t effects_sequence = 0;
    uint32_t effect_slot_offset = 0;
    uint32_t parent_offset = 0;
    uint32_t children_offset = 0;
    uint32_t endless_wait = 0;
    uint32_t interrupt_return_offset = 0;

    // A MusyX game's songs. The game's code holds a table of songs at `song_table` and a table of jingles at
    // `jingle_table`, with `song_count` and `jingle_count` entries. An entry starts with the song's file name, then
    // gives the song's file number on the disc, its number in MusyX, and its volume in the top byte, a word each. With
    // `song_name_size` 0, the entry gives the name's address. Otherwise the entry holds the name in that many bytes.
    // The driver loads the sound banks from `bank_files`. The library takes the name `library_name`.
    uint32_t song_table = 0, song_count = 0, jingle_table = 0, jingle_count = 0;
    uint32_t song_name_size = 0;
    std::vector<const char*> bank_files = {};
    const char* library_name = nullptr;
    // With `library_per_disc`, the library's name ends with "Disc" and the disc's number, for example
    // `ResidentEvilZeroDisc2`. A game whose discs hold different sounds with the same code sets it. Rips of both discs
    // can then share a folder.
    bool library_per_disc = false;

    // Words that the rip writes over the game's code, each with its address.
    std::vector<std::pair<uint32_t, uint32_t>> patches = {};

    // A MusyX game whose code names its songs in a table. WarioWare's sound test has one. The table at `named_songs`
    // holds `named_song_count` entries of `named_song_size` bytes. An entry's first word is the song's ID: its MusyX
    // song group in the top byte and its number in the low 16 bits. The word at `named_song_name` in the entry points
    // at the song's name. Such a game keeps every song in its sound files. The library holds `bank_files` whole.
    uint32_t named_songs = 0, named_song_count = 0, named_song_size = 0, named_song_name = 0;

    // A game for driver/musyx.c: where it keeps its MusyX files, and the table of songs in its code (see MusyXFiles).
    // A table without an end marker holds `musyx_song_count` entries. A game whose main.dol is a loader for a second
    // DOL keeps that DOL at `embedded_dol`. `params` holds the driver's parameters from os_sleep_thread on. The
    // reverbs of studio 0's aux A and aux B get their settings from `reverbs`. A game that picks aux A's reverb for
    // each song keeps a byte for each song ID at `song_reverbs`. A byte of 2 picks `other_reverb` for aux A.
    MusyXFiles musyx_files = MusyXFiles::kNone;
    uint32_t musyx_songs = 0, musyx_song_count = 0;
    uint32_t embedded_dol = 0;
    // Words that the driver writes for each sound after sndInit, each with its address (see MusyXCatalogue::Write).
    std::vector<std::pair<uint32_t, uint32_t>> musyx_writes = {};
    // The songs that play a stream from the disc along with their notes, each as its group << 16 | its song ID. GCSF
    // doesn't rip streams. A rip of such a song would play only its notes. The ripper leaves it out as a stream.
    std::vector<uint32_t> musyx_streamed_songs = {};
    // The volumes of songs whose volume the game's table of songs doesn't give, each with the song's group << 16 | its
    // song ID.
    std::vector<std::pair<uint32_t, uint8_t>> musyx_song_volumes = {};
    // Pokémon Colosseum's tables in common_rel, as offsets in the module's file (see MusyXFiles::kColosseum): the
    // songs' files, in `musyx_file_count` entries, and the settings of the reverb's presets.
    uint32_t musyx_file_table = 0, musyx_file_count = 0, musyx_reverb_presets = 0;
    std::array<MusyXReverb, 2> reverbs = {};
    uint32_t song_reverbs = 0;
    MusyXReverb other_reverb = {};

    // What the length analysis reads of MusyX: the heads of the two lists of the songs that play, at `song_lists`, and
    // how often each section of a song has looped (see MusyXSections in rip/musyx.h). MusyX counts a section's loops
    // in a halfword. The 16 sections' counts are `section_loop_size` bytes apart from `section_loops`. A profile
    // without them gives the sequencer's slots for a song's tracks instead: `track_slot_count` of `track_slot_size`
    // bytes from `track_slots`. A slot's first word is the tick at which the track's next region starts. It falls when
    // the track's section loops.
    uint32_t track_slots = 0, track_slot_size = 0, track_slot_count = 0;
    std::array<uint32_t, 2> song_lists = {};
    uint32_t section_loops = 0, section_loop_size = 0;
    // MusyX's voices, for the lengths of effects (see MusyXVoices).
    MusyXVoices musyx_voices = {};
    // A game whose code names its effects in a table: `musyx_effect_count` rows from `musyx_effects` (see
    // MusyXFiles).
    uint32_t musyx_effects = 0, musyx_effect_count = 0;
    // Rebel Strike maps part of its code and data through the CPU's page table. main.dol holds that part at
    // `packed_module`: two big-endian words, its packed size and its size, then the part, a DOL that zlib packs. The
    // rip unpacks it and places each of its sections `module_offset` bytes above the section's address. The profile's
    // patches map the sections there with a BAT. The rip leaves out main.dol's sections that they overlap. Other games
    // give 0 for both.
    uint32_t packed_module = 0, module_offset = 0;

    // A Red game's pieces of music: the BGM files in `music_folder`, each named "music" and the piece's number in three
    // digits or more. A piece's file names its wave bank: the WD file in `wave_folder` named "wave" and the bank's
    // number in four digits or more. The game's disc library matches the folders' names in any case. The length
    // analysis reads Red's sound controls. The word at `red_controls` points at them, `red_control_size` bytes each.
    // The first `red_music_controls` of them play music. A control's word at `red_music_offset` holds the number of the
    // piece that it plays, or -1. The CPU reaches `red_loop` each time a piece goes back to the start of its loop.
    const char* music_folder = nullptr;
    uint32_t red_controls = 0, red_control_size = 0, red_music_controls = 0, red_music_offset = 0, red_loop = 0;

    // A Red game's effects. The game numbers an effect below kRedFirstSep by its block and its place in the block: the
    // block's number times 1000 plus the place. The blocks are the first `red_blocks` SEB files in `block_folder`, each
    // named "se" and the block's number in three digits or more. An effect from kRedFirstSep on is a SEP file in
    // `effect_folder`, named "se" and the effect's number in six digits or more. A SEP file names its wave bank. Each
    // effect of a block names one too. The length analysis reads Red's effect control, number `red_effect_control`.
    // Its first word points at its `red_effect_tracks` tracks, `red_track_size` bytes each. A track's first word
    // points at its next command. It's 0 when the track is free. The CPU reaches `red_track_loop` each time a track
    // goes back to the start of a loop that never ends. r3 then points at the track's control and r5 at the track.
    const char* block_folder = nullptr;
    const char* effect_folder = nullptr;
    uint32_t red_blocks = 0, red_effect_control = 0, red_effect_tracks = 0, red_track_size = 0, red_track_loop = 0;

    // A game with the Nintendo 64's sound library (SoundLibrary::kNintendo64). Its file of sound data is at
    // `sound_archive`. The game numbers its pieces of music. A table of 256 bytes at `music_table` gives each number's
    // sequence. A table of sequences at `sequence_table` gives each sequence's place in the file of sound data: a
    // 16-bit count of the sequences, then 16 bytes for each from 16 bytes in, with its offset and its size. A sequence
    // of size 0 plays the sequence whose number its offset holds.
    uint32_t music_table = 0, sequence_table = 0;

    // The effects of a game with the Nintendo 64's sound library play in sequence `n64_effects`. Its data holds a table
    // of banks at `n64_effect_banks`: a 16-bit offset in the data for each bank, where the bank's table starts. A
    // bank's table holds a 16-bit offset for each effect, where the effect's script starts. The banks' tables follow
    // the table of banks. An effect's number is its bank's in bits 8 to 11 and its place in the bank in the low byte.
    uint32_t n64_effects = 0, n64_effect_banks = 0;

    // What the length analysis reads of the Nintendo 64's sound library. It plays each sequence in one of
    // `n64_player_count` sequence players of `n64_player_size` bytes from `n64_players`. A player's script, its
    // channels' scripts and their layers' scripts read each byte through the function `n64_read_byte`. The calls from
    // the places where a player, a channel and a layer read a command return to `n64_command_reads`. The function
    // `n64_flow` runs the scripts' jumps, calls and loops. The calls from a player, a channel and a layer return to
    // `n64_flow_returns`.
    uint32_t n64_players = 0, n64_player_count = 0, n64_player_size = 0;
    uint32_t n64_read_byte = 0;
    std::array<uint32_t, 3> n64_command_reads = {};
    uint32_t n64_flow = 0;
    std::array<uint32_t, 3> n64_flow_returns = {};

    // A program in a TGC: a GameCube disc image inside the disc, at `tgc`. `game_id` is the disc's ID, and
    // `program_id` the ID in the TGC's disc header. The ripper rips the program (see OpenProgram).
    const char* tgc = nullptr;
    const char* program_id = nullptr;

    // A game with HAL Laboratory's sound library (SoundLibrary::kHal) keeps its effects' scripts in the file at
    // `sound_archive`, and their samples in the bank file that `other_sound_files` names. The file of scripts holds
    // four tables, each a 32-bit count and then its entries. The effects form groups. The third table gives each
    // group's first effect, and the fourth gives each effect's script, as an offset in the file. An effect's ID is its
    // group in bits 16 to 31 and its place in the group in the low 16 bits. The game's C header at `sound_names`
    // defines a name for an effect's ID. A game with no header (nullptr) names none.
};

// The first number of a Red game's effect in a SEP file (see GameProfile::effect_folder). The game's function that
// plays an effect takes a smaller number as an effect of a block.
inline constexpr uint32_t kRedFirstSep = 4000;

const std::vector<GameProfile>& KnownProfiles();

// The profile for a game's main.dol, or nullptr if it isn't supported. A program from a TGC (OpenProgram) matches the
// profile's `program_id`.
const GameProfile* FindProfile(const disc::GameFiles& game);

// Replaces `game` with the program in a TGC on the disc, for a profile whose `tgc` the disc holds. A TGC is a GameCube
// disc image inside a disc. It holds its disc header, bi2.bin and apploader, its main.dol and its file system table,
// then its files. Its header gives where each part lies in the TGC (see the source). The program's files then come
// from the TGC. Returns an error message for a TGC that can't be read, and nothing otherwise. A disc that holds no
// such TGC stays as it is.
std::optional<std::string> OpenProgram(disc::GameFiles& game);

// The bytes of the game's code and data from `address` to the end of its section. Throws xsflib::FormatError if no
// section holds the address.
std::span<const uint8_t> DolBytes(const ntdlib::Dol& dol, uint32_t address);

// Whether gcsfrip takes the game's sounds from its code or its files (ListSongs): a MusyX game's songs, a Jac game's
// pieces of music, or a Red game's pieces of music. Other games list theirs in a start-up archive.
bool ListsSongs(const GameProfile& profile);

// Whether the profile's driver loads only the waves that a .minigcsf names. Four Swords Adventures' driver loads every
// wave bank of the game, and Mario Golf's loads two whole MusyX groups.
bool NamesWaves(const GameProfile& profile);

// The wave groups that the game loads for the sound with ID `id`, each (system << 16) | group, if the profile gives
// them. The profile gives the groups of Pikmin 2's pieces that the game plays. For other sounds, the ripper finds the
// groups that hold the sound's waves.
std::optional<std::vector<uint32_t>> GameWaveGroups(const GameProfile& profile, uint32_t id);

// Makes `game` read the profile's sound files from another release of the game: the start-up archive, the sequence
// archive and the wave archives. The game's other files still come from `game`. `folder` holds the other release's
// files: an extracted disc, a title's folder, or only the sound files. The start-up archive is found by its file name
// anywhere in `folder`. The folder that holds it stands for the folder of the profile's start-up archive. The sequence
// archive and the wave archives are read from their places relative to it. Returns the folder that holds the
// start-up archive. Returns nullopt and sets `error` if `folder` holds no start-up archive, or more than one.
std::optional<std::filesystem::path> UseSoundFiles(disc::GameFiles& game, const GameProfile& profile,
                                                   const std::filesystem::path& folder, std::string* error = nullptr);

// Makes `game` read the profile's start-up archive from the archive that packs it, for a profile with a
// `packed_archive`. The start-up archive then reads as the file at the profile's `sound_archive`. Other profiles keep
// `game` as it is.
void UnpackSoundArchive(disc::GameFiles& game, const GameProfile& profile);

// The game's sounds, from its start-up archive. A JAudio1 game's sound table names no sound. `names`, if given, is the
// game's sound name table (the profile's `sound_names`). It names each sound that it lists, and gives a sequence the
// game's ID. Otherwise, an effect is named by its category and index ("se4-0123"), and a sequence by its file in the
// sequence archive, without the extension. Pikmin 2's pieces of music come first, named by their files, and its effects
// follow. A JAudio2 start-up archive may be compressed with Yaz0. Throws xsflib::FormatError if an archive that the
// list requires is damaged.
std::vector<jaudio::SoundEntry> ListSounds(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                           const std::vector<uint8_t>& sequence_archive,
                                           const std::vector<uint8_t>* names = nullptr);

// The start-up archive's wave systems, with their groups: a BAA's, or a JAudio1 game's AAF's. A BAA may be compressed
// with Yaz0. Throws xsflib::FormatError if the archive or a wave system is damaged.
std::vector<jaudio::WaveSystem> WaveSystems(const GameProfile& profile, const std::vector<uint8_t>& sound_archive);

// Whether a sound is music: whether its section in the sound table is named BGM. JAudio2 games name the music's section
// that way. Mario Kart: Double Dash!! names it TYPE_BGM.
bool IsMusic(const jaudio::SoundEntry& sound);

// A sound as gcsfrip lists and rips it: the value that the driver's parameter block takes as the sound, the label for
// the sound's files and tags, and whether the sound is music.
struct Sound
{
    uint32_t id;
    std::string label;
    bool music;
    bool stream = false; // a stream: an audio file on the disc, which GCSF doesn't rip
};

// A JAudio2 sound as gcsfrip rips it: its JAudio sound ID, its name in the name table (or the ID in hex when the table
// has none), whether it's music (IsMusic), and whether its type plays a stream.
Sound SoundOf(const jaudio::SoundEntry& entry);

// A MusyX game's songs, from the tables in its main.dol: the songs, then the jingles. A song's ID holds its table in
// the top byte (0 for the songs, 1 for the jingles) and its number in the table below. Its label is its file's name
// without the extension. A game whose code names its songs (GameProfile::named_songs) lists them in the table's order
// instead. The table then gives each song's ID, and a song's label is its name. A Jac game's pieces are the files of
// its sequence index from number 2, without the empty ones. Numbers 0 and 1 are the effects' and the system's
// sequences. A Jac sound's ID is 0x80000000 | group << 24 | wave system << 16 | part << 8 | piece. The piece field
// holds the piece's number in the index. The driver loads the wave group. A piece's part is 0. A piece plays group 0
// of the wave system with the piece's number. A piece's label is its file's name without the extension. A piece in
// parts is listed as its parts instead (GameProfile::parts). A part's label is the piece's label, a hyphen and the
// part's number. A Red game's pieces are the BGM files in its music folder, by number. A piece's ID is its number.
// Its label is the file's name without the extension. Its effects follow: those of its blocks, by number, then its SEP
// files from kRedFirstSep on, by number. An effect's ID is 0x1000000 plus the game's number of the effect. A block's
// effect is labelled with the block's file name without the extension, a hyphen and the effect's place in three
// digits. A SEP file's effect is labelled with the file's name without the extension. A game for driver/musyx.c lists
// the sounds of its catalogue (GatherMusyX), in order. A sound's ID is its number there. The songs and the pieces are
// music. The effects aren't. A song that the profile's `musyx_streamed_songs` lists is a stream too. Throws
// xsflib::FormatError if main.dol, a table or a file of the catalogue can't be read.
std::vector<Sound> ListSongs(const disc::GameFiles& game, const GameProfile& profile);

// The disc path of a MusyX game's song. Camelot's file number holds the file's index above its low bit. The low bit is
// a flag that the game's loader (0x8000b214) tests. The game's path function (0x8001bbf0) drops the flag and writes the
// index in base 26 as three letters, from "A/A/A". Every song's number has the flag clear.
std::string SongPath(uint32_t file_number);

// A file of the sequence archive that no sound of the sound table plays. The game never plays it. A rip plays it as the
// table's first sound that plays a file of the archive. A copy of the start-up archive changes that sound: it plays the
// file instead, at the volume that most of the game's music has.
struct UnplayedSequence
{
    jaudio::SoundEntry sound;           // the sound that plays the file, named after the file without its extension
    std::string file_name;              // the file's name in the sequence archive
    std::vector<uint8_t> sound_archive; // the start-up archive with the sound changed, decoded if it was compressed
};

// The sequence archive's files that no sound plays, in the archive's order. There are none if no sound of the table
// plays a file of the archive. In a JAudio1 game, the effects sequence's file counts as played. Either archive may be
// compressed with Yaz0. Throws xsflib::FormatError if the sequence archive is damaged. It also throws if the start-up
// archive is damaged and is a JAudio1 game's or compressed.
std::vector<UnplayedSequence> UnplayedSequences(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                                const std::vector<jaudio::SoundEntry>& sounds,
                                                const std::vector<uint8_t>& sequence_archive);

// Builds the .gcsflib: the game's code with main patched to branch to the driver, the driver with the profile, the
// disc's header, the start-up and sequence archives, the profile's other sound files and the wave archives. The wave
// archives are those that `wave_archives` names, by file name, or every one that the start-up archive names if it's
// null. A JAudio1 library also has the archives that the sound library loads at start-up. Returns nullopt and sets
// `error` if a file can't be read.
std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error = nullptr,
                                                 const std::vector<std::string>* wave_archives = nullptr);

// A .minigcsf that plays the sound with JAudio sound ID `id` from `lib_name`. `file`, if given, replaces the library's
// file with the same path (an unplayed sequence's start-up archive). `waves`, if given, are the waves that the driver
// loads for the sound: at most kMaxMiniWaves, for a driver that NamesWaves. The driver's source says what an entry
// means. Throws std::invalid_argument if there are too many.
std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags, const FileChunk* file = nullptr,
                               const std::vector<uint32_t>* waves = nullptr);

// A sound's length and fade in milliseconds, and whether it never makes a sound.
using xsflib::rip::Timing;

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
    // never makes a sound is marked silent. Returns nullopt when the sound can't be played. `file` and `waves`, if
    // given, are as in BuildMini. `played`, if given, gets the waves that the sound's notes looked up, for a profile
    // with wave discovery.
    std::optional<Timing> Analyze(uint32_t id, const FileChunk* file = nullptr,
                                  const std::vector<uint32_t>* waves = nullptr,
                                  std::vector<jaudio::WaveUse>* played = nullptr) const;

    // The waves that the sound plays, for a profile with wave discovery. The sound plays with no wave group loaded.
    // Each note still looks up its wave, loaded or not. A lookup counts as a sound. The play ends where Analyze would
    // end it. Returns nullopt when the sound can't be played, and an empty list when it plays no wave.
    std::optional<std::vector<jaudio::WaveUse>> FindWaves(uint32_t id, const FileChunk* file = nullptr) const;

private:
    // Plays the sound, with the driver loading `waves` as in Analyze. The waves that the notes look up go in `found`
    // if it isn't null. For FindWaves, `lookups_sound` makes each lookup count as a sound.
    std::optional<Timing> Play(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                               std::set<jaudio::WaveUse>* found, bool lookups_sound) const;

    struct Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace gcsf::rip
