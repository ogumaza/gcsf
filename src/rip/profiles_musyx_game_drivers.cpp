// SPDX-License-Identifier: MIT

// The driver profiles of the games with MusyX that a driver for the game plays: Mario Golf: Toadstool Tour, Mario
// Power Tennis, WarioWare and Mario Party 4 (see GameProfile in rip/rip.h). The comment of each profile says what the
// game's code does for sound.

#include <array>
#include <cstdint>
#include <vector>

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{
namespace
{

// The parameters of Mario Golf's driver (driver/mario_golf.c's Params from arena_lo on): the OS's arena, the sizes of
// Camelot's heaps and the heap for a song, and the addresses of the OS's functions and variables and of the game's
// functions. The driver calls the functions of `before_sound` before the game's sound set-up. An entry of 0 calls
// nothing. After the set-up, the driver waits `set_up_retraces` video retraces. The functions from `load_voice_bank`
// on load and start the game's effects. A game without one of them gives 0. `upload_check` is the instruction that
// compares the count of MusyX's queue of uploads to ARAM with its limit, or 0. While a voice bank loads, the driver
// sets that limit to 1.
struct MarioGolfParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

    uint32_t arena_lo = 0, arena_hi = 0, heap1_size = 0, heap7_size = 0, heap0_size = 0, song_heap = 0;
    uint32_t os_get_arena_lo = 0, os_get_arena_hi = 0, os_set_arena_lo = 0, os_set_arena_hi = 0, os_init_alloc = 0,
             os_create_heap = 0, os_set_current_heap = 0, os_alloc_from_heap = 0, os_current_heap = 0,
             os_sleep_thread = 0, vi_init = 0, vi_wait_for_retrace = 0, ar_init = 0, aram_blocks = 0, arq_init = 0;
    uint32_t context_init = 0, context_object = 0, heaps_init = 0, create_heap = 0, select_heap = 0;
    std::array<uint32_t, 4> before_sound = {};
    uint32_t sound_init = 0, set_up_retraces = 0, load_banks = 0, play_song = 0, play_jingle = 0;
    uint32_t load_voice_bank = 0, play_voice = 0, play_effect = 0, load_narrations = 0, play_narration = 0,
             load_court = 0, load_file = 0, push_group = 0, upload_check = 0;
};

std::vector<uint32_t> MarioGolfParams::Words() const
{
    return {arena_lo,
            arena_hi,
            heap1_size,
            heap7_size,
            heap0_size,
            song_heap,
            os_get_arena_lo,
            os_get_arena_hi,
            os_set_arena_lo,
            os_set_arena_hi,
            os_init_alloc,
            os_create_heap,
            os_set_current_heap,
            os_alloc_from_heap,
            os_current_heap,
            os_sleep_thread,
            vi_init,
            vi_wait_for_retrace,
            ar_init,
            aram_blocks,
            arq_init,
            context_init,
            context_object,
            heaps_init,
            create_heap,
            select_heap,
            before_sound[0],
            before_sound[1],
            before_sound[2],
            before_sound[3],
            sound_init,
            set_up_retraces,
            load_banks,
            play_song,
            play_jingle,
            load_voice_bank,
            play_voice,
            play_effect,
            load_narrations,
            play_narration,
            load_court,
            load_file,
            push_group,
            upload_check};
}

} // namespace

// Mario Golf: Toadstool Tour (USA). Its sound library is MusyX, under a layer of Camelot code.
//
// OSInit starts the OS's arena at 0x817DC000, near the top of MEM1. The game's linker set that address. OSInit uses it
// because the boot information holds no start for the arena and the debugger flag (0x800030E8) is clear. GCSF's machine
// puts the arena's top at 0x81700000, below the driver. Without the patch, ClearArena (in OSInit) would clear from the
// arena's start to its top. That size is negative and wraps to almost 4 GiB. The clear would wipe the disc's file
// system table at the top of MEM1 and run on past MEM1's end. The patch makes ClearArena return at once. Emulated
// memory starts cleared. The game's main would set the arena from 0x80550000 to 0x81750000. The driver sets it from
// 0x80550000 to 0x81700000.
//
// The game's tables give 737 effects. The bank loader pushes group 87. Group 87 holds the effects that the table at
// 0x801320CC gives. 37 voice banks hold the characters' voices and the menu's effects, and 51 groups hold a narration
// each. A rip of a bank's effect loads the bank through JVoiceLoad1 and starts the effect through voiceFXcall. A rip of
// an effect of group 87 starts it through fx_play. Both put the effect in volume group 1 and start it in studio 0. The
// game's narration player loads a narration's group and starts the narration. A rip gives each effect a volume and a
// pan of 0xFF. MusyX then takes both from the effect's row. Most of the game's calls give 0xFF too.
//
// JVoiceLoad1's upload callback (0x80012724) reads a bank's samples in chunks of 0x2000 bytes. The chunks go into two
// buffers in turn. MusyX's upload loop (0x8008BA64) queues each chunk's upload. The queue holds 16 uploads. GCSF's
// drive reads a chunk sooner than the ARAM DMA moves one. Without the driver's limit, the uploads fall behind. Most of
// a bank's chunks then reach ARAM with the data of a later chunk.
const GameProfile kMarioGolfUsa = {
    .name = "Mario Golf: Toadstool Tour",
    .game_id = "GFTE01",
    .dol_sha1 = "7fa79da5a7cc3159d4af8c99cb38a971080bdbb5",
    .main = 0x80018418,
    .driver = Driver::kMarioGolf,
    .library = SoundLibrary::kMusyX,
    .params =
        MarioGolfParams{
            .arena_lo = 0x80550000,
            .arena_hi = 0x81700000,
            .heap1_size = 0x4000,
            .heap7_size = 0xc0000,
            .heap0_size = 0x1000000,
            .song_heap = 7,
            .os_get_arena_lo = 0x80032d90,
            .os_get_arena_hi = 0x80032d88,
            .os_set_arena_lo = 0x80032da0,
            .os_set_arena_hi = 0x80032d98,
            .os_init_alloc = 0x8003272c,
            .os_create_heap = 0x8003279c,
            .os_set_current_heap = 0x8003271c,
            .os_alloc_from_heap = 0x80032320,
            .os_current_heap = 0x802d7b10, // __OSCurrHeap
            .os_sleep_thread = 0x8003aa58,
            .vi_init = 0x80055a5c,
            .vi_wait_for_retrace = 0x80055f0c,
            .ar_init = 0x8002ea00,
            .aram_blocks = 0x802d7e4c,
            .arq_init = 0x8003095c,
            .context_init = 0x80009dc8,
            .context_object = 0x8023f9cc,
            .heaps_init = 0x8001a510,
            .create_heap = 0x8001a400,
            .select_heap = 0x8001a5c4,
            // The file loader's set-up, then the system set-up's steps before the sound.
            .before_sound = {0x8000b990, 0x8000e2f4, 0x800140c0},
            .sound_init = 0x80012ecc,
            .load_banks = 0x80012a9c,
            .play_song = 0x80013230,
            .play_jingle = 0x800138ec,
            .load_voice_bank = 0x80012844, // JVoiceLoad1
            .play_voice = 0x800120a8,      // voiceFXcall
            .play_effect = 0x80011b88,     // fx_play
            .play_narration = 0x80013be8,
            .upload_check = 0x8008baa8, // cmplwi r0, 16
        }
            .Words(),
    .song_table = 0x80137784,
    .song_count = 38,
    .jingle_table = 0x801379e4,
    .jingle_count = 9,
    .bank_files = {"B/T/M", "B/T/N", "B/T/O", "B/T/P", "D/L/L", "D/L/M", "D/L/N", "D/L/O"},
    .library_name = "MarioGolf",
    .effect_ranges = 0x801320cc,
    .voice_banks = 0x80137308,
    .voice_bank_count = 37,
    .voice_bank_size = 0x14,
    .narrations = 0x801375ec,
    .narration_count = 51,
    .patches = {{0x80030e4c, 0x4e800020}}, // ClearArena: blr
    .track_slots = 0x8025e034,
    .track_slot_size = 0x18,
    .track_slot_count = 16,
    .song_lists = {0x802d84e4, 0x802d84e0},
    .musyx_voices = {.count = 64,
                     .synth = 0x802d8518, // synthVoice
                     .synth_size = 0x404,
                     .dsp = 0x802d8564, // dspVoice
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x8007adc4}, // mcmdLoop
    .musyx_effect_bit = 0x80000000,
};

// Mario Power Tennis (USA, disc version 1). Camelot's layer over MusyX is a later build of Mario Golf's. Mario Golf's
// driver plays it with these parameters.
//
// Like Mario Golf's, OSInit starts the arena at 0x817DC000. Here ClearArena is inline in OSInit. The patch replaces the
// call that begins it with a branch past its end (0x80058508). The game's main would set the arena from 0x80580000 to
// 0x81750000. The driver sets it from 0x80580000 to 0x81700000.
//
// The song table has 111 entries of 0x24 bytes: the file name in 0x18 bytes, then the file number, MusyX's song ID and
// the volume in the top byte. The game plays its jingles from the same table. The banks are the 8 files of MusyX's
// sound groups 0x3F and 0x40. The sequencer is at 0x802C80F8. A song's track slots start 0xF04 bytes into it, and each
// holds its tick at +8.
//
// The game has 980 effects in MusyX groups 0 to 65. The bank loader pushes group 63. Group 63 holds the effects that
// the table at 0x802177E8 gives. 18 voice banks hold the characters' voices. Group 65 holds the narrations, and 8
// groups hold the courts' effects. 37 groups hold the other 56 effects. No table in main.dol lists them. Two packed
// files on the disc name them: a table in the code that A/O/E unpacks to, and a table in C/I/L. The groups follow each
// other on the disc, from groups 26 to 44, then from groups 0 to 17. A rip of a bank's effect loads the bank through
// JVoiceLoad1 and starts the effect through voiceFXcall. voiceFXcall puts the effect in volume group 1. A rip of a
// narration pushes group 65 through NarrationInit, then starts the narration through the game's narration player. A rip
// of a court's effect loads the court's group through the game's court loader. A rip of an effect of the other groups
// loads its group as the bank loader loads its groups. The game's starter of effects (0x8001DA10) then starts an effect
// of group 63, of a court or of the other groups. Every effect plays in studio 1, with controller 0x5B at 0x40. A rip
// gives each effect a volume and a pan of 0xFF. MusyX then takes both from the effect's row. Most of the game's calls
// give 0xFF too.
const GameProfile kMarioPowerTennisUsa = {
    .name = "Mario Power Tennis",
    .game_id = "GOME01",
    .dol_sha1 = "beae7cba9bc987de9c9962bf0d273143aa4c8be6",
    .main = 0x80004574,
    .driver = Driver::kMarioGolf,
    .library = SoundLibrary::kMusyX,
    .params =
        MarioGolfParams{
            .arena_lo = 0x80580000,
            .arena_hi = 0x81700000,
            .heap1_size = 0x90000,
            .heap7_size = 0xc0000,
            .heap0_size = 0x800000,
            .song_heap = 7,
            .os_get_arena_lo = 0x8005a0bc,
            .os_get_arena_hi = 0x8005a0b4,
            .os_set_arena_lo = 0x8005a0cc,
            .os_set_arena_hi = 0x8005a0c4,
            .os_init_alloc = 0x80059a48,
            .os_create_heap = 0x80059ab8,
            .os_set_current_heap = 0x80059a38,
            .os_alloc_from_heap = 0x8005963c,
            .os_current_heap = 0x80309758, // __OSCurrHeap
            .os_sleep_thread = 0x80062bd0,
            .vi_init = 0x8007e884,
            .vi_wait_for_retrace = 0x8007ed34,
            .ar_init = 0x80055a2c,
            .aram_blocks = 0x80309960,
            .arq_init = 0x80057988,
            .context_init = 0x80006a8c,
            .context_object = 0x80225bb8,
            .heaps_init = 0x80029258,
            .create_heap = 0x80029130,
            .select_heap = 0x80029314,
            .before_sound = {0x80008d20}, // the file loader's set-up
            .sound_init = 0x8001dff0,
            // The set-up calls sndMasterVolume(101, 200, 1, 0) and sndMasterVolume(101, 200, 0, 1) (0x8001A8D4): a
            // fade of 200 ms. The game plays no music before it has ended. 13 retraces take 217 ms.
            .set_up_retraces = 13,
            .load_banks = 0x8001a214,
            .play_song = 0x80019fc4,
            .play_jingle = 0x8001afd8,
            .load_voice_bank = 0x8001b77c, // JVoiceLoad1
            .play_voice = 0x8001ba3c,      // voiceFXcall
            .play_effect = 0x8001da10,
            .load_narrations = 0x8001a31c, // NarrationInit
            .play_narration = 0x8001b25c,
            .load_court = 0x8001a670,
            .load_file = 0x800083fc,
            .push_group = 0x8001a124,
        }
            .Words(),
    .song_table = 0x802165e8,
    .song_count = 111,
    .song_name_size = 0x18,
    .bank_files = {"A/O/F", "A/O/G", "A/O/H", "A/O/I", "A/O/J", "A/O/K", "A/O/L", "A/O/M"},
    .library_name = "MarioPowerTennis",
    .effect_ranges = 0x802177e8,
    .voice_banks = 0x80215e98,
    .voice_bank_count = 18,
    .voice_bank_size = 0x20,
    .voice_bank_name_size = 16,
    .narrations = 0x80216298,
    .narration_count = 106,
    .narration_files = 754, // A/O/N
    .court_banks = 0x80217828,
    .court_bank_count = 10,
    .other_groups = 2566, // B/X/J
    .other_group_count = 37,
    .patches = {{0x80058404, 0x48000104}}, // OSInit: b past the arena's clearing
    .track_slots = 0x802c9004,
    .track_slot_size = 0x18,
    .track_slot_count = 16,
    .song_lists = {0x8030a22c, 0x8030a228},
    .musyx_voices = {.count = 64,
                     .synth = 0x8030a260, // synthVoice
                     .synth_size = 0x458,
                     .dsp = 0x8030a358, // dspVoice
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x800ad3c0}, // mcmdLoop
    .musyx_effect_bit = 0x80000000,
};

// WarioWare, Inc.: Mega Party Game$! (USA). Its sound library is MusyX, under a layer of Intelligent Systems' code. The
// parameters follow driver/warioware.c's Params from os_sleep_thread. The game's music is MusyX songs in
// sound_data.bin. The game's sound test names 231 songs. Its table's entries are 0x20 bytes: the song's ID, its volume
// and two more bytes, then a name for each of six languages. This release has only the English names (the second).
//
// The game's play function (0x800348B8) takes a mode and a number. A number below 1606 plays a song, at the volume of
// its entry in the sound test's table or in the table of songs that play as effects (0x801B4918). A number from 1606
// on plays MusyX's effect of the number less 1606 through sndFXStartEx, at the volume of its entry in the table of
// effects (0x801B6640), with the entry's reverb and chorus sends (MIDI controllers 91 and 93). The mode only decides
// which of the function's slots a sound may take from another. Each rip plays one sound, in the mode of the music. The
// length analysis follows an effect through MusyX's voices.
const GameProfile kWarioWareUsa = {
    .name = "WarioWare, Inc.: Mega Party Game$!",
    .game_id = "GZWE01",
    .dol_sha1 = "422e40ca1283a91e9f3c6a8e5da6ae82639b1e02",
    .main = 0x80005c0c,
    .driver = Driver::kWarioWare,
    .library = SoundLibrary::kMusyX,
    .params =
        {
            0x800a11f4, // os_sleep_thread: OSSleepThread
            0x8004166c, // heaps_init: the game's heaps
            2,          // max_heaps
            0x80018cf4, // load_file: the game's file loader
            0x802663c0, // file_names[0]: "gcwario_sound.sdir"
            0x802663d4, // file_names[1]: "gcwario_sound.pool"
            0x802663e8, // file_names[2]: "gcwario_sound.proj"
            0x802663fc, // file_names[3]: "sound_data.bin"
            0x802663ac, // file_names[4]: "gcwario_sound.samp"
            0x80296488, // file_pointers[0]: the sound set-up's sample directory
            0x8029648c, // file_pointers[1]: its pool
            0x80296490, // file_pointers[2]: its project
            0x80296494, // file_pointers[3]: its songs
            0x80296484, // file_pointers[4]: its samples
            0x800327b8, // sound_init: the sound set-up
            0x800348b8, // play_sound: the game's play function
            1,          // play_mode: the mode in which the game plays music
        },
    .bank_files = {"gcwario_sound.sdir", "gcwario_sound.pool", "gcwario_sound.proj", "sound_data.bin",
                   "gcwario_sound.samp"},
    .library_name = "WarioWare",
    .named_songs = 0x801b2c38,
    .named_song_count = 231,
    .named_song_size = 0x20,
    .named_song_name = 0xc,
    .effect_songs = 0x801b4918,
    .effect_song_count = 933,
    .play_effects = 0x801b6640,
    .play_effect_count = 145,
    .track_slots = 0x8034d09c,
    .track_slot_size = 0x18,
    .track_slot_count = 64,
    .song_lists = {0x80375a34, 0x80375a30},
    .musyx_voices = {.count = 64,
                     .synth = 0x80375a68,
                     .synth_size = 0x458,
                     .dsp = 0x80375b50,
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x800d0854},
    .musyx_effect_bit = 0x80000000,
};

// Mario Party 4 (USA, disc version 1). Its sound library is MusyX 1.5.4, under Hudson's sound manager. The parameters
// follow driver/mario_party.c's Params from os_sleep_thread. The sound manager's file holds every sound group, song and
// effect. The game's audio set-up (0x80032830) gives the sound manager a heap of 0x13FC00 bytes and the ARAM up to
// 0x808000. It starts the output in surround unless the console is set to mono. The boot module loads the base groups.
// Each scene then loads a group set.
//
// The options module's sound test plays each of its songs with the group set of the song's entry. Its table holds 14
// pages of 10 entries at 0xAC58 in the module's file. Message bank 47 of the English messages names the sounds. The
// sound test lists 76 songs and 14 streams. The sound manager's table holds two songs more. Song 0 is a dummy. Song 42
// is song 41 in another group. The length analysis reads MusyX's lists of the songs that play, and the loop counts of
// the sequencer's first song.
//
// The sound manager's table holds 2,308 effects. 236 of its entries are empty: they give the effect number 0xFFFF. The
// Option Room's list of character voices follows the sound test in the module. It names 93 of the effects. The game
// plays an effect with msmSePlay. It loads an effect's group with a scene's group set, or alone through its function at
// 0x800336D4. That function calls msmSysLoadGroup. A rip loads the effect's group alone if it isn't a base group. The
// rip plays the effect without parameters: with no position, at the volume, the pan and the reverb and chorus sends of
// the effect's entry. The length analysis follows an effect through MusyX's voices.
const GameProfile kMarioParty4Usa = {
    .name = "Mario Party 4",
    .game_id = "GMPE01",
    .dol_sha1 = "64b42eac733a57a53d2ce25e7e47f54fc1e31915",
    .main = 0x800057c0,
    .driver = Driver::kMarioParty,
    .library = SoundLibrary::kMusyX,
    .params =
        {
            0x800babdc, // os_sleep_thread: OSSleepThread
            0x800b5da4, // os_get_arena_lo: OSGetArenaLo
            0x800b5d9c, // os_get_arena_hi: OSGetArenaHi
            0x800be118, // dvd_init: DVDInit
            0x80117254, // msm_sys_init: msmSysInit
            0x8012e9b8, // msm_path: "/sound/mpgcsnd.msm"
            0x8012e9cb, // pdt_path: "/sound/mpgcstr.pdt"
            0x13fc00,   // heap_size: the sound manager's heap
            0x808000,   // aram_end: the end of the sound manager's ARAM
            0x80115fa0, // msm_sys_set_output_mode: msmSysSetOutputMode
            2,          // output_mode: SND_OUTPUTMODE_SURROUND
            0x8011618c, // msm_sys_get_samp_size: msmSysGetSampSize
            0x80116fbc, // msm_sys_load_group: msmSysLoadGroup
            0x80116818, // msm_sys_load_group_set: msmSysLoadGroupSet
            0x801185e4, // msm_mus_play: msmMusPlay
            0x80119a94, // msm_se_play: msmSePlay
        },
    .bank_files = {"sound/mpgcsnd.msm"},
    .library_name = "MarioParty4",
    .stream_file = "sound/mpgcstr.pdt",
    .sound_test_module = "dll/option.rel",
    .sound_test = 0xac58,
    .sound_test_count = 140,
    .sound_test_voices = 0xb518,
    .sound_test_voice_count = 123,
    .messages = "mess/board_e.dat",
    .song_lists = {0x801d47a4, 0x801d47a0}, // seqActiveRoot and seqPausedRoot
    .section_loops = 0x801ae214,            // in seqInstance
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 50,
                     .synth = 0x801d47d8, // synthVoice
                     .synth_size = 0x404,
                     .dsp = 0x801d48b4, // dspVoice
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x80100208}, // mcmdLoop
    .musyx_effect_bit = 0x8000,
};

// Mario Party 4 (USA, disc version 0). Its main.dol differs from disc version 1's only in three of the debugger's
// functions (DBRead, DBQueryData and DBInitComm). Every other function and variable keeps its address. The options
// module, the messages and the sound files are disc version 1's byte for byte.
const GameProfile kMarioParty4UsaRev0 = []
{
    GameProfile profile = kMarioParty4Usa;
    profile.dol_sha1 = "ff8d894776d37167e454ae3f0c9e644407c1e6b1";

    return profile;
}();

// Mario Party 4's European release, disc versions 0 and 2. Both hold the same main.dol and the same sound files. The
// sound manager, MusyX and the game's audio set-up are the American code at other addresses. The options module holds
// the American sound test 0x68 bytes lower. The English messages give the American names, apart from five: four call
// Koopa Kid Mini Bowser, and one has an accent. The sound manager's file is the American one byte for byte.
const GameProfile kMarioParty4Eur = {
    .name = "Mario Party 4",
    .game_id = "GMPP01",
    .dol_sha1 = "65ef91650e9201047f50a3c39c7693a21fff2b29",
    .main = 0x800057c0,
    .driver = Driver::kMarioParty,
    .library = SoundLibrary::kMusyX,
    .params =
        {
            0x800bab68, // os_sleep_thread: OSSleepThread
            0x800b5cc4, // os_get_arena_lo: OSGetArenaLo
            0x800b5cbc, // os_get_arena_hi: OSGetArenaHi
            0x800be0a4, // dvd_init: DVDInit
            0x80117084, // msm_sys_init: msmSysInit
            0x8012e7d8, // msm_path: "/sound/mpgcsnd.msm"
            0x8012e7eb, // pdt_path: "/sound/mpgcstr.pdt"
            0x13fc00,   // heap_size: the sound manager's heap
            0x808000,   // aram_end: the end of the sound manager's ARAM
            0x80115dd0, // msm_sys_set_output_mode: msmSysSetOutputMode
            2,          // output_mode: SND_OUTPUTMODE_SURROUND
            0x80115fbc, // msm_sys_get_samp_size: msmSysGetSampSize
            0x80116dec, // msm_sys_load_group: msmSysLoadGroup
            0x80116648, // msm_sys_load_group_set: msmSysLoadGroupSet
            0x80118414, // msm_mus_play: msmMusPlay
            0x801198c4, // msm_se_play: msmSePlay
        },
    .bank_files = {"sound/mpgcsnd.msm"},
    .library_name = "MarioParty4",
    .stream_file = "sound/mpgcstr.pdt",
    .sound_test_module = "dll/option.rel",
    .sound_test = 0xabf0,
    .sound_test_count = 140,
    .sound_test_voices = 0xb4b0,
    .sound_test_voice_count = 123,
    .messages = "mess/board_e.dat",
    .song_lists = {0x801f1b64, 0x801f1b60}, // seqActiveRoot and seqPausedRoot
    .section_loops = 0x801cb5b4,            // in seqInstance
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 50,
                     .synth = 0x801f1b98, // synthVoice
                     .synth_size = 0x404,
                     .dsp = 0x801f1c74, // dspVoice
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x80100038}, // mcmdLoop
    .musyx_effect_bit = 0x8000,
};

// Mario Party 4's Japanese release. The sound manager, MusyX and the game's audio set-up are the American code at other
// addresses. The options module holds the American sound test at the American offsets. The Japanese messages name the
// sounds in the game's Japanese font. The sound manager's file leaves out two macros and two samples of group 0x5A,
// which holds se-2066 to se-2071. Its se-2068 plays se-2067's macro. Its se-2069 plays the macro of the game's silent
// effects. The other groups hold the American samples.
const GameProfile kMarioParty4Jpn = {
    .name = "Mario Party 4",
    .game_id = "GMPJ01",
    .dol_sha1 = "abe951fa239e7cb5de67e1ac091cfbc14010d6e0",
    .main = 0x800057c0,
    .driver = Driver::kMarioParty,
    .library = SoundLibrary::kMusyX,
    .params =
        {
            0x800ba7cc, // os_sleep_thread: OSSleepThread
            0x800b5994, // os_get_arena_lo: OSGetArenaLo
            0x800b598c, // os_get_arena_hi: OSGetArenaHi
            0x800bdd08, // dvd_init: DVDInit
            0x80116e44, // msm_sys_init: msmSysInit
            0x8012e598, // msm_path: "/sound/mpgcsnd.msm"
            0x8012e5ab, // pdt_path: "/sound/mpgcstr.pdt"
            0x13fc00,   // heap_size: the sound manager's heap
            0x808000,   // aram_end: the end of the sound manager's ARAM
            0x80115b90, // msm_sys_set_output_mode: msmSysSetOutputMode
            2,          // output_mode: SND_OUTPUTMODE_SURROUND
            0x80115d7c, // msm_sys_get_samp_size: msmSysGetSampSize
            0x80116bac, // msm_sys_load_group: msmSysLoadGroup
            0x80116408, // msm_sys_load_group_set: msmSysLoadGroupSet
            0x801181d4, // msm_mus_play: msmMusPlay
            0x80119684, // msm_se_play: msmSePlay
        },
    .bank_files = {"sound/mpgcsnd.msm"},
    .library_name = "MarioParty4",
    .stream_file = "sound/mpgcstr.pdt",
    .sound_test_module = "dll/option.rel",
    .sound_test = 0xac58,
    .sound_test_count = 140,
    .sound_test_voices = 0xb518,
    .sound_test_voice_count = 123,
    .messages = "mess/board.dat",
    .japanese_messages = true,
    .song_lists = {0x801d4464, 0x801d4460}, // seqActiveRoot and seqPausedRoot
    .section_loops = 0x801adeb4,            // in seqInstance
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 50,
                     .synth = 0x801d4498, // synthVoice
                     .synth_size = 0x404,
                     .dsp = 0x801d4574, // dspVoice
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x800ffdf8}, // mcmdLoop
    .musyx_effect_bit = 0x8000,
};

} // namespace gcsf::rip
