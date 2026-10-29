// SPDX-License-Identifier: MIT

// GCSF ripper. Builds a .gcsflib for a game and one .minigcsf per sound. Rips are in game mode: the library holds the
// game's code, GCSF's driver with the game's profile, and the sound files from the game's disc.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ntdlib/disc/containers.h"
#include "ntdlib/disc/game.h"
#include "ntdlib/disc/image.h"
#include "ntdlib/jaudio/archive.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/jaudio/wave_system.h"
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
    kFourSwords, // driver/four_swords.c: Four Swords Adventures' audio object. It loads every wave bank.
    // driver/wind_waker.c: The Wind Waker's sound layer (JAIZelBasic). It loads the waves that a .minigcsf names.
    kWindWaker,
    // driver/twilight_princess.c: ntdlib's start-up of Twilight Princess's sound layer, Z2. It loads the wave groups
    // that a .minigcsf names.
    kTwilightPrincess,
    // driver/mario_golf.c: Mario Golf: Toadstool Tour's sound set-up. It loads two whole MusyX groups. It also loads
    // the group of an effect outside them.
    kMarioGolf,
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
    // driver/musyx_upload.c: driver/musyx.c for a game whose sample file doesn't fit in main memory. MusyX reads each
    // group's samples from the file through its upload callback. The rip's catalogue is the same as for kMusyX.
    kMusyXUpload,
    // driver/musyx.c: MusyX's functions, not a game's layer over them. It loads the groups that a sound of the rip's
    // catalogue names (see GameProfile::musyx_files).
    kMusyX,
    // driver/mario_party.c: Hudson's sound manager over MusyX, in Mario Party 4. It loads the base groups and the group
    // set that a .minigcsf names.
    kMarioParty,
};

// Where a game for driver/musyx.c keeps its MusyX files. The ripper gathers them into the catalogue of the game's
// sounds (see rip/musyx_catalogue.h).
enum class MusyXFiles : uint8_t
{
    kNone,
    // Retro Studios' packs: the songs in MidiData.pak, and the audio group sets in AudioGrp.pak.
    kRetro,
    // Tales of Symphonia's files: a song table in the game's code (GameProfile::musyx_songs), the songs' files, the
    // music's group in `bank_files`, and the groups of effects in the other .snd files of S/. A group's file holds
    // MusyX's four files (see GatherMusyX).
    kTales,
    // Star Fox Adventures' files: a song table in the game's code (GameProfile::musyx_songs), the songs in
    // audio/midi.wad, the music's triggers in audio/data/Music.bin, the effects' rows in audio/data/Sfx.bin, and the
    // groups of two sets of MusyX's files in audio/.
    kStarFox,
    // Star Wars Rogue Squadron II: Rogue Leader's files: audio/data in the pack Rogue2/data.dat holds the banks of
    // groups and their songs, and the game's music hall names the songs (GameProfile::musyx_songs). The game's table
    // of sounds gives the effects' volumes (GameProfile::musyx_effects).
    kRogue,
    // Pokémon Colosseum's FSYS archives: the module common_rel in common.fsys holds the table of sounds
    // (GameProfile::musyx_songs), the table of the songs' files and the reverbs' settings. bgm_archive.fsys holds the
    // songs. common.fsys holds the projects, pools and sample directories of the music and the effects. sound/ holds
    // their samples.
    kColosseum,
    // Resident Evil Zero's files in bio0/sound/: a song table in the game's code (GameProfile::musyx_songs) names each
    // song's file and the archive that holds the song's group. The archive holds MusyX's project, pool and sample
    // directory. A file with the archive's name holds the samples. The other archives hold the groups of effects.
    kResidentEvil,
    // Paper Mario: The Thousand-Year Door's files: one set of MusyX's files, sound/proj/pmario, the songs in
    // sound/proj/pmario.slib with their names in pmario.stbl, and the table of the game's sounds in its code
    // (GameProfile::musyx_effects).
    kPaperMario,
    // Pokémon Channel's files: one set of MusyX's files, en/pch/pchsound, and the table of the game's music in its
    // code (GameProfile::musyx_songs). The game's sound viewer, pch/scripts/sndviewer.pkc, names the sounds.
    kPokemonChannel,
    // Star Wars Rogue Squadron III: Rebel Strike's files: audio/data in the pack Rogue3/data.dat holds the banks of
    // groups. Rogue Leader's pack has the same layout. The rip takes the effects that the banks' projects list.
    kRebelStrike,
    // Nintendo Puzzle Collection's modules: the menu and three games, each a module of the game's code that Yay0
    // packs. A module holds its songs and MusyX's files, or a table of where another file holds them (see
    // GatherPuzzleCollection).
    kPuzzleCollection,
    // The NDDEMO tech demo's files: a table of the songs' paths in the demo's code (GameProfile::musyx_songs), the
    // songs' files, one set of MusyX's files in audio/, and a header there that names the effects.
    kNdDemo,
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

// Where some of driver/musyx.c's parameters lie among the profile's words (GameProfile::params): the numbers of voices
// and of studios that sndInit takes, studio 0's aux effects, the volume of every group after the sound's words, and the
// call of sndMasterVolume. The aux effects take the callbacks of aux A and aux B, then their blocks, then their
// preparations. The call takes sndMasterVolume's address, then the volume and the two words that pick the masters that
// it sets. The parameters start at DriverBlock::kProfileOffset. A sound's words can change the aux effects before the
// driver sets them up, and the volumes before the driver sets them. Nintendo Puzzle Collection's sounds do both (see
// MusyXFiles::kPuzzleCollection).
inline constexpr uint32_t kMusyXVoices = 19, kMusyXStudios = 22;
inline constexpr uint32_t kMusyXAuxCallbacks = 35, kMusyXAuxBlocks = 37, kMusyXAuxPreparations = 39;
inline constexpr uint32_t kMusyXGroupVolume = 46, kMusyXMasterVolume = 47;

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

// A part of a Jac game's piece in parts (see GameProfile::parted_piece): its number, and the wave system and group
// that the game loads for it.
struct JacPart
{
    uint8_t number = 0;
    uint8_t system = 0;
    uint8_t group = 0;
};

// How a Jac game starts a sound effect: by its number as an effect of the system or of the player, or by an action of
// an event. An event's number is its type << 12 | the action. The sound of Pikmin's formation (number 0) plays while
// the player moves the formation. A variant gives the move on ports 2 and 3 of the formation's track.
enum class JacEffect : uint8_t
{
    kSystem = 1,
    kPlayer = 2,
    kEvent = 3,
    kFormation = 4,
};

// The sequences of a Jac game's sequence index that play its effects: the effects sequence plays the system's and the
// player's, and the events' sequence the events'. A piece of music has a higher number.
inline constexpr uint32_t kJacEffectsSequence = 0;
inline constexpr uint32_t kJacEventsSequence = 1;
inline constexpr uint32_t kJacFirstPiece = 2;

// A Jac effect's sound ID: 0x80000000 | kind << 24 | number << 8 | the sequence that plays it. The number takes 16
// bits.
constexpr uint32_t JacEffectId(JacEffect kind, uint32_t number)
{
    const uint32_t sequence = kind == JacEffect::kEvent ? kJacEventsSequence : kJacEffectsSequence;
    return 0x80000000u | static_cast<uint32_t>(kind) << 24 | (number & 0xffffu) << 8 | sequence;
}

// Whether a Jac sound's ID is an effect's.
constexpr bool IsJacEffect(uint32_t id)
{
    return (id & 0xffu) < kJacFirstPiece;
}

// The first bit of a JAudio2 sound's ID that gives the archive of the start-up archive's "baac" block that the sound
// loads, plus 1 (see GameProfile::baac_archives). The bits from it are 0 for a sound that loads none.
inline constexpr uint32_t kBaacArchiveShift = 28;

// One of Pikmin 2's pieces of music, for driver/pikmin2.c (see GameProfile::scene_pieces): its file in the sequence
// archive, how the game makes its object, the wave scene that the game loads for it (a group of the wave system,
// besides group 0), an AutoBgm's conductor, the object's sound information, and the beat interval that the game gives
// the root track (0 keeps the default). A boss's music then has a start point: the part of the music that the boss
// starts.
struct ScenePiece
{
    // How Pikmin 2's driver makes a piece's object: driver/pikmin2.c's PIECE_ values. Each is a way in which the game
    // makes a piece's object.
    enum class Kind : uint32_t
    {
        kPlain = 0,    // a BgmSeq that only plays
        kMain = 1,     // an area's music: a JumpBgmSeq with the scene's directors
        kDirected = 2, // a boss's music, or the music of 2-player battles
        kAuto = 3,     // a cave's music: an AutoBgm. Its conductor picks what the tracks play
        kBattle = 4,   // a DirectedBgm with the 2-player battle directors that the game gives battle_t
        kFree = 5,     // a DirectedBgm without directors
        kAutoFree = 6, // an AutoBgm without directors or the arrangement of its melody
    };

    // The JAInter::SoundInfo that the game gives a piece's object, in words: the flags, then the priority and the
    // sequence's slot in the top two bytes, the pitch (1.0) and the volume in the top byte. The game's list of volumes
    // (BgmList.txt) then sets the volume.
    using SoundInfo = std::array<uint32_t, 4>;

    // initMainBgm's sound information. A piece takes it unless its entry gives another.
    static constexpr SoundInfo kAreaInfo = {0x1f00, 127u << 24 | 1u << 16, 0x3f800000, 50u << 24};

    // The wave scene of a piece that the game never plays. The ripper finds the groups that hold its waves.
    static constexpr uint8_t kFindWaves = 0xff;

    const char* sequence;
    Kind kind;
    uint8_t wave_scene;
    const char* conductor = "";
    SoundInfo info = kAreaInfo;
    uint16_t beat_interval = 0;
    uint8_t start_point = 0;
};

// An effect that reads a port that the game's code sets when it starts the effect, and the values that the game puts
// there for it (see GameProfile::port_effects). Each value plays a variant of the effect.
struct PortEffect
{
    uint32_t id; // the effect's sound ID
    int port;
    std::vector<int16_t> values;
};

// A state of Luigi's Mansion in which the game plays LuigiSings (GameProfile::singing_states). The game starts the
// piece through a function of its own. That function reads Luigi's health, the room's lights, the room and the room's
// sound information. It then picks the tracks that play and sets the piece's tempo. The driver gives the game the
// state and calls the function.
struct SingingState
{
    const char* name;    // the state's rip is named after the piece, a space and this name
    uint32_t health;     // Luigi's health, 0 to 100
    bool lights;         // whether the room's lights are on
    uint32_t room;       // the room's number
    uint32_t room_sound; // the room's sound information: the room's sound_echo_parameter, sound_room_size << 6 and
                         // sound_room_code << 8
};

// The first bit of a sound ID that gives a singing state, from 1 (GameProfile::singing_states). A JAudio1 game's
// music ID uses only bit 31 and its low 16 bits.
inline constexpr uint32_t kSingingStateShift = 16;

// A game's driver profile. It names the driver that the game's rips contain and gives the driver's parameters. It also
// says where the game keeps its sound files and what the length analysis reads in the game's code.
struct GameProfile
{
    const char* name = nullptr;
    std::string_view game_id = {};  // six bytes, such as "G4SE01". They may hold a NUL
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

    // A Jac game's catalogue of sound effects in its code (0 for none): `effect_count` rows of 20 bytes. A row gives a
    // number in its word at +4, a pointer to the effect's name at +8 and how the game starts the effect at +16: -2 for
    // an effect of the system, -1 for one of the player, or the type of the event whose action the number is. The
    // table at `event_offsets` holds each event type's first entry in the table of actions that follows it. An
    // action's entry gives the number of the effect that it plays, in its halfword at +4. The game's code starts the
    // effects in `other_effects` itself. `left_out_effects` are effects of the catalogue that the ripper leaves out.
    // Both lists hold sound IDs (JacEffectId).
    uint32_t effect_table = 0;
    uint32_t effect_count = 0;
    uint32_t event_offsets = 0;
    std::vector<uint32_t> other_effects = {};
    std::vector<uint32_t> left_out_effects = {};

    // A Jac game's event type whose event starts an effect as the game creates it (0 for none), and the type of the
    // event that a rip plays the type's effects on instead (see JacEffects).
    uint32_t sounding_event = 0;
    uint32_t quiet_event = 0;

    // The wave groups that a Jac game loads for its effects, each (system << 16) | group. A rip loads them for an
    // effect.
    std::vector<uint32_t> effect_groups = {};

    // The wave groups that the driver loads at start-up, each (system << 16) | group, for a JAudio1 game whose sound
    // library loads none by itself. A JAudio1 library holds their archives.
    std::vector<uint32_t> staying_groups = {};

    // Whether the start-up archive's sound table has the first layouts (jaudio::FirstLayoutOf).
    bool first_layouts = false;

    // Whether the start-up archive's "baac" block holds more start-up archives that the game loads one at a time, each
    // with a wave system and a bank (Mario Kart: Double Dash!!'s voices). An effect whose sequence selects one of
    // their banks has the archive's place in the block, plus 1, in bits 28 to 31 of its ID. The driver loads that
    // archive before it starts the effect. The library holds the wave archives of the archives that its sounds load.
    bool baac_archives = false;

    // Other files on the disc that the driver loads for the sound library, such as Donkey Kong Jungle Beat's chord
    // archive and chord database. The library holds them.
    std::vector<const char*> other_sound_files = {};

    // Other files of the disc that the game's sound layer reads. The library contains them too.
    std::vector<const char*> other_files = {};

    // The driver's parameters, in the order of its parameter block (see the driver's source). They follow the sound's
    // handle, or the wave list for a driver that NamesWaves.
    std::vector<uint32_t> params = {};

    // Pikmin 2's pieces of music. Its driver takes them as a table after `params`. A piece's sound ID is 0x80000000 and
    // its place here. The game's sound table lists sequences whose data the game's sound layer gives them. The pieces
    // take their place (see ListSounds). Other profiles leave this empty.
    std::span<const ScenePiece> scene_pieces = {};

    // What the length analysis and the wave discovery watch in a JAudio game's code (see ntdlib's jaudio::GameWatch).
    // A game that loads its wave groups as its scenes require them gives the functions with which a note looks up its
    // wave. The ripper then finds the groups that each sound's waves are in. The analysis takes `jaudio1` from
    // `library`. A profile leaves it false.
    jaudio::GameWatch watch = {};

    // A sound that the driver plays beside the ripped one, by its word's index in `params`. The driver writes the
    // sound's JAISound to that word. In Donkey Kong Jungle Beat, the sound is the stage's music under a chord effect.
    // The length analysis leaves out the commands of that sound's tracks. A JASTrack holds its parent at
    // `track_parent_offset`. Other profiles leave `background_sound` at -1.
    int background_sound = -1;
    uint32_t track_parent_offset = 0;

    // The effects that read a port that the game's code sets when it starts them, for a driver with a port list
    // (PortList). The list of sounds gives an effect's variants after the effect: one for each value, with the value on
    // the port (see Sound::ports).
    std::vector<PortEffect> port_effects = {};

    // Luigi's Mansion's LuigiSings, by its JAudio sound ID, and the game's states for it. The driver takes the states
    // as a table after `params`. A state's sound ID is the piece's, with the state's place in the table, from 1, at
    // kSingingStateShift. The list of sounds gives the states in place of the piece. Other profiles leave the ID 0.
    uint32_t singing_piece = 0;
    std::span<const SingingState> singing_states = {};

    // A MusyX game's songs. The game's code holds a table of songs at `song_table` and a table of jingles at
    // `jingle_table`, with `song_count` and `jingle_count` entries. An entry starts with the song's file name, then
    // gives the song's file number on the disc, its number in MusyX, and its volume in the top byte, a word each. With
    // `song_name_size` 0, the entry gives the name's address. Otherwise the entry holds the name in that many bytes.
    // The driver loads the sound banks from `bank_files`. The library takes the name `library_name`.
    uint32_t song_table = 0, song_count = 0, jingle_table = 0, jingle_count = 0;
    uint32_t song_name_size = 0;
    std::vector<const char*> bank_files = {};
    const char* library_name = nullptr;

    // The sound effects of a game for driver/mario_golf.c (see CamelotEffects in rip/musyx.cpp). A group's four files
    // hold its pool, its project, its sample directory and its samples, in that order. The table at `effect_ranges`
    // gives the effects of the banks that the game loads at start-up: pairs of words, the first ID and the last, up to
    // a word of 0xFFFFFFFF. The table at `voice_banks` holds `voice_bank_count` entries of `voice_bank_size` bytes. An
    // entry starts with the bank's name in `voice_bank_name_size` bytes, or with the name's address for a size of 0.
    // The number of the bank's first file, the count of its effects and the first effect's ID follow, a word each. The
    // entry ends with the group's ID in a half. The table at `narrations` holds `narration_count` entries of 8 bytes:
    // the number of the narration's first file in a word, then the group's ID and the effect's ID in a half each. A
    // file of 0xFFFFFFFF marks a narration of the group that the game's set-up of narrations loads from
    // `narration_files`. The table at `court_banks` holds `court_bank_count` entries of 8 bytes: the number of the
    // first file of a court's group in a word, then the group's ID in the low half of a word. `other_group_count`
    // groups that no table lists follow each other on the disc, four files each, from the file numbered `other_groups`.
    uint32_t effect_ranges = 0;
    uint32_t voice_banks = 0, voice_bank_count = 0, voice_bank_size = 0, voice_bank_name_size = 0;
    uint32_t narrations = 0, narration_count = 0, narration_files = 0;
    uint32_t court_banks = 0, court_bank_count = 0;
    uint32_t other_groups = 0, other_group_count = 0;

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

    // The effects of such a game. Its play function starts them by number (see MusyXSongs). The table at
    // `effect_songs` holds `effect_song_count` songs that the game plays as effects, 8 bytes each: the song's MusyX
    // song group in the first byte, and its number in a big-endian half at +2. The table at `play_effects` holds
    // `play_effect_count` of MusyX's effects, 6 bytes each. Each starts with the number that the play function takes,
    // in a big-endian half.
    uint32_t effect_songs = 0, effect_song_count = 0, play_effects = 0, play_effect_count = 0;

    // A game for driver/mario_party.c: its MusyX is under Hudson's sound manager. `bank_files` holds the sound
    // manager's file of sound data. The library holds it whole. Of the stream file at `stream_file`, the library holds
    // what the sound manager reads at start-up: the header and the tables, up to the streams' samples. The game's sound
    // test lists the songs: `sound_test_count` entries of 16 bytes from offset `sound_test` in the file
    // `sound_test_module`. That file is one of the game's modules. The songs' names are messages in the message file
    // `messages` (see MsmSounds). The game's list of character voices holds `sound_test_voice_count` entries of the
    // same form from offset `sound_test_voices` in that module. Its entries name effects.
    const char* stream_file = nullptr;
    const char* sound_test_module = nullptr;
    uint32_t sound_test = 0, sound_test_count = 0, sound_test_voices = 0, sound_test_voice_count = 0;
    const char* messages = nullptr;

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

    // Words that the driver writes for each effect, after `musyx_writes`, each with its address. Pokémon Colosseum's
    // catalogue reads them (see MusyXFiles::kColosseum).
    std::vector<std::pair<uint32_t, uint32_t>> musyx_effect_writes = {};

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

    // The callback and the preparation of MusyX's standard reverb, for a game whose sounds can give aux A that reverb
    // in place of the effect in `params`. `other_reverb` then holds the reverb's block and settings (see
    // MusyXFiles::kPuzzleCollection).
    std::array<uint32_t, 2> standard_reverb = {};

    // The word in MusyX's state that holds the numbers of voices, for a game whose sounds limit them in other ways
    // than sndInit does. The word holds four counts, a byte each: all the voices, those for music, those for effects,
    // and the studios. sndInit sets all four. sndSetMaxVoices sets the two in the middle (see
    // MusyXFiles::kPuzzleCollection).
    uint32_t musyx_voice_limits = 0;

    // What the length analysis reads of MusyX: the heads of the two lists of the songs that play, at `song_lists`, and
    // how often each section of a song has looped (see MusyXSections in rip/musyx_catalogue.h). MusyX counts a
    // section's loops in a halfword. The 16 sections' counts are `section_loop_size` bytes apart from `section_loops`.
    // A profile without them gives the sequencer's slots for a song's tracks instead: `track_slot_count` of
    // `track_slot_size` bytes from `track_slots`. A slot's first word is the tick at which the track's next region
    // starts. It falls when the track's section loops.
    uint32_t track_slots = 0, track_slot_size = 0, track_slot_count = 0;
    std::array<uint32_t, 2> song_lists = {};
    uint32_t section_loops = 0, section_loop_size = 0;

    // MusyX's voices, for the lengths of effects (see MusyXVoices).
    MusyXVoices musyx_voices = {};

    // A sound ID with this bit set plays one of MusyX's effects, in a game without a catalogue (see MeasureMusyXSound).
    // 0 for a game that rips none.
    uint32_t musyx_effect_bit = 0;

    // A game whose code lists its effects in a table: `musyx_effect_count` rows from `musyx_effects` (see
    // MusyXFiles).
    uint32_t musyx_effects = 0, musyx_effect_count = 0;

    // Pokémon Channel's sound viewer: the offsets in its script of the viewer's names and of the code that gives each
    // name's effect (see MusyXFiles::kPokemonChannel).
    uint32_t viewer_names = 0, viewer_effects = 0;

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
    // `sound_archive`. The game numbers its pieces of music. A table of `n64_pieces` entries at `music_table` gives
    // each number's sequence. An entry takes a byte, or 16 bits with `n64_wide_numbers`. A table of sequences at
    // `sequence_table` gives each sequence's place in the file of sound data: a 16-bit count of the sequences, then 16
    // bytes for each from 16 bytes in, with its offset and its size. A sequence of size 0 plays the sequence whose
    // number its offset holds. With `n64_wide_numbers`, a piece's sound ID takes driver/animal_crossing.c's layout for
    // 16-bit numbers (WIDE_PIECE).
    uint32_t music_table = 0, sequence_table = 0;
    uint32_t n64_pieces = 256;
    bool n64_wide_numbers = false;

    // The island's piece of music in Doubutsu no Mori e+, or 0 for none. The game mutes some of its channels unless
    // the islander is on the island.
    uint32_t n64_island_piece = 0;

    // The game's start of a piece of music plays its numbers from `n64_effect_numbers[0]` to `n64_effect_numbers[1]` as
    // effects. They aren't pieces. Number 0 stops the music. A game that plays no number as an effect leaves both at 0.
    std::array<uint32_t, 2> n64_effect_numbers = {};

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
    // `n64_flow_returns`. A player holds its script's state, its sequence's data and the ticks left to wait at the
    // offsets in `n64_player_fields`. The default is the place of each in Animal Crossing's build.
    uint32_t n64_players = 0, n64_player_count = 0, n64_player_size = 0;
    uint32_t n64_read_byte = 0;
    std::array<uint32_t, 3> n64_command_reads = {};
    uint32_t n64_flow = 0;
    std::array<uint32_t, 3> n64_flow_returns = {};
    std::array<uint32_t, 3> n64_player_fields = {0x78, 0x18, 0x10};

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

// Whether the game has a start-up archive: a BAA for JAudio2 or an AAF for JAudio1. The archive's sound table lists
// the game's sounds (see ListGameSounds). UseSoundFiles finds another release's sound files by the archive's name. A
// game of another sound library lists its sounds from its code or its files. The `sound_archive` of such a game's
// profile names another kind of file, or none.
bool HasStartUpArchive(const GameProfile& profile);

// Whether the profile's driver loads only the waves that a .minigcsf names. Four Swords Adventures' driver loads every
// wave bank of the game, and Mario Golf's loads two whole MusyX groups.
bool NamesWaves(const GameProfile& profile);

// Whether the profile's game loads its wave groups as its scenes require them (see GameProfile::watch). The ripper then
// finds the groups that hold each sound's waves. The library holds only the archives of those groups.
bool FindsWaves(const GameProfile& profile);

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
// sequence archive, without the extension. Pikmin 2's pieces of music (GameProfile::scene_pieces) come first, each
// named by its file. Its effects follow. A JAudio2 start-up archive may be compressed with Yaz0. Throws
// xsflib::FormatError if an archive that the list requires is damaged.
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

    // The ports that the driver sets for a variant of an effect (GameProfile::port_effects). The variant's gcsf_var tag
    // holds them, and its label is the effect's, a space and the tag's value. Other sounds have none.
    xsflib::psf::Variables ports = {};

    // Whether the sound's files take the number of the sound before it in the list: a port effect's variant, or a
    // singing state after the first (GameProfile::singing_states).
    bool shares_number = false;
};

// A JAudio2 sound as gcsfrip rips it: its JAudio sound ID, its name in the name table (or the ID in hex when the table
// has none), whether it's music (IsMusic), and whether its type plays a stream.
Sound SoundOf(const jaudio::SoundEntry& entry);

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

// The game's sounds as gcsfrip lists and rips them (ListGameSounds).
struct GameSounds
{
    std::vector<Sound> sounds;
    std::vector<uint8_t> sound_archive;     // a JAudio game's start-up archive
    std::vector<UnplayedSequence> unplayed; // a JAudio game's sequences that no sound plays
    std::vector<std::string> warnings;      // what the list leaves out, and why
};

// The game's sounds. A game without a start-up archive (HasStartUpArchive) gets them from its code or its files. The
// function for the profile's sound library lists them and gives their IDs and labels: CatalogueSounds for a game for
// driver/musyx.c, MusyXSongs for another MusyX game, JacPieces and JacEffects for Jac, RedSounds for Red, HalEffects
// for HAL, and N64Sounds for the Nintendo 64's sound library. The songs and the pieces are music. The effects aren't.
// A JAudio game's start-up archive lists its sounds (ListSounds), with the profile's sound name table if it has one.
// The variants of the profile's port effects follow their effects. A profile's singing states take the place of their
// piece. The sequences that no sound plays follow (UnplayedSequences). A JAudio1 game's list requires its sequence
// archive. Another game's sequence archive that can't be read or is damaged loses only those sequences. A warning then
// says so.
// Returns nullopt and sets `error` if a file that the list requires can't be read or is damaged.
std::optional<GameSounds> ListGameSounds(const disc::GameFiles& game, const GameProfile& profile,
                                         std::string* error = nullptr);

// Builds the .gcsflib: the game's code with main patched to branch to the driver, the driver with the profile, the
// disc's header, the start-up and sequence archives, the profile's other sound files and the wave archives. The wave
// archives are those that `wave_archives` names, by file name, or every one that the start-up archive names if it's
// null. A JAudio1 library also has the archives that the sound library loads at start-up. `sounds`, if given, are the
// IDs (Sound::id) of the sounds that the library plays. If TrimsLibrary(profile), the library then holds zeros in place
// of the files that none of them reads. With GameProfile::baac_archives, the library holds only the wave archives of
// the "baac" block's archives that they load. Returns nullopt and sets `error` if a file can't be read.
std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error = nullptr,
                                                 const std::vector<std::string>* wave_archives = nullptr,
                                                 const std::vector<uint32_t>* sounds = nullptr);

// Whether the profile's library holds zeros in place of what none of its sounds reads (BuildLibrary's `sounds`). A
// library for driver/musyx.c holds zeros in place of the file sets and the songs that none of its sounds uses
// (AddMusyXCatalogue). A library for driver/mario_golf.c holds zeros in place of the files of the effects' groups if
// none of its sounds is an effect (AddMusyXFiles). A Red game's library holds zeros in place of the files that none of
// its sounds reads (AddRedFiles). A Jac game's library holds zeros in place of the wave archives that none of its
// sounds loads (AddJacFiles).
bool TrimsLibrary(const GameProfile& profile);

// A .minigcsf that plays sound `id` (Sound::id) from `lib_name`. `file`, if given, replaces the library's file with the
// same path (an unplayed sequence's start-up archive). `waves`, if given, are the waves that the driver loads for the
// sound: at most DriverBlock::kMaxWaves, for a driver that NamesWaves. The driver's source says what an entry means.
// Throws std::invalid_argument if there are too many. `ports`, if given and not empty, go in the gcsf_var tag
// (Sound::ports).
std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags, const FileChunk* file = nullptr,
                               const std::vector<uint32_t>* waves = nullptr,
                               const xsflib::psf::Variables* ports = nullptr);

// A library's contents (from BuildLibrary) as a player loads them: the library's chunks and its process descriptor.
// Throws std::runtime_error if the library can't be read or has no process descriptor. The length analysis reads a
// library once and plays each sound from a copy of the set (SoundSet). xsflib's LoadSet would read and decompress the
// library again for each sound.
LoadedSet LibrarySet(const std::vector<uint8_t>& library);

// `library` with the chunks and the gcsf_var tag that the .minigcsf of sound `id` adds (see BuildMini). A player plays
// the sound from the set.
LoadedSet SoundSet(LoadedSet library, uint32_t id, const FileChunk* file = nullptr,
                   const std::vector<uint32_t>* waves = nullptr, const xsflib::psf::Variables* ports = nullptr);

// A sound's length and fade in milliseconds, and whether it never makes a sound.
using xsflib::rip::Timing;

// Works out how long sounds play by playing them in game mode: the library's game code with the driver, on the emulated
// GameCube. Analyze and FindWaveGroups can be called from several threads at once.
class LengthAnalyzer
{
public:
    // `library` is the .gcsflib's contents. Throws std::runtime_error if it can't be read.
    LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile);
    ~LengthAnalyzer();

    // A sound that loops gets two loops and a 10 s fade. A sound that doesn't loop runs until the game releases it or
    // until it falls silent for good, for at most 600 s. It then ends 0.5 s after its last sound. A sound that never
    // makes a sound is marked silent. A JAudio or Jac sound, or a sound of the Nintendo 64's sound library, loops when
    // one of its tracks jumps back to a command it has already played, twice to the same command. A pass that changes
    // nothing doesn't count. The track idles in that loop. A track's loop counts only while each track that opened
    // before it idles, has finished, or has run no command for 10 s with no ticks left to wait. Such a sound holds when
    // a track idles, every track has settled that way, and nothing has moved for 10 s. It loops there if it still
    // sounds. The analysis of a MusyX, Red or HAL sound follows the sound library's state instead (see MeasuredSound in
    // rip/analysis.h). Returns nullopt when the sound can't be played. The emulated machine can stop with an error
    // once the sound has started. The game's code may read past the end of a table, for example. A sound that stops
    // the machine before the sound makes a sound is marked silent. `stop`, if given, then gets the error. `file`,
    // `waves` and `ports`, if given, are as in BuildMini. `played`, if given, gets the waves that the sound's notes
    // looked up, for a profile with wave discovery.
    std::optional<Timing> Analyze(uint32_t id, const FileChunk* file = nullptr,
                                  const std::vector<uint32_t>* waves = nullptr,
                                  std::vector<jaudio::WaveUse>* played = nullptr, std::string* stop = nullptr,
                                  const xsflib::psf::Variables* ports = nullptr) const;

    // The wave groups that the driver loads for the sound, for a profile with wave discovery, each (system << 16) |
    // group. The profile gives the groups of Pikmin 2's pieces that the game plays. Another sound takes the groups of
    // `systems` that hold the waves that it plays (FindWaves). The driver loads at most DriverBlock::kMaxWaves groups.
    // A longer list keeps its first ones. `group_count`, if given, gets the number of groups before that cut. `ports`
    // is as in BuildMini. Returns nullopt when the sound's waves can't be found.
    std::optional<std::vector<uint32_t>> FindWaveGroups(uint32_t id, const FileChunk* file,
                                                        const std::vector<jaudio::WaveSystem>& systems,
                                                        std::size_t* group_count = nullptr,
                                                        const xsflib::psf::Variables* ports = nullptr) const;

private:
    struct Impl;

    // The waves that the sound plays. The sound plays with no wave group loaded. Each note still looks up its wave,
    // loaded or not. A lookup counts as a sound. The play ends where Analyze would end it. Returns nullopt when the
    // sound can't be played, and an empty list when it plays no wave.
    std::optional<std::vector<jaudio::WaveUse>> FindWaves(uint32_t id, const FileChunk* file,
                                                          const xsflib::psf::Variables* ports) const;

    // Plays the sound, with the driver loading `waves` as in Analyze. The waves that the notes look up go in `found`
    // if it isn't null. For FindWaves, `lookups_sound` makes each lookup count as a sound. `stop` and `ports` are as in
    // Analyze.
    std::optional<Timing> Play(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                               std::set<jaudio::WaveUse>* found, bool lookups_sound, std::string* stop,
                               const xsflib::psf::Variables* ports) const;

    std::unique_ptr<Impl> impl_;
};

} // namespace gcsf::rip
