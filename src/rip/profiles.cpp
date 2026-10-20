// SPDX-License-Identifier: MIT

// The driver profiles of the games that GCSF rips (see GameProfile in rip/rip.h). The comment of each profile says what
// the game's code does for sound.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ntdlib/flipper/z2_params.h"
#include "xsflib/sha1.h"

#include "rip/rip.h"

namespace gcsf::rip
{

namespace
{

// The parameters of Four Swords Adventures' driver (driver/four_swords.c's Params from frame_retraces on): the sizes
// that the game's start-up gives, the addresses of the game's functions and variables, and the piece whose tracks the
// game's music start mutes. That start mutes some children of the piece's track 0. `muted_sound` names the piece, or 0
// for none. Bit n of `muted_children` mutes child n. `track_mute` is JASTrack::mute.
struct FourSwordsParams
{
    uint32_t frame_retraces = 0;
    uint32_t max_heaps = 0, system_heap_size = 0, aram_audio_size = 0, aram_graph_size = 0, aram_priorities = 0,
             audio_heap_size = 0, disc_heap_size = 0, audio_object_size = 0;
    uint32_t vi_init = 0, vi_wait_for_retrace = 0, get_current_thread = 0, get_thread_priority = 0;
    uint32_t create_root_heap = 0, create_exp_heap = 0, create_solid_heap = 0, create_aram = 0, alloc = 0,
             alloc_from = 0, thread_ctor = 0, become_current_heap = 0, system_heap = 0;
    uint32_t game_globals = 0, audio_object_ctor = 0, create_disc_thread = 0, audio_setup = 0, audio_frame = 0;
    uint32_t sound_manager_offset = 0, section_offset = 0, load_wave_bank = 0, section_data_offset = 0,
             wave_banks_offset = 0, wave_arc_state_offset = 0;
    uint32_t track_mute = 0, muted_sound = 0, muted_children = 0;

    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;
};

std::vector<uint32_t> FourSwordsParams::Words() const
{
    return {frame_retraces,
            max_heaps,
            system_heap_size,
            aram_audio_size,
            aram_graph_size,
            aram_priorities,
            audio_heap_size,
            disc_heap_size,
            audio_object_size,
            vi_init,
            vi_wait_for_retrace,
            get_current_thread,
            get_thread_priority,
            create_root_heap,
            create_exp_heap,
            create_solid_heap,
            create_aram,
            alloc,
            alloc_from,
            thread_ctor,
            become_current_heap,
            system_heap,
            game_globals,
            audio_object_ctor,
            create_disc_thread,
            audio_setup,
            audio_frame,
            sound_manager_offset,
            section_offset,
            load_wave_bank,
            section_data_offset,
            wave_banks_offset,
            wave_arc_state_offset,
            track_mute,
            muted_sound,
            muted_children};
}

// Four Swords Adventures (USA). Its sound library is JAudio2, in the generation of Twilight Princess's.
const GameProfile kFourSwordsUsa = {
    .name = "The Legend of Zelda: Four Swords Adventures",
    .game_id = "G4SE01",
    .dol_sha1 = "d86b3d9f785566e34264035e140ef3daf3cc19f0",
    .main = 0x801344c0,
    .sound_archive = "GC4Sword_usa/AudioRes/JaiInit.baa",
    .sequence_archive = "GC4Sword_usa/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "GC4Sword_usa/AudioRes/Waves/",
    .params =
        FourSwordsParams{
            .frame_retraces = 1,
            .max_heaps = 2,
            .system_heap_size = 0x400000,
            .aram_audio_size = 0x800000,
            .aram_graph_size = 0x600000,
            .aram_priorities = 8 << 16 | 7 << 8 | 6,
            .audio_heap_size = 0x180000,
            .disc_heap_size = 0x800,
            .audio_object_size = 0x90,
            .vi_init = 0x8004bd78,
            .vi_wait_for_retrace = 0x8004c228,
            .get_current_thread = 0x80045214,
            .get_thread_priority = 0x8004632c,
            .create_root_heap = 0x80081454,
            .create_exp_heap = 0x800814d4,
            .create_solid_heap = 0x80084bac,
            .create_aram = 0x800790a8,
            .alloc = 0x80084370,
            .alloc_from = 0x8008440c,
            .thread_ctor = 0x800855cc,
            .become_current_heap = 0x8008382c,
            .system_heap = 0x8053a8b0,
            .game_globals = 0x80529dec,
            .audio_object_ctor = 0x8045a080,
            .create_disc_thread = 0x80401fec,
            .audio_setup = 0x80442810,
            .audio_frame = 0x80459674,
            .sound_manager_offset = 0x30,
            .section_offset = 0x44,
            .load_wave_bank = 0x8003df84,
            .section_data_offset = 0x30,
            .wave_banks_offset = 0xec,
            .wave_arc_state_offset = 0x4c,
            // The game's music start (0x80459674, 0x80458ff0) mutes every child of JA_BGM_SELECT's track 0 but 2 and 7.
            .track_mute = 0x80037f1c,
            .muted_sound = 0x0100001a,
            .muted_children = 0xffff & ~(1u << 2 | 1u << 7),
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x80035184, // JASSeqParser::parse
            .jump_handler = 0x80033ec8,  // the sequence parser's command 0xC7
            .sequence_vtable = 0x8049175c,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x804916c8,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
        },
};

// Four Swords Adventures' Japanese release, Four Swords+. Its sound library is the same build as the American
// release's, 0xd8 bytes higher. The rest of the game's code is linked in another order. The audio object has no word
// at the American object's +0x48. The fields after that place are 4 bytes lower.
const GameProfile kFourSwordsJpn = {
    .name = "The Legend of Zelda: Four Swords Plus",
    .game_id = "G4SJ01",
    .dol_sha1 = "56097356bceafde9a64abeab8501b909746ac03c",
    .main = 0x8013a58c,
    .sound_archive = "GC4Sword/AudioRes/JaiInit.baa",
    .sequence_archive = "GC4Sword/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "GC4Sword/AudioRes/Waves/",
    .params =
        FourSwordsParams{
            .frame_retraces = 1,
            .max_heaps = 2,
            .system_heap_size = 0x400000,
            .aram_audio_size = 0x800000,
            .aram_graph_size = 0x600000,
            .aram_priorities = 8 << 16 | 7 << 8 | 6,
            .audio_heap_size = 0x180000,
            .disc_heap_size = 0x800,
            .audio_object_size = 0x8c,
            .vi_init = 0x8004be50,
            .vi_wait_for_retrace = 0x8004c300,
            .get_current_thread = 0x800452ec,
            .get_thread_priority = 0x80046404,
            .create_root_heap = 0x80087590,
            .create_exp_heap = 0x80087610,
            .create_solid_heap = 0x8008ace8,
            .create_aram = 0x8007f1e4,
            .alloc = 0x8008a4ac,
            .alloc_from = 0x8008a548,
            .thread_ctor = 0x8008b708,
            .become_current_heap = 0x80089968,
            .system_heap = 0x80531568,
            .game_globals = 0x804e436c,
            .audio_object_ctor = 0x8007b5c4,
            .create_disc_thread = 0x8032c980,
            .audio_setup = 0x8036c258,
            .audio_frame = 0x8007abd8,
            .sound_manager_offset = 0x30,
            .section_offset = 0x44,
            .load_wave_bank = 0x8003e064,
            .section_data_offset = 0x30,
            .wave_banks_offset = 0xec,
            .wave_arc_state_offset = 0x4c,
            // The game's music start (0x8007abd8, 0x8007a580) mutes every child of JA_BGM_SELECT's track 0 but 2 and 7.
            .track_mute = 0x80037ff4,
            .muted_sound = 0x0100001a,
            .muted_children = 0xffff & ~(1u << 2 | 1u << 7),
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x8003525c, // JASSeqParser::parse
            .jump_handler = 0x80033fa0,  // the sequence parser's command 0xC7
            .sequence_vtable = 0x804883dc,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x80488348,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
        },
};

// Twilight Princess (USA). Its sound layer, Z2, sits on JAudio2. The game loads its wave groups as its scenes require
// them. The driver (driver/twilight_princess.c) runs ntdlib's start-up of Z2. Its parameters are ntdlib's z2::Profile.
// An effect's handle points at the JAISound inside its JAISe, 0x10 bytes in: its vtable is the JAISe's + 8, and its
// root track is 0xb8 bytes further.
const GameProfile kTwilightPrincessUsa = {
    .name = "The Legend of Zelda: Twilight Princess",
    .game_id = "GZ2E01",
    .dol_sha1 = "4997d93b9692620c40e90374a0f1dbf0e4889395",
    .main = 0x80006454,
    .driver = Driver::kTwilightPrincess,
    .sound_archive = "Audiores/Z2Sound.baa",
    .sequence_archive = "Audiores/Seqs/Z2SoundSeqs.arc",
    .wave_folder = "Audiores/Waves/",
    .params =
        ntdlib::flipper::z2::Profile{
            .frame_retraces = 2, // the game's frames are a 30th of a second
            .max_heaps = 1,
            .system_heap_size = 0x400000,
            .aram_audio_size = 0xa00000,
            .aram_graph_size = 0xffffffff, // the rest of ARAM
            .aram_priorities = 8 << 16 | 7 << 8 | 6,
            .aram_buffer_size = 0x2000,
            .command_heap_size = 0x1000,
            .archive_heap_size = 0x8df400,
            .zelda_heap_margin = 0x10000,
            .audio_heap_size = 0x14d800,
            .vi_init = 0x8034c514,
            .vi_wait_for_retrace = 0x8034c9c4,
            .get_current_thread = 0x80340c84,
            .get_thread_priority = 0x80341e5c,
            .create_root_heap = 0x802cedb4,     // JKRExpHeap::createRoot
            .create_exp_heap = 0x802cee2c,      // JKRExpHeap::create
            .create_solid_heap = 0x802d0a24,    // JKRSolidHeap::create
            .create_aram = 0x802d1fa4,          // JKRAram::create
            .set_aram_buffer = 0x802d4034,      // JKRAramStream::setTransBuffer
            .create_thread_switch = 0x802d1a14, // JKRThreadSwitch::createManager
            .create_command_heap = 0x8000ed88,  // mDoExt_createCommandHeap
            .create_archive_heap = 0x8000edb8,  // mDoExt_createArchiveHeap
            .archive_heap_parent = 0,           // the root heap
            .create_assert_heap = 0x8000ecc0,   // mDoExt_createAssertHeap
            .create_zelda_heap = 0x8000ed58,    // mDoExt_createZeldaHeap
            .get_free_size = 0x802ce72c,        // JKRHeap::getFreeSize
            .become_current_heap = 0x802ce438,  // JKRHeap::becomeCurrentHeap
            .system_heap = 0x80451370,          // JKRHeap::sSystemHeap
            .alloc = 0x802cec4c,                // operator new
            .thread_ctor = 0x802d16b8,          // JKRThread's constructor for an OSThread
            .create_disc_thread = 0x80015858,   // mDoDvdThd::create
            .audio_heap = 0x80450bbc,           // g_mDoAud_audioHeap
            .audio_setup = 0x80006fc0,          // mDoAud_Create
            .init_flag = 0x80450bb8,            // mDoAud_zelAudio_c::mInitFlag
            .audio_frame = 0x802cd904,          // Z2AudioMgr::gframeProcess
            .load_se_wave = 0x802ba704,         // Z2SceneMgr::loadSeWave
            .load_bgm_wave = 0x802ba770,        // Z2SceneMgr::loadBgmWave
            .wave_status = 0x802ba698,          // Z2SceneMgr::getWaveLoadStatus
            .audio_manager = 0x803dbf4c,        // the audio manager, g_mDoAud_zelAudio
            .start_sound = 0x802cdb1c,          // Z2AudioMgr::startSound
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x802958d4,    // JASSeqParser::parse
            .jump_handler = 0x80294328,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8029436c, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x803c98f4,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x803c9860,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
            // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
            .wave_handle_functions = {0x802988dc, 0x80298e60},
            .wave_bank_table = 0x80450cc0,  // the section heap's instance
            .wave_bank_table_offset = 0xfc, // its table of wave banks
            .wave_bank_count_offset = 0xf8, // the table's count
        },
};

// The Wind Waker (USA). Its sound library is JAudio1, with its start-up archive in AAF. Its waves don't all fit in
// ARAM. The driver loads the waves that stay loaded, and the waves that a .minigcsf names (driver/wind_waker.c). The
// sound library also gets all but 64 KiB of the ARAM that the game gives graphics. A rip draws nothing. The Wind Waker
// HD's waves that stay loaded take more than the game's 10 MiB for sound. The GameCube's sounds play the same with
// either split.
const GameProfile kWindWakerUsa = {
    .name = "The Legend of Zelda: The Wind Waker",
    .game_id = "GZLE01",
    .dol_sha1 = "8d28bab68bb5078c38e43f29206f0bd01f7e7a67",
    .main = 0x80006464,
    .driver = Driver::kWindWaker,
    .library = SoundLibrary::kJAudio1,
    .sound_archive = "Audiores/JaiInit.aaf",
    .sequence_archive = "Audiores/Seqs/JaiSeqs.arc",
    .wave_folder = "Audiores/Banks/",
    .params =
        {
            2,                    // frame_retraces: the game runs at 30 frames a second
            1,                    // max_heaps
            0xa51400,             // archive_heap_size
            0x2ce800,             // game_heap_size
            0x1000,               // command_heap_size
            0xfbe000,             // aram_audio_size: the game's 0xa00000 and most of its 0x5ce000 for graphics
            0x10000,              // aram_graph_size
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x2000,               // aram_buffer_size
            0x166800,             // audio_heap_size
            0x68,                 // thread_size
            0x80313554,           // vi_init
            0x80313a04,           // vi_wait_for_retrace
            0x80307a34,           // get_current_thread
            0x80308d4c,           // get_thread_priority
            0x80302dc0,           // get_arena_lo
            0x80302db8,           // get_arena_hi
            0x802b1558,           // create_root_heap
            0x802b15d0,           // create_exp_heap
            0x802b3290,           // create_solid_heap
            0x802b0868,           // get_free_size
            0x802b03f8,           // become_current_heap
            0x802b42c4,           // create_aram
            0x802b66b8,           // set_aram_buffer
            0x802b0c38,           // alloc
            0x802b3e38,           // thread_ctor
            0x800118f8,           // create_command_heap
            0x800119d4,           // create_archive_heap
            0x80011734,           // create_game_heap
            0x8001181c,           // create_zelda_heap
            0x80017f54,           // create_disc_thread
            0x803f687c,           // audio_heap
            0x80007224,           // audio_process
            0x803f6878,           // audio_ready
            0x803f7578,           // sound_library
            0x802904b4,           // start_sound
            0x803f7584,           // wave_systems
            0x803f758c,           // wave_status
            0x8029165c,           // load_wave_group
            0x803f75fc,           // effects_sequence
            5,                    // sound_state_offset
            0x8028fff8,           // set_camera
            0x803ed050,           // default_camera
        },
    .watch =
        {
            .parse_command = 0x80280268, // the loop over a track's commands in JASystem::TSeqParser::parseSeq
            .jump_handler = 0x8027eacc,  // the sequence parser's command 0xC8, where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 28,
            .jump_target_register = 26,
            .depth_offset = 0xc,
            .wait_offset = 8,
            .note_wait_offset = 0,
            .wave_handle_functions = {0x80285e28, 0x8028670c}, // TBasicWaveBank's and TSimpleWaveBank's getWaveHandle
            .wave_bank_table = 0x803f74d4,                     // JASystem::WaveBankMgr::sWaveBank
            .parameters_offset = 0x3c,
            .parameters_track_offset = 0x1360,
            .effects_sequence = 0x803f75fc,
            .effect_slot_offset = 4,
            .parent_offset = 0x31c,
            .children_offset = 0x320,
        },
};

// The parameters of Mario Golf's driver (driver/mario_golf.c's Params from arena_lo on): the OS's arena, the sizes of
// Camelot's heaps and the heap for a song, and the addresses of the OS's functions and variables and of the game's
// functions. The driver calls the functions of `before_sound` before the game's sound set-up. An entry of 0 calls
// nothing.
struct MarioGolfParams
{
    uint32_t arena_lo = 0, arena_hi = 0, heap1_size = 0, heap7_size = 0, heap0_size = 0, song_heap = 0;
    uint32_t os_get_arena_lo = 0, os_get_arena_hi = 0, os_set_arena_lo = 0, os_set_arena_hi = 0, os_init_alloc = 0,
             os_create_heap = 0, os_set_current_heap = 0, os_alloc_from_heap = 0, os_current_heap = 0,
             os_sleep_thread = 0, vi_init = 0, ar_init = 0, aram_blocks = 0, arq_init = 0;
    uint32_t context_init = 0, context_object = 0, heaps_init = 0, create_heap = 0, select_heap = 0;
    std::array<uint32_t, 4> before_sound = {};
    uint32_t sound_init = 0, load_banks = 0, play_song = 0, play_jingle = 0;

    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;
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
            load_banks,
            play_song,
            play_jingle};
}

// Mario Golf: Toadstool Tour (USA). Its sound library is MusyX, under a layer of Camelot code.
//
// OSInit starts the OS's arena at 0x817DC000, near the top of MEM1. The game's linker set that address. OSInit uses it
// because the boot information holds no start for the arena and the debugger flag (0x800030E8) is clear. GCSF's machine
// puts the arena's top at 0x81700000, below the driver. Without the patch, ClearArena (in OSInit) would clear from the
// arena's start to its top. That size is negative and wraps to almost 4 GiB. The clear would wipe the disc's file
// system table at the top of MEM1 and run on past MEM1's end. The patch makes ClearArena return at once. Emulated
// memory starts cleared. The game's main would set the arena from 0x80550000 to 0x81750000. The driver sets it from
// 0x80550000 to 0x81700000.
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
        }
            .Words(),
    .song_table = 0x80137784,
    .song_count = 38,
    .jingle_table = 0x801379e4,
    .jingle_count = 9,
    .bank_files = {"B/T/M", "B/T/N", "B/T/O", "B/T/P", "D/L/L", "D/L/M", "D/L/N", "D/L/O"},
    .library_name = "MarioGolf",
    .patches = {{0x80030e4c, 0x4e800020}}, // ClearArena: blr
    .track_slots = 0x8025e034,
    .track_slot_size = 0x18,
    .track_slot_count = 16,
    .song_lists = {0x802d84e4, 0x802d84e0},
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
            .load_banks = 0x8001a214,
            .play_song = 0x80019fc4,
            .play_jingle = 0x8001afd8,
        }
            .Words(),
    .song_table = 0x802165e8,
    .song_count = 111,
    .song_name_size = 0x18,
    .bank_files = {"A/O/F", "A/O/G", "A/O/H", "A/O/I", "A/O/J", "A/O/K", "A/O/L", "A/O/M"},
    .library_name = "MarioPowerTennis",
    .patches = {{0x80058404, 0x48000104}}, // OSInit: b past the arena's clearing
    .track_slots = 0x802c9004,
    .track_slot_size = 0x18,
    .track_slot_count = 16,
    .song_lists = {0x8030a22c, 0x8030a228},
};

// WarioWare, Inc.: Mega Party Game$! (USA). Its sound library is MusyX, under a layer of Intelligent Systems' code. The
// parameters follow driver/warioware.c's Params from os_sleep_thread. Every sound of the game is a MusyX song in
// sound_data.bin. The effects are songs too. The game's sound test names 231 songs. Its table's entries are 0x20 bytes:
// the song's ID, its volume and two more bytes, then a name for each of six languages. This release has only the
// English names (the second).
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
    .track_slots = 0x8034d09c,
    .track_slot_size = 0x18,
    .track_slot_count = 64,
    .song_lists = {0x80375a34, 0x80375a30},
};

// Luigi's Mansion (USA). Its sound library is an early JAudio1. The start-up archive is an AAF. The sequences are one
// file, and a table in the AAF divides it. The sound library loads all four wave systems at start-up. The parameters
// follow driver/luigis_mansion.c's Params from frame_retraces.
const GameProfile kLuigisMansionUsa = {
    .name = "Luigi's Mansion",
    .game_id = "GLME01",
    .dol_sha1 = "77d49225e7d28b9777916cdbe5809345c570ecbc",
    .main = 0x80005bdc,
    .driver = Driver::kLuigisMansion,
    .library = SoundLibrary::kJAudio1,
    .sound_archive = "AudioRes/JaiInit.aaf",
    .sequence_archive = "AudioRes/Seqs/JaiArcS.arc",
    .wave_folder = "AudioRes/Banks/",
    .params =
        {
            2,                    // frame_retraces: the game runs at 30 frames a second
            1,                    // max_heaps
            0x6ac000,             // game_heap_size
            0xf00000,             // aram_audio_size
            0xffffffff,           // aram_graph_size: the rest of ARAM
            8 << 16 | 8 << 8 | 8, // aram_priorities
            0xc0000,              // audio_heap_size
            0x20,                 // audio_heap_alignment
            0x801e738c,           // vi_init
            0x801e77c0,           // vi_wait_for_retrace
            0x801e4384,           // dvd_init
            0x804d9674,           // long_file_names
            0x801ce9dc,           // create_root_heap
            0x801cea5c,           // create_exp_heap
            0x801ce280,           // become_system_heap
            0x801ce290,           // become_current_heap
            0x801ce2a0,           // heap_alloc
            0x801d0c30,           // create_aram
            0x80188594,           // audio_setup
            0x80188638,           // load_staying_waves
            0x8018865c,           // audio_frame
            0x804d7828,           // sound_library
            0x64,                 // state_offset
            0x80,                 // wave_systems_offset
            0x60,                 // last_load_offset
            0x8018d6c0,           // start_sound
        },
    .watch =
        {
            .parse_command = 0x801a5270, // the head of the loop over a track's commands in the track's tick
            .jump_handler = 0x801a7864,  // the conditional jump and call (0xC8, 0xC4), where they take the jump
            .command_track_register = 31,
            .jump_track_register = 3,
            .jump_target_register = 30,
            .depth_offset = 8,
            .relative_positions = true,
            .wait_offset = 0x8c,
            .note_wait_offset = 0,
            .parameters_offset = 0x38,
            .parameters_track_offset = 0,
            .root_tracks = 0x8049a448,
            .root_track_count = 16,
            .effects_sequence = 0x803f015c,
            .effect_slot_offset = 0,
            .parent_offset = 0x40,
            .children_offset = 0x44,
            // Many effects hold a note with a wait of 16,000 or 32,000 ticks until the game stops them. At the effects'
            // 96 ticks a second, that's 2 min 47 s or 5 min 33 s.
            .endless_wait = 0x2000,
        },
};

// Super Mario Sunshine (USA). Its sound library is JAudio1, under the game's sound object (MSound). The game keeps its
// start-up archive in data/nintendo.szs. It names its sounds in AudioRes/mSound.asn. Its scenes' waves don't all fit in
// ARAM. The game loads three groups at start-up and before its title, and a scene's group with the scene. Groups of
// that system share some waves, with the same samples. The parameters follow driver/super_mario_sunshine.c's Params
// from frame_retraces.
const GameProfile kSuperMarioSunshineUsa = {
    .name = "Super Mario Sunshine",
    .game_id = "GMSE01",
    .dol_sha1 = "a6782903ef79d4196c8489ecb1b57decb5b3728f",
    .main = 0x80005600,
    .driver = Driver::kSuperMarioSunshine,
    .library = SoundLibrary::kJAudio1,
    .sound_archive = "AudioRes/mSound.aaf",
    .sequence_archive = "AudioRes/Seqs/sequence.arc",
    .wave_folder = "AudioRes/Banks/",
    .packed_archive = "data/nintendo.szs",
    .packed_sound_archive = "msound.aaf",
    .sound_names = "AudioRes/mSound.asn",
    .staying_groups = {0 << 16 | 0, 1 << 16 | 0, 2 << 16 | 16},
    .params =
        {
            2,                    // frame_retraces: the game runs at 30 frames a second
            1,                    // max_heaps
            0x20000,              // system_heap_size
            0xf40000,             // aram_audio_size
            0xffffffff,           // aram_graph_size: the rest of ARAM
            8 << 16 | 8 << 8 | 8, // aram_priorities
            0x8000,               // aram_buffer_size
            0xd8,                 // sound_object_size
            0x000,                // staying_waves[0]: the first wave system's group 0
            0x210,                // staying_waves[1]: the scene system's group 16
            0x100,                // staying_waves[2]: the second wave system's group 0
            0xffffffff,           // staying_waves[3]: none
            2,                    // scene_system
            0x3f400000,           // music_volume: 0.75
            0x8034f20c,           // vi_init
            0x8034f684,           // vi_wait_for_retrace
            0x8034bfcc,           // dvd_init
            0x8040e86c,           // long_file_names
            0x802c0f8c,           // create_root_heap
            0x802c100c,           // create_exp_heap
            0x802c3720,           // become_system_heap
            0x802c3730,           // become_current_heap
            0x802c3740,           // heap_alloc
            0x802bdd2c,           // create_aram
            0x802f11a8,           // set_aram_buffer
            0x802c3ba4,           // alloc
            0x80014e70,           // sound_object_ctor
            0x8040e17c,           // sound_object
            0x8030a2d4,           // set_output_mode
            0x8001569c,           // load_waves
            0x803017b0,           // change_scene_waves
            0x80014da8,           // audio_frame
            0x8040e430,           // sound_library
            0x38,                 // state_offset
            0x64,                 // wave_status_offset
            0x80301e80,           // start_sound
            0x8030a57c,           // set_volume
            8,                    // music_volume_kind
        },
    .watch =
        {
            .parse_command = 0x80321328, // the loop over a track's commands in the track's tick
            .jump_handler = 0x8031fcac,  // the sequence parser's command 0xC8, where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 29,
            .jump_target_register = 27,
            .depth_offset = 0xc,
            .wait_offset = 8,
            .note_wait_offset = 0,
            .wave_handle_functions = {0x8030e40c, 0x8030ed10}, // TBasicWaveBank's and TSimpleWaveBank's getWaveHandle
            .wave_bank_table = 0x8040e544,                     // the wave bank manager's table of banks
            // The sound library takes the table of root tracks and the variable that holds the effects sequence from
            // the root heap at start-up. The driver's start-up is the same for every sound. Their addresses therefore
            // don't change.
            .parameters_offset = 0x38,
            .parameters_track_offset = 0,
            .root_tracks = 0x804afb70,
            .root_track_count = 8,
            .effects_sequence = 0x8044f928,
            .effect_slot_offset = 0,
            .parent_offset = 0x2c0,
            .children_offset = 0x2c4,
            // Some effects hold a note with a wait of 32,767 ticks until the game stops them. At the effects' 96 ticks
            // a second, that's 5 min 41 s. DOLPIC's root track waits 22,080 ticks a pass while its children play.
            .endless_wait = 0x7000,
            // An effect's child track holds a note while its timer routine polls the game every 5 ticks.
            .interrupt_return_offset = 0x44,
        },
};

// Pikmin (USA, disc version 1). Its sound library is an early JAudio1. The game runs it through its C functions (Jac,
// Jaf, Jaq and Jam). The game's code holds the sequence index. The game plays a piece of music by its number in the
// index, in one of two slots, with the wave group of the wave system of the same number. The addresses come from the
// disc's map (dataDir/build.map). The parameters follow driver/pikmin.c's Params from frame_retraces.
const GameProfile kPikminUsa = {
    .name = "Pikmin",
    .game_id = "GPIE01",
    .dol_sha1 = "02204260b7efe8742d34572e58ba3dfecd92e4e9",
    .main = 0x80005560,
    .driver = Driver::kPikmin,
    .library = SoundLibrary::kJac,
    .sound_archive = "dataDir/SndData/Banks/pikibank.bx",
    .sequence_archive = "dataDir/SndData/Seqs/pikiseq.arc",
    .wave_folder = "dataDir/SndData/Banks/",
    .sequence_index = 0x80226fe0, // HEAD_pikiseq
    // The music of the cutscenes: demobgm, in parts. Jac_InitBgm starts demobgm when the sound library starts. A
    // cutscene's entry in DEMO_STATUS (0x802269b0) gives the part to play. __Prepare_BGM (0x8001aea0) loads the part's
    // group of wave system 13. Part 8 plays when a ship part appears (Jac_PlaySystemSe, 0x80016de0). Part 2 plays bank
    // 14. The waves of that bank are in wave system 14, the diary's. The diary's scene loads them. Parts 3 and 4 play
    // part 5's tracks without a wave group. No cutscene plays them.
    .parted_piece = 13,
    .parts = {{1, 13, 7}, {2, 14, 0}, {5, 13, 3}, {6, 13, 4}, {7, 13, 5}, {8, 13, 6}, {9, 13, 1}, {10, 13, 8}},
    .params =
        {
            1,          // frame_retraces: the driver only polls. The sound library's threads play the music.
            0x80000,    // audio_heap_size: as System::Initialise gives Jac_Start
            0x20,       // audio_heap_alignment
            0x800000,   // aram_audio_size
            0x80398da8, // aram_blocks: the stack that System::Initialise gives ARInit
            3,          // aram_block_count
            0x802a534c, // sound_path: "/dataDir/SndData/"
            0x803e7b0c, // system: gsys
            0x40,       // disc_buffer
            0x3c,       // disc_buffer_size
            0,          // music_slot
            3,          // music_handle: the slot's root sequence (Jac_PlayBgm plays slot n with handle n + 3)
            0x801f59d0, // os_init
            0x803e7fcc, // long_file_names: __DVDLongFileNameFlag. OSInit sets it from bi2.bin.
            0x801ff964, // dvd_init
            0x80202920, // vi_init
            0x80202d58, // vi_wait_for_retrace
            0x80206a40, // ar_init
            0x80207c50, // arq_init
            0x801f6920, // get_arena_lo
            0x801f6930, // set_arena_lo
            0x800171c0, // start: Jac_Start
            0x800173e0, // add_disc_buffer: Jac_AddDVDBuffer
            0x803e7988, // boot_ok
            0x80018be0, // ready_bgm: Jac_ReadyBgm
            0x8000d9e0, // collect_aram: Collect_AramMotherHeap
            0x8000c6e0, // set_wave_scene: WaveScene_Set
            0x80007f80, // check_pass: DVDT_CheckPass
            0x80018c80, // play_bgm: Jac_PlayBgm
            0x8001b780, // handle_to_sequence: Jaf_HandleToSeq
            0x30000,    // part_track: the name that demobgm's root track takes. Jac_StartDemo writes to that track.
            0x80010660, // get_track_handle: Jam_GetTrackHandle
            0x80010380, // write_port: Jam_WritePortAppDirect
        },
    .watch =
        {
            .parse_command = 0x80013130, // the head of the loop over a track's commands in Jam_SeqmainNote
            .jump_handler = 0x800119c4,  // Cmd_CallF, for the jump (0xC8) and the call (0xC4), where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 3,
            .jump_target_register = 30,
            .depth_offset = 8,
            .relative_positions = true,
            .wait_offset = 0x8c,
            .note_wait_offset = 0,
            // The driver's word `sound` points at a word that holds the piece's root sequence.
            .parameters_offset = 0,
            .parameters_track_offset = 0,
            .parent_offset = 0x40,
            .children_offset = 0x44,
        },
};

// Donkey Kong Jungle Beat (USA). Its sound library is JAudio2, in the generation of Twilight Princess's, under the
// game's audio manager. The start-up archive is compressed with Yaz0. The game loads it, decoded, into its game heap,
// and the sequence archive, the chord archive and the chord database into its audio heap. Its 63 wave banks hold
// 27.4 MiB of waves. The game keeps six banks loaded and loads the others per stage. The game's BGM player starts the
// music. It sets each track of a BGM by tables in the game's code, and the driver starts music the same way. The
// parameters follow driver/jungle_beat.c's Params from frame_retraces.
const GameProfile kJungleBeatUsa = {
    .name = "Donkey Kong Jungle Beat",
    .game_id = "GYBE01",
    .dol_sha1 = "9471e979610e7e3097526fe8719fca4867d4dd8d",
    .main = 0x8020f0ec,
    .driver = Driver::kJungleBeat,
    .sound_archive = "AudioRes/Donkey.szs",
    .sequence_archive = "AudioRes/Seqs/JaiSeq.arc",
    .wave_folder = "AudioRes/Banks/",
    .other_sound_files = {"AudioRes/Info/JaiChord.arc", "AudioRes/Info/ChordSeDataBase.csd"},
    .params =
        {
            1,                    // frame_retraces: the game runs at 60 frames a second
            0x800000,             // aram_audio_size
            0x600000,             // aram_graph_size
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x7c,                 // thread_size
            0x70,                 // archive_size: a JKRMemArchive
            0x802e82d8,           // vi_init: VIInit
            0x802e8788,           // vi_wait_for_retrace: VIWaitForRetrace
            0x802ddd84,           // get_current_thread: OSGetCurrentThread
            0x80204418,           // start_jsystem: OSInit, DVDInit and JKRExpHeap::createRoot
            0x8020f16c,           // create_heaps: the system heap, the game heap and the audio heap
            0x803f2efc,           // heaps
            0x80257cfc,           // become_current_heap: JKRHeap::becomeCurrentHeap
            0x803f347c,           // current_heap: JKRHeap::sCurrentHeap
            0x8025bee0,           // create_aram: JKRAram::create
            0x802582f8,           // alloc: operator new
            0x80258344,           // alloc_from: operator new with a heap
            0x802583bc,           // alloc_array_from: operator new[] with a heap
            0x8025840c,           // free_array: operator delete[]
            0x8025b670,           // thread_ctor: JKRThread's constructor for an OSThread
            0x802e460c,           // path_to_entry: DVDConvertPathToEntrynum
            0x80239808,           // file_size
            0x80262890,           // load_file: JKRDvdRipper::loadToMainRAM
            0x803b2938,           // sound_archive_path: "/AudioRes/Donkey.szs"
            0x803b29b8,           // sequence_archive_path: "/AudioRes/Seqs/JaiSeq.arc"
            0x803b2a38,           // chord_archive_path: "/AudioRes/Info/JaiChord.arc"
            0x803b2ab8,           // chord_database_path: "/AudioRes/Info/ChordSeDataBase.csd"
            0x8025f814,           // archive_ctor: JKRMemArchive's constructor
            0x8023e6bc,           // create_audio_manager
            0x8023f390,           // audio_frame: the audio manager's frame process
            0x803f0dc0,           // section_heap: JASGlobalInstance<JAUSectionHeap>::sInstance
            0xfc,                 // wave_banks_offset
            0x8001e430,           // load_wave_arcs: JAUSection::loadWaveArc with a mask of archives
            0x4c,                 // wave_arc_state_offset
            1,                    // music_section: music's sound IDs are 0x01xxxxxx
            0xd34,                // music_player_offset: the audio manager's BGM player
            0x80351e58,           // music_tables: each BGM's tables of track states
            0x803533bc,           // music_states: the second table's states
            0x80253ef8,           // set_music_state: the BGM player's function for a state of the second table
        },
    .watch =
        {
            .parse_command = 0x8000bb9c,    // JASSeqParser::parse
            .jump_handler = 0x8000a628,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8000a66c, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x8035cfec,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x8035cf40,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
            // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
            .wave_handle_functions = {0x8000f178, 0x8000f7d4},
            .wave_bank_table = 0x803f0dc0,  // the section heap's instance
            .wave_bank_table_offset = 0xfc, // its table of wave banks
            .wave_bank_count_offset = 0xf8, // the table's count
        },
};

// Mario Kart: Double Dash!! (USA). Its sound library is the earliest known build of JAudio2, under the game's audio
// object. The start-up archive's sound table has the first layouts. The archive holds the music's sequences as blocks.
// The game has no sequence archive. Its three wave banks hold 6.5 MiB of waves. The game keeps them loaded. Its voices
// come from three more start-up archives inside the first one. The game loads one of them at a time. The driver loads
// none. The parameters follow driver/double_dash.c's Params from frame_retraces.
const GameProfile kDoubleDashUsa = {
    .name = "Mario Kart: Double Dash!!",
    .game_id = "GM4E01",
    .dol_sha1 = "e389e50357d4a5076ab3d38672204658f411123d",
    .main = 0x801cc6a8,
    .driver = Driver::kDoubleDash,
    .sound_archive = "AudioRes/GCKart.baa",
    .wave_folder = "AudioRes/Waves/",
    .first_layouts = true,
    .params =
        {
            1,                    // frame_retraces: the game runs at 60 frames a second
            2,                    // max_heaps
            0xe0000,              // system_heap_size
            0xa20000,             // aram_audio_size
            0xffffffff,           // aram_graph_size: the rest of ARAM
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x7c,                 // thread_size
            0x8000,               // aram_buffer_size
            0xc0000,              // audio_heap_size
            0x800e5b8c,           // os_init: OSInit
            0x800abe14,           // dvd_init: DVDInit
            0x800af1d8,           // vi_init: VIInit
            0x800af688,           // vi_wait_for_retrace: VIWaitForRetrace
            0x800eb178,           // get_current_thread: OSGetCurrentThread
            0x80012728,           // create_root_heap: JKRExpHeap::createRoot
            0x800127a8,           // create_exp_heap: JKRExpHeap::create
            0x80015f40,           // create_solid_heap: JKRSolidHeap::create
            0x8000a540,           // create_aram: JKRAram::create
            0x8000ce50,           // set_aram_buffer: JKRAramStream::setTransBuffer
            0x800155a8,           // alloc: operator new
            0x800157a8,           // free: operator delete
            0x800169d4,           // thread_ctor: JKRThread's constructor for an OSThread
            0x800ab56c,           // path_to_entry: DVDConvertPathToEntrynum
            0x80011a44,           // load_file: JKRDvdRipper::loadToMainRAM
            0x80336e30,           // sound_archive_path: "AudioRes/GCKart.baa"
            0x803cbd98,           // audio_heap
            0x803cb658,           // audio
            0x80114740,           // audio_start: the audio object's first step
            0x80113ddc,           // audio_setup: the audio object's set-up
            0x80114878,           // audio_frame: the audio object's frame process
            0,                    // sound_manager_offset
            0x14,                 // section_offset
            0x800a77d0,           // load_wave_bank: JAUSection::loadWaveArc
            0x30,                 // section_data_offset
            0xec,                 // wave_banks_offset
            0x4c,                 // wave_arc_state_offset
            0xc,                  // sound_info_offset
            0x8011fde8,           // is_level_sound: an effect whose entry's first byte isn't 0
        },
    .watch =
        {
            .parse_command = 0x8009d090,    // JASSeqParser::parse
            .jump_handler = 0x8009bd84,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8009be80, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x80341754,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x803416c0,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
        },
};

// The sound information of Pikmin 2's pieces that don't take initMainBgm's (ScenePiece::kAreaInfo): initBossBgm's,
// initAdditionalBgm's and the key's music's.
constexpr ScenePiece::SoundInfo kBossInfo = {0, 127u << 24 | 2u << 16, 0x3f800000, 40u << 24};
constexpr ScenePiece::SoundInfo kExtraInfo = {0, 127u << 24 | 1u << 16, 0x3f800000, 50u << 24};
constexpr ScenePiece::SoundInfo kKeyInfo = {0x1f00, 127u << 24 | 4u << 16, 0x3f800000, 35u << 24};

// Pikmin 2's music: every file of the sequence archive but the effects sequence, se.bms, in the archive's order. A
// piece's sound ID is 0x80000000 and its place here.
//
// The game makes most pieces' objects in PikSceneMgr::initMainBgm, initBossBgm and initAdditionalBgm. A piece's scene
// loads one of the wave system's 49 groups (wScene_*.aw) besides group 0. A cave floor's conductor comes from a list
// for each area (BgmList_*.txt). The conductor picks the phrases that the cave music's tracks play. Its name gives the
// floor's wave group. A rip plays one conductor: the first of each family, such as caveconc_00_0.cnd and new_00_0.cnd.
// The game's code names caveconc_00_0.cnd itself, for a boss's floor. A boss starts its music when it appears, at a
// part of the music: the start point. The middle bosses start m_boss at their appearance (9). The final boss starts
// l_boss at its part for four weapons (1). At start point 0, a track of l_boss jumps to itself without end.
//
// Neither the game's code nor its lists of music start 14 of the files: 2pbattle, a_iron, a_zawinul01, caveboss,
// c_result, the six ff_ pieces, id_test, result and u_result. The game plays streams of the same names in place of
// c_result, result, u_result and the ff_ pieces. The calls that each sequence makes to the game choose its object.
// 2pbattle calls as battle_t does. a_iron and caveboss call as the caves' pieces do. They play with iron.cnd and
// caveboss.cnd. The game has no track map for them, and they play without directors. a_zawinul01 has no conductor of
// its own and plays with iron.cnd. id_test calls as a directed piece does, and plays without directors. The others
// only play. The ripper finds the wave groups of these pieces.
const ScenePiece kPikmin2Pieces[] = {
    {"2pbattle.bms", ScenePiece::Kind::kBattle, ScenePiece::kFindWaves},
    {"a_iron.bms", ScenePiece::Kind::kAutoFree, ScenePiece::kFindWaves, "iron.cnd"},
    {"a_zawinul01.bms", ScenePiece::Kind::kAutoFree, ScenePiece::kFindWaves, "iron.cnd"},
    {"battle_t.bms", ScenePiece::Kind::kDirected, 6},
    {"book.bms", ScenePiece::Kind::kPlain, 36},
    {"caveboss.bms", ScenePiece::Kind::kAutoFree, ScenePiece::kFindWaves, "caveboss.cnd"},
    {"caveconc.bms", ScenePiece::Kind::kAuto, 26, "caveconc_00_0.cnd"},
    {"caveglass.bms", ScenePiece::Kind::kAuto, 23, "caveglass_00_0.cnd"},
    {"cavekeyget.bms", ScenePiece::Kind::kAuto, 5, "cavekeyget.cnd", kKeyInfo},
    {"cavemetal.bms", ScenePiece::Kind::kAuto, 10, "cavemetal_00_0.cnd"},
    {"caverelax.bms", ScenePiece::Kind::kAuto, 28, "caverelax.cnd"},
    {"cavesoil.bms", ScenePiece::Kind::kAuto, 22, "cavesoil_00_0.cnd"},
    {"cavetile.bms", ScenePiece::Kind::kAuto, 24, "cavetile_00_0.cnd"},
    {"cavetsumiki.bms", ScenePiece::Kind::kAuto, 25, "cavetsumiki_00_0.cnd"},
    {"comp_result.bms", ScenePiece::Kind::kPlain, 19},
    {"c_menu.bms", ScenePiece::Kind::kPlain, 20},
    {"c_result.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_annihi.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_doped.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_doping.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_keyget.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_playerdown.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"ff_treasureget.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"forest.bms", ScenePiece::Kind::kMain, 2},
    {"f_result.bms", ScenePiece::Kind::kPlain, 19},
    {"hiscore.bms", ScenePiece::Kind::kPlain, 21, "", kExtraInfo},
    {"id_test.bms", ScenePiece::Kind::kFree, ScenePiece::kFindWaves},
    {"kuro_post.bms", ScenePiece::Kind::kPlain, 48, "", kExtraInfo},
    {"kuro_pre.bms", ScenePiece::Kind::kPlain, 48},
    {"last.bms", ScenePiece::Kind::kMain, 4, "", ScenePiece::kAreaInfo, 30},
    {"l_boss.bms", ScenePiece::Kind::kDirected, 35, "", kBossInfo, 60, 1},
    {"m_boss.bms", ScenePiece::Kind::kDirected, 0, "", kBossInfo, 60, 9},
    {"new_00.bms", ScenePiece::Kind::kAuto, 37, "new_00_0.cnd"},
    {"new_01.bms", ScenePiece::Kind::kAuto, 38, "new_01_0.cnd"},
    {"new_02.bms", ScenePiece::Kind::kAuto, 39, "new_02_0.cnd"},
    {"new_03.bms", ScenePiece::Kind::kAuto, 40, "new_03_0.cnd"},
    {"new_04.bms", ScenePiece::Kind::kAuto, 41, "new_04_0.cnd"},
    {"new_05.bms", ScenePiece::Kind::kAuto, 42, "new_05_0.cnd"},
    {"new_06.bms", ScenePiece::Kind::kAuto, 43, "new_06_0.cnd"},
    {"new_07.bms", ScenePiece::Kind::kAuto, 44, "new_07_0.cnd"},
    {"new_08.bms", ScenePiece::Kind::kAuto, 45, "new_08_0.cnd"},
    {"n_tutorial.bms", ScenePiece::Kind::kMain, 15},
    {"n_tutorial_1stday.bms", ScenePiece::Kind::kPlain, 15, "", kExtraInfo},
    {"result.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"u_result.bms", ScenePiece::Kind::kPlain, ScenePiece::kFindWaves},
    {"worldmap.bms", ScenePiece::Kind::kPlain, 16},
    {"worldmap_intro.bms", ScenePiece::Kind::kPlain, 16},
    {"yakushima.bms", ScenePiece::Kind::kMain, 3, "", ScenePiece::kAreaInfo, 30},
};

// Pikmin 2 (USA). Its sound library is a later JAudio1, under the game's sound layer (PSSystem, PSGame and PSM). The
// start-up archive is an AAF. The game starts its music by file name, through the objects of its sound scenes
// (driver/pikmin2.c). The sound table lists its effects, and PSound.asn names them. Its one wave system has 49 groups,
// one .aw each: wScene_0.aw holds most effects and stays loaded, and a scene loads at most two others. The parameters
// follow driver/pikmin2.c's Params from frame_retraces to rappas. The table of pieces (kPikmin2Pieces) follows them.
const GameProfile kPikmin2Usa = {
    .name = "Pikmin 2",
    .game_id = "GPVE01",
    .dol_sha1 = "90d328bf8f190c90472e8c19e7e53c6ad0fe0d1a",
    .main = 0x804111b4,
    .driver = Driver::kPikmin2,
    .library = SoundLibrary::kJAudio1,
    .sound_archive = "AudioRes/PSound.aaf",
    .sequence_archive = "AudioRes/Seqs/Seq.arc",
    .wave_folder = "AudioRes/Banks/",
    .sound_names = "AudioRes/PSound.asn",
    .other_files = {"AudioRes/Banks/wScene_0.aw", "AudioRes/Conductor.arc", "AudioRes/Key.arc",
                    "user/Totaka/BgmList.txt", "user/Totaka/StreamList.txt", "user/Totaka/trackMap_Cond_T.txt",
                    "user/Totaka/trackMap_Seq_T.txt", "user/Wakai/trackMap_Cond_W.txt",
                    "user/Wakai/trackMap_Seq_W.txt"},
    .params =
        {
            2,                    // frame_retraces: the game's areas and caves run at 30 frames a second
            1,                    // max_heaps
            0xa0000,              // system_heap_size
            0x900000,             // aram_audio_size
            0xffffffff,           // aram_graph_size: the rest of ARAM
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x7c,                 // thread_size
            0x200000,             // heap_margin
            0xd4800,              // file_heap_size: the game's heap for 2D resources
            0x1c,                 // factory_size
            0x900000,             // sound_memory
            0x38,                 // scene_info_size
            17,                   // scene_type: the challenge mode's menu
            0x24,                 // battle_directors_size
            0xb8,                 // directed_bgm_size
            0x350,                // auto_bgm_size
            0x800d0d84,           // vi_init
            0x800d1234,           // vi_wait_for_retrace
            0x800f19c4,           // get_current_thread
            0x800efb30,           // init_mutex
            0x8001fe48,           // create_root_heap
            0x8001fec8,           // create_exp_heap
            0x80024d70,           // create_solid_heap
            0x800237b4,           // get_free_size
            0x800235a4,           // become_current_heap
            0x800235b4,           // destroy_heap
            0x80024f48,           // adjust_size
            0x80023ea4,           // alloc
            0x80025790,           // thread_ctor
            0x80017a10,           // create_aram
            0x800219c4,           // mount_file_cache
            0x80023018,           // get_resource
            0x80334ed8,           // factory_ctor
            0x804ebb74,           // factory_vtable
            0x80470f0c,           // make_se
            0x80334f0c,           // new_sound_system
            0x80515f00,           // scene_manager
            0x80515ed8,           // system_interface
            0x8033874c,           // main_loop
            0x80335f4c,           // new_bgm_seq
            0x8045a200,           // new_main_bgm
            0x8045a2ec,           // new_directed_bgm
            0x8045c12c,           // new_auto_bgm
            0x8045a4ac,           // new_game_scene
            0x80341898,           // append_seq
            0x80341714,           // adapt_child_scene
            0x803354b0,           // scene_info_ctor
            0x803328b0,           // directed_bgm_ctor
            0x8033bc04,           // auto_bgm_ctor
            0x80470484,           // battle_directors_ctor
            0x803438d0,           // adapt_directors
            0x803335b0,           // start_jump_bgm
            0x80515ee8,           // conductor_heap
            0x80338670,           // play_system_se
            0x80515b10,           // play_data
            0x80514bc0,           // rappas
        },
    .scene_pieces = kPikmin2Pieces,
    .watch =
        {
            .parse_command = 0x8009e198, // the head of the loop over a track's commands in JASSeqParser::parseSeq
            .jump_handler = 0x8009ce24,  // the sequence parser's command 0xC8, where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 27,
            .jump_target_register = 25,
            .reader_offset = 0xc,
            .depth_offset = 0x18,
            .interrupt_offset = 0x50, // JASSeqCtrl's saved position, +0x44 in the sequence control
            .idle_in_calls = true,    // the root tracks poll the game once a beat in a call that never returns
            .wait_offset = 0x14,
            .note_wait_offset = 0,
            // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
            .wave_handle_functions = {0x8009a134, 0x8009b8fc},
            .wave_bank_table = 0x805150d4, // JASWaveBankMgr::sWaveBank
            .parameters_offset = 0x48,
            .parameters_inline = true,
            .parameters_track_offset = 0x2c4,
            .effects_sequence = 0x80515260, // JAInter::SeMgr::seHandle
            .effect_slot_offset = 0x14,     // JAISound's track number, after its JSULink and its vtable
            .parent_offset = 0x2f8,
            .children_offset = 0x2fc,
        },
};

// Pac-Man Vs. (USA). The game streams its music. Its sequenced sounds are effects. They play through the build of
// JAudio1 that Pikmin 2 uses. The game's copy was compiled with other options. A wrapper in the game's code drives a
// JAIBasic (driver/pacman_vs.c). The start-up archive is an AAF. Its two wave systems, one for the effects and one for
// the narration, stay loaded. The parameters follow driver/pacman_vs.c's Params from frame_retraces to
// effects_sequence.
const GameProfile kPacManVsUsa = {
    .name = "Pac-Man Vs.",
    .game_id = "PRJE01",
    .dol_sha1 = "50cfa8e394dd80087c1d1aa3140496893ac46146",
    .main = 0x8000ca24,
    .driver = Driver::kPacManVs,
    .library = SoundLibrary::kJAudio1,
    .sound_archive = "PROOT/AudioRes/JaiInit.aaf",
    .sequence_archive = "PROOT/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "PROOT/AudioRes/Banks/",
    .params =
        {
            1,                    // frame_retraces: the game runs at 60 frames a second
            0xf00000,             // aram_audio_size
            0xffffffff,           // aram_graph_size: the rest of ARAM
            8 << 16 | 8 << 8 | 8, // aram_priorities
            0x1000,               // aram_buffer_size
            2,                    // wave_systems
            0x15,                 // sound_state_offset
            0x80006d60,           // create_heaps
            0x800c9b2c,           // vi_init
            0x800c9fdc,           // vi_wait_for_retrace
            0x800c6768,           // dvd_init
            0x8000572c,           // create_task_thread
            0x800057d0,           // send_task
            0x80097fd4,           // alloc
            0x8009b5ac,           // create_aram
            0x8009d3d8,           // set_aram_buffer
            0x8009d9e8,           // mount_archive
            0x8003a0c8,           // track_callback
            0x800089c8,           // create_audio_heap
            0x801a3f60,           // audio_heap
            0x8003ba8c,           // set_audio_folder
            0x80122cdc,           // audio_folder: "PROOT/AudioRes/"
            0x80005838,           // load_file
            0x80122cec,           // startup_archive
            0x80122d08,           // sequence_archive
            0x8003ba50,           // set_startup_archive
            0x8003f0cc,           // set_sequence_archive
            0x80008a04,           // sound_init
            0x80008a6c,           // sound_frame
            0x80008aa0,           // play_sound
            0x8003b288,           // waves_loaded
            0x801a43f8,           // effects_sequence
        },
    .watch =
        {
            .parse_command = 0x8002bbd8, // the head of the loop over a track's commands in the sequence parser
            .jump_handler = 0x8002a830,  // the sequence parser's command 0xC8, where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 27,
            .jump_target_register = 25,
            .reader_offset = 0xc,
            .depth_offset = 0x18,
            .interrupt_offset = 0x50,
            .idle_in_calls = true, // an effect's slot track polls in a loop inside the call of the effect's routine
            .wait_offset = 0x14,
            .note_wait_offset = 0,
            .wave_handle_functions = {0x80032c9c, 0x8003358c}, // the basic and simple wave banks' getWaveHandle
            .wave_bank_table = 0x801a42cc,                     // the wave bank manager's table
            .parameters_offset = 0x48,
            .parameters_inline = true,
            .parameters_track_offset = 0x2c4,
            .effects_sequence = 0x801a43f8,
            .effect_slot_offset = 0x14,
            .parent_offset = 0x2f8,
            .children_offset = 0x2fc,
        },
};

// The Legend of Zelda: Collector's Edition (USA). The disc's menu streams its music. Its sequenced sounds are effects.
// They play through a build of JAudio2 close to the one in Mario Kart: Double Dash!!. The menu's start-up reads the
// sound files. The menu's audio set-up loads the one wave archive and returns the sound manager
// (driver/collectors_edition.c). The sound table also lists one piece of music, JA_BGM_DUMMY. The menu's code never
// starts it. The rip leaves out the disc's other programs: the four games that it emulates and a demo of The Wind
// Waker. The parameters follow driver/collectors_edition.c's Params from frame_retraces.
const GameProfile kCollectorsEditionUsa = {
    .name = "The Legend of Zelda: Collector's Edition",
    .game_id = "PZLE01",
    .dol_sha1 = "20dac90fe9a4d94827348028d6c453521542b994",
    .main = 0x80005b60,
    .driver = Driver::kCollectorsEdition,
    .sound_archive = "AudioRes/ZeldaPak.baa",
    .sequence_archive = "AudioRes/Seqs/ZeldaPakSeqs.arc",
    .wave_folder = "AudioRes/Waves/",
    .params =
        {
            1,                    // frame_retraces: the menu runs at 60 frames a second
            1,                    // max_heaps
            0x300000,             // system_heap_size
            0x800000,             // aram_audio_size
            0x600000,             // aram_graph_size
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x7c,                 // thread_size
            0x100000,             // audio_heap_size
            0x8002dff0,           // os_init: OSInit
            0x800368f0,           // dvd_init: DVDInit
            0x80039c7c,           // vi_init: VIInit
            0x8003a12c,           // vi_wait_for_retrace: VIWaitForRetrace
            0x800337b4,           // get_current_thread: OSGetCurrentThread
            0x8006e820,           // create_root_heap: JKRExpHeap::createRoot
            0x8006e8a0,           // create_exp_heap: JKRExpHeap::create
            0x80072020,           // create_solid_heap: JKRSolidHeap::create
            0x800664c4,           // create_aram: JKRAram::create
            0x80071670,           // alloc: operator new
            0x80072e80,           // thread_ctor: JKRThread's constructor for an OSThread
            0x80069a84,           // mount_archive: JKRArchive::mount
            0x8006bdf4,           // load_file: the loader's reader of a whole file
            0x800a96a0,           // sequence_archive_path: "AudioRes/Seqs/ZeldaPakSeqs.arc"
            0x800a96c0,           // sound_archive_path: "AudioRes/ZeldaPak.baa"
            0x800289e0,           // audio_setup
            0x80029540,           // audio_frame: the sound manager's frame work
            0x820,                // section_offset: the section heap in the sound manager
            0x30,                 // section_data_offset
            0xec,                 // wave_banks_offset
            0x4c,                 // wave_arc_state_offset
        },
    .watch =
        {
            .parse_command = 0x800230d4,    // JASSeqParser::parse
            .jump_handler = 0x80021e24,     // the sequence parser's command 0xC7
            .conditional_jump = 0x80021f1c, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x8011e5dc,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x8011e548, // the vtable of the JAISound in a JAISe, after the JAISe's JSULink
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
        },
};

// Final Fantasy Crystal Chronicles (USA). Its sound library is Square's Red driver (RedSound, built 2003-06-17) on the
// SDK's AX, under a thin layer of the game's code, CSound. The game's system set-up (0x80021E74) sets up the memory
// manager, the sound manager and the file manager, in that order. The game's scripts load a piece's wave bank, then
// the piece, then play it, through CSound's LoadWave, LoadBgm and PlayBgm. The game loads its blocks of effects and
// the wave banks 0 and 500 once, through CSound::LoadBlock (0x800C6284). Those banks take Red's first two slots for
// banks. Any effect's track can pick a wave from them by the slot (command 0x21). The game's scripts load an area's
// wave bank and the area's SEP files through LoadWave and LoadSe. Every effect plays through CSound::PlaySe
// (0x800C5C1C), or through a function that places the effect in 3D. The game's calls of PlaySe pass a pan of 0x40, a
// volume of 127 and no fade-in. The names come from the disc's map (game.MAP), a link map of another build. Each
// address was found in main.dol. The parameters follow driver/crystal_chronicles.c's Params from os_sleep_thread.
//
// The memory manager's set-up (0x8001F6E4) gives its heap the arena from 80 KiB above the arena's bottom up to
// 0x81780000, and fills it with 0xAB. That would cover the driver. The patch moves the heap's top to 0x81700000, the
// top of the arena.
//
// The length analysis reads Red's sound controls (p_SoundControlBuffer). The first three play music. A piece's loop
// takes its tracks back to their state at its start in _MusicNoteExecute, at 0x801C5440. The fourth control plays
// effects, on 32 tracks. A track goes back to the start of a loop that never ends in __MidiCtrl_LoopRepeat
// (0x801C6F38).
const GameProfile kCrystalChroniclesUsa = {
    .name = "Final Fantasy Crystal Chronicles",
    .game_id = "GCCE01",
    .dol_sha1 = "ba57add3e93ea97b0a93ed77037173217bc89eaa",
    .main = 0x80019d7c,
    .driver = Driver::kCrystalChronicles,
    .library = SoundLibrary::kRed,
    .wave_folder = "dvd/sound/wave/",
    .params =
        {
            0x80180840, // os_sleep_thread: OSSleepThread
            0x8001f6e4, // memory_init: CMemory::Init
            0x8023b848, // memory_manager
            0x800c7970, // sound_init: CSound::Init
            0x802f1694, // sound_manager
            0x80013b98, // file_init: CFile::Init
            0x8021dd00, // file_manager
            0x80013948, // file_open: CFile::Open
            0x80013790, // file_read: CFile::Read
            0x800135ec, // file_sync: CFile::SyncCompleted
            0x8001364c, // file_close: CFile::Close
            0x801b58f0, // sprintf
            0x801da194, // music_path: "dvd/sound/music/music%03d.bgm"
            0x800c5f20, // load_wave: CSound::LoadWave
            0x800c66b0, // load_bgm: CSound::LoadBgm
            0x800c6630, // play_bgm: CSound::PlayBgm
            0x801da124, // effect_path: "dvd/sound/se/sep/se%06d.sep"
            0x800c6284, // load_block: CSound::LoadBlock
            0x800c6108, // load_se: CSound::LoadSe
            0x800c5c1c, // play_se: CSound::PlaySe
        },
    .library_name = "CrystalChronicles",
    .patches = {{0x8001f764, 0x3c008170}}, // CMemory::Init: lis r0, 0x8170
    .music_folder = "dvd/sound/music/",
    .red_controls = 0x8032e3d0,
    .red_control_size = 0x494,
    .red_music_controls = 3,
    .red_music_offset = 0x470,
    .red_loop = 0x801c5440,
    .block_folder = "dvd/sound/se/block/",
    .effect_folder = "dvd/sound/se/sep/",
    .red_blocks = 4,
    .red_effect_control = 3,
    .red_effect_tracks = 32,
    .red_track_size = 0x154,
    .red_track_loop = 0x801c6f38,
};

// Animal Crossing (USA). Its sound library is Nintendo's sound library for the Nintendo 64, ported to the GameCube. The
// port runs the Nintendo 64's sequence player and synthesis on the CPU, inside an early JAudio1 that mixes their output
// with the DSP's. The game calls it through C functions (Na_*). The sequences, the banks and the samples are in one
// file, `audiorom.img`. The sound library reads all of it into ARAM as it starts. The game's main (0x800060F8) sets up
// the OS, the disc, JSystem and the sound library, and waits until the sound library is up. Then it links the game's
// module. The module calls Na_GameFrame once a frame, and starts its music with Na_BgmStart. The names and addresses
// come from the disc's link map of main.dol (static.map). The parameters follow driver/animal_crossing.c's Params from
// frame_retraces.
//
// The sound library plays the music in sequence player 1 or 3 and the effects in player 0. A piece of music that plays
// the hour holds the arrangements for three kinds of weather. The game mutes the channels of the others with masks
// from the tables BGM_MUTE_TABLE_FINE, _SNOW and _SAKURA. When it rains, the game plays another piece in place of the
// hour's (see N64Weather).
const GameProfile kAnimalCrossingUsa = {
    .name = "Animal Crossing",
    .game_id = "GAFE01",
    .dol_sha1 = "2ae8f56e7791d37e165bd5900921f2269f9515bf",
    .main = 0x800060f8,
    .driver = Driver::kAnimalCrossing,
    .library = SoundLibrary::kNintendo64,
    .sound_archive = "audiorom.img",
    .params =
        {
            2,          // frame_retraces: a frame is two retraces
            3,          // aram_block_count: JKRAram's ARInit takes a list of 3 blocks
            0x800c3140, // start_up_sound: nintendo_hi_0
            0x66a0,     // start_up_sound_size: as the game's sound_initial gives it
            0x80211f90, // players: AG + 0x3788
            0x160,      // player_size
            0x80217bc0, // music_player: sou_now_bgm_handle
            0x80218361, // music_request: sou_bgm_call_buffer
            0x800788a4, // os_init: OSInit
            0x80079074, // os_init_alarm: OSInitAlarm
            0x800850f4, // dvd_init: DVDInit
            0x800882c8, // vi_init: VIInit
            0x80088740, // vi_wait_for_retrace: VIWaitForRetrace
            0x8008c3cc, // ar_init: ARInit
            0x8008cff4, // arq_init: ARQInit
            0x80010c8c, // init_audio: Na_InitAudio
            0x80010d1c, // game_frame: Na_GameFrame
            0x80015548, // check_boot: Na_CheckNeosBoot
            0x80011158, // set_weather: Na_Tenki
            0x80011170, // start_music: Na_BgmStart
            128,        // first_disc: from Sou_Na_BgmStart
            182,        // last_disc
            9,          // disc_stereo: a hi-fi stereo
            0x8001405c, // place_stereo: Na_MDPlayerPos
            0x80015578, // set_room: Na_Museum
            0,          // effects_player: the game's SE_GROUP
            0x38,       // player_channels
            0x800eb670, // effect_slots: sou_trg_se
            6,          // effect_slot_count
            0x20,       // effect_slot_size
            0x8000c784, // start_effect: Sou_TrgStart
            0x800a9838, // music_table: SEQ_TABLE
            92,         // first_song: the boat's sing_1st_part_bgm_no
            111,        // last_song
            112,        // first_second_part: the boat's sing_2nd_part_bgm_no
            121,        // last_second_part
            126,        // empty_piece: its sequence (247) ends at once
            0x158,      // player_ports: as Nas_GroupSeq reads them
        },
    .library_name = "AnimalCrossing",
    // __OK: blr. The sound library calls it once the game's sound data is in ARAM. It plays the start-up screen's sound
    // on the early JAudio1's sequence player: about 7 s of a voice and a chime. The game plays no music over that
    // sound.
    .patches = {{0x8000bb84, 0x4e800020}},
    .music_table = 0x800a9838,    // SEQ_TABLE
    .sequence_table = 0x800cca40, // AudioseqHeaderStart
    .n64_effects = 242,           // the sequence that the game's Sou_InitAudio starts in SE_GROUP
    .n64_effect_banks = 0x188,    // read by the effects channels' scripts
    .n64_players = 0x80211f90,
    .n64_player_count = 5,
    .n64_player_size = 0x160,
    .n64_read_byte = 0x80029a60,                               // Nas_ReadByteData
    .n64_command_reads = {0x8002bbfc, 0x8002ad2c, 0x80029dd0}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x80028ec0,                                    // Common_Com
    .n64_flow_returns = {0x8002bc30, 0x8002adc0, 0x80029e14},
};

// The Pokémon Colosseum Bonus Disc's download program (USA). The Bonus Disc's menu runs it from a TGC,
// tgc/pokedownload.tgc. The program sends Jirachi to a Game Boy Advance. Its sound library is HAL Laboratory's: a
// synthesizer on the SDK's AX, and a driver above it that runs a script for each effect. The program streams its
// music. It plays five effects from pm6fgm.sem, with their samples in pm6fgm.ssm, and pm6fgm.h names them. The TGC
// also holds sfxsem.sem and sfxmain.ssm. No code of the program names them. The SDK's functions were found by their
// version and assertion strings, and the sound library's by the program's calls. The parameters follow
// driver/colosseum_bonus.c's Params from vi_init.
const GameProfile kColosseumBonusUsa = {
    .name = "Pokémon Colosseum Bonus Disc",
    .game_id = "PC6E01",
    .dol_sha1 = "d9da26b12dfc3b6518c50f79988c41a864b227b7",
    .main = 0x8001115c,
    .driver = Driver::kColosseumBonus,
    .library = SoundLibrary::kHal,
    .sound_archive = "pm6fgm.sem",
    .sound_names = "pm6fgm.h",
    .other_sound_files = {"pm6fgm.ssm"},
    .params =
        {
            0x80041244, // vi_init: VIInit
            0x800416f4, // vi_wait_for_retrace: VIWaitForRetrace
            0x80042fbc, // ai_init: AIInit
            0x800436d8, // ar_init: ARInit
            0x80045208, // arq_init: ARQInit
            0x80038314, // os_get_arena_lo: OSGetArenaLo
            0x8003830c, // os_get_arena_hi: OSGetArenaHi
            0x8002b718, // dvd_open: DVDOpen
            0x8002bb18, // dvd_read: DVDReadPrio
            0x8009a24c, // set_allocator: the sound library's allocator
            0x800976d4, // init_sound: the sound library's set-up
            0x800000,   // aram_size: the program's 8 MiB
            0x40,       // entries: the program's 64
            0x20,       // tracks: the program's 32
            0x80096f6c, // set_scripts: the scripts' file, once in memory
            0x800ac804, // scripts_name: "pm6fgm.sem"
            0x8009aa6c, // allocate_bank: a bank in ARAM
            0x513100,   // bank_size: as the program gives it
            0x8009a8ec, // load_bank: the bank's samples, from the disc
            0x800ac810, // bank_name: "pm6fgm.ssm"
            0x80096f28, // start_effect: the function through which the program plays an effect
            0x800976fc, // playing: the count of the effects that play
            0x800f4fc8, // playing_list: the head of their list
        },
    .library_name = "ColosseumBonus",
    .tgc = "tgc/pokedownload.tgc",
    .program_id = "POKE01",
};

// The parameters of the MusyX driver (driver/musyx.c's Params from os_sleep_thread on): the addresses of the game's
// functions for the OS, the disc, the ARAM, the audio interface and MusyX, MusyX's settings, and studio 0's aux
// effects, A then B. A function that the game doesn't call is 0. driver/musyx.c's Params says what each field means.
struct MusyXParams
{
    uint32_t os_sleep_thread = 0, os_get_arena_lo = 0, os_get_arena_hi = 0;
    std::array<uint32_t, 4> startup = {};
    uint32_t dvd_open = 0, dvd_read_async_prio = 0, dvd_get_command_block_status = 0, dvd_close = 0;
    uint32_t ar_init = 0, aram_blocks = 0, aram_block_count = 0, arq_init = 0, ai_init = 0;
    uint32_t snd_set_hooks = 0, snd_init = 0, init_form = 0, voices = 0, music = 0, sfx = 0, studios = 0,
             init_flags = 0, aram_size = 0;
    std::array<uint32_t, 2> after_init = {};
    uint32_t snd_output_mode = 0, output_mode = 0;
    uint32_t snd_push_group = 0, snd_seq_play = 0, seq_play_form = 0, snd_seq_volume = 0, snd_fx_start_ex = 0;
    uint32_t snd_set_aux_processing_callbacks = 0;
    std::array<uint32_t, 2> aux_callback = {}, aux_user = {}, aux_prepare = {}, aux_midi = {};
    uint32_t snd_volume = 0, snd_fx_ctrl = 0, snd_fx_ctrl14 = 0;

    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;
};

std::vector<uint32_t> MusyXParams::Words() const
{
    return {os_sleep_thread,
            os_get_arena_lo,
            os_get_arena_hi,
            startup[0],
            startup[1],
            startup[2],
            startup[3],
            dvd_open,
            dvd_read_async_prio,
            dvd_get_command_block_status,
            dvd_close,
            ar_init,
            aram_blocks,
            aram_block_count,
            arq_init,
            ai_init,
            snd_set_hooks,
            snd_init,
            init_form,
            voices,
            music,
            sfx,
            studios,
            init_flags,
            aram_size,
            after_init[0],
            after_init[1],
            snd_output_mode,
            output_mode,
            snd_push_group,
            snd_seq_play,
            seq_play_form,
            snd_seq_volume,
            snd_fx_start_ex,
            snd_set_aux_processing_callbacks,
            aux_callback[0],
            aux_callback[1],
            aux_user[0],
            aux_user[1],
            aux_prepare[0],
            aux_prepare[1],
            aux_midi[0],
            aux_midi[1],
            snd_volume,
            snd_fx_ctrl,
            snd_fx_ctrl14};
}

// The places of some of the parameters among the words (kMusyXAuxCallbacks). Words() lists the fields in their order.
static_assert(offsetof(MusyXParams, voices) == kMusyXVoices * 4);
static_assert(offsetof(MusyXParams, studios) == kMusyXStudios * 4);
static_assert(offsetof(MusyXParams, aux_callback) == kMusyXAuxCallbacks * 4);
static_assert(offsetof(MusyXParams, aux_user) == kMusyXAuxBlocks * 4);
static_assert(offsetof(MusyXParams, aux_prepare) == kMusyXAuxPreparations * 4);

// Metroid Prime (USA, disc version 2). Its sound library is MusyX 2.0, under Retro Studios' audio system. Its music
// streams from the disc, apart from the four songs in MidiData.pak.
//
// The game's start-up of the OS (0x802D6D70) also initialises the disc and a timer. The game initialises the ARAM with
// the stack at 0x805A9CFC and keeps its first 0x5FC000 bytes for MusyX. Its audio system starts the audio interface,
// sets MusyX's memory hooks and calls sndInit. sndInit takes 48 voices of each kind, one studio, and the flag that
// encodes the default studio for Dolby Pro Logic II. The output is in surround unless the console is set to mono. A rip
// plays it as on a console set to stereo. The game's start-up then calls sndVolume(127, 0, 0xFF) at 0x800072D4. That
// gives the volume groups of the songs and the effects their full volume.
//
// The length analysis reads the first of MusyX's song instances, at 0x80544A50. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kMetroidPrimeUsa = {
    .name = "Metroid Prime",
    .game_id = "GM8E01",
    .dol_sha1 = "39a2f928159ad46491e9b1ef0a72613d91e0cc40",
    .main = 0x800085c4,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x80386124,
            .os_get_arena_lo = 0x8037f594,
            .os_get_arena_hi = 0x8037f58c,
            .startup = {0x802d6d70}, // the start-up of the OS and the disc
            .dvd_open = 0x803722b4,
            .dvd_read_async_prio = 0x803725c4,
            .dvd_get_command_block_status = 0x80374b04,
            .dvd_close = 0x8037237c,
            .ar_init = 0x8036e720,
            .aram_blocks = 0x805a9cfc,
            .aram_block_count = 2,
            .arq_init = 0x80370230,
            .ai_init = 0x8036e004,
            .snd_set_hooks = 0x803b4f20,
            .snd_init = 0x803b1bd8,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 48,
            .music = 48,
            .sfx = 48,
            .studios = 1,
            .init_flags = 1, // SND_FLAGS_DEFAULT_STUDIO_DPL2
            .aram_size = 0x5fc000,
            .snd_output_mode = 0x8039d460,
            .output_mode = 2, // SND_OUTPUTMODE_SURROUND
            .snd_push_group = 0x803ab444,
            .snd_seq_play = 0x803abbf8, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x8039d124,
            .snd_fx_start_ex = 0x8039d2a8,
            .snd_volume = 0x8039d36c,
        }
            .Words(),
    .library_name = "MetroidPrime",
    .musyx_files = MusyXFiles::kRetro,
    .song_lists = {0x805aaab4, 0x805aaab0},
    .section_loops = 0x80545f6c,
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 48,
                     .synth = 0x805aaae8,
                     .synth_size = 0x404,
                     .dsp = 0x805aabc4,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x803a3d9c},
};

// Tales of Symphonia (USA). Both discs hold the same code and the same music. Its sound library is MusyX 2.0, under
// Namco's sound layer.
//
// OSInit initialises the disc. The game then initialises the ARAM with a stack of 64 entries at 0x802CADD8. Its sound
// set-up (0x800A2100) starts the audio interface, sets MusyX's memory hooks and calls sndInit: 64 voices, 42 for
// music, 22 for effects, one studio, and the ARAM up to 0x783000. The output is in surround unless the console is set
// to mono. A rip plays it as on a console set to stereo. The set-up then gives studio 0 two standard reverbs. Aux A's
// lasts 3.6 s, and aux B's 1 s. The game's play function (0x800A0E24) sets aux A's reverb for each song. A byte for
// each song ID at 0x802108B0 picks the reverb of 3.6 s (1) or one of 2.5 s (2). With 0, aux A keeps its reverb. A rip
// then takes the one of 3.6 s, as at start-up.
//
// The length analysis reads the first of MusyX's song instances, at 0x802FB7E8. Its 16 sections start 0x1508 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kTalesOfSymphoniaUsa = {
    .name = "Tales of Symphonia",
    .game_id = "GQSEAF",
    .dol_sha1 = "01a4ad21219e5635cbca144ea5de0c3dd284dc39",
    .main = 0x80028254,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x800fce04,
            .os_get_arena_lo = 0x800f6164,
            .os_get_arena_hi = 0x800f615c,
            .startup = {0x80100314}, // DVDInit
            .dvd_open = 0x800ffd68,
            .dvd_read_async_prio = 0x80100078,
            .dvd_get_command_block_status = 0x8010245c,
            .dvd_close = 0x800ffe30,
            .ar_init = 0x801078b4,
            .aram_blocks = 0x802cadd8,
            .aram_block_count = 0x40,
            .arq_init = 0x801093c4,
            .ai_init = 0x801072b0,
            .snd_set_hooks = 0x8014fb94,
            .snd_init = 0x8014c2e4,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 64,
            .music = 42,
            .sfx = 22,
            .studios = 1,
            .init_flags = 0,       // SND_FLAGS_DEFAULT
            .aram_size = 0x77f000, // up to 0x783000
            .snd_output_mode = 0x80139000,
            .output_mode = 2, // SND_OUTPUTMODE_SURROUND
            .snd_push_group = 0x8014649c,
            .snd_seq_play = 0x80146c8c, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x80138d24,
            .snd_fx_start_ex = 0x80138e48,
            .snd_set_aux_processing_callbacks = 0x801390ec,
            .aux_callback = {0x80150e34, 0x80150e34}, // sndAuxCallbackReverbSTD for aux A and aux B
            .aux_user = {0x802cc464, 0x802cc310},     // their reverbs
            .aux_prepare = {0x80150ee4, 0x80150ee4},  // sndAuxCallbackPrepareReverbSTD for both
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
        }
            .Words(),
    .bank_files = {"S/inst.snd"},
    .library_name = "TalesOfSymphonia",
    .musyx_files = MusyXFiles::kTales,
    .musyx_songs = 0x801f9830,
    .reverbs =
        {{{.block = 0x802cc464, .coloration = 0.8f, .mix = 0.7f, .time = 3.6f, .damping = 0.6f, .pre_delay = 0.08f},
          {.block = 0x802cc310, .coloration = 1.0f, .mix = 0.5f, .time = 1.0f, .damping = 0.8f, .pre_delay = 0.01f}}},
    .song_reverbs = 0x802108b0,
    .other_reverb =
        {.block = 0x802cc464, .coloration = 0.7f, .mix = 0.7f, .time = 2.5f, .damping = 0.6f, .pre_delay = 0.05f},
    .song_lists = {0x8035ac2c, 0x8035ac28},
    .section_loops = 0x802fcd24,
    .section_loop_size = 0x38,
};

// Star Fox Adventures (USA, disc version 1). Its sound library is MusyX, under Rare's sound layer.
//
// The game's start-up initialises the disc. Its sound set-up (0x80009BF8) initialises the ARAM with a stack of 10
// entries at 0x803369F4. It then starts the audio interface, sets MusyX's memory hooks and calls sndInit: 48 voices, 48
// for music, 24 for effects, one studio, the flag that encodes the default studio for Dolby Pro Logic II, and 16 MiB of
// ARAM. It then sets the same numbers of voices again. The output is in stereo unless the console is set to mono. The
// set-up then gives studio 0's aux A a standard reverb of 3 s and sets MusyX's volumes to their defaults.
//
// The length analysis reads the first of MusyX's song instances, at 0x803B15B0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kStarFoxAdventuresUsa = {
    .name = "Star Fox Adventures",
    .game_id = "GSAE01",
    .dol_sha1 = "8bde4669c75260cc058b8b44ef36bacff3a67d20",
    .main = 0x80021400,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x802471c4,
            .os_get_arena_lo = 0x80241df0,
            .os_get_arena_hi = 0x80241de8,
            .startup = {0x80249958}, // DVDInit
            .dvd_open = 0x80249300,
            .dvd_read_async_prio = 0x80249610,
            .dvd_get_command_block_status = 0x8024ba84,
            .dvd_close = 0x802493c8,
            .ar_init = 0x80250838,
            .aram_blocks = 0x803369f4,
            .aram_block_count = 10,
            .arq_init = 0x80251460,
            .ai_init = 0x8025024c,
            .snd_set_hooks = 0x8028469c,
            .snd_init = 0x802817a8,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 48,
            .music = 48,
            .sfx = 24,
            .studios = 1,
            .init_flags = 1, // SND_FLAGS_DEFAULT_STUDIO_DPL2
            .aram_size = 0x1000000,
            .snd_output_mode = 0x802731c8,
            .output_mode = 1, // SND_OUTPUTMODE_STEREO
            .snd_push_group = 0x8027be90,
            .snd_seq_play = 0x8027c140, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x80272e84,
            .snd_fx_start_ex = 0, // no effects yet
            .snd_set_aux_processing_callbacks = 0x802732c0,
            .aux_callback = {0x80285324, 0}, // sndAuxCallbackReverbSTD for aux A, and nothing for aux B
            .aux_user = {0x803368a0, 0},     // aux A's reverb
            .aux_prepare = {0x80285380, 0},  // sndAuxCallbackPrepareReverbSTD for aux A
            .aux_midi = {0xff, 0xff},        // no MIDI channel controls aux A or aux B
        }
            .Words(),
    .library_name = "StarFoxAdventures",
    .musyx_files = MusyXFiles::kStarFox,
    .musyx_songs = 0x802c5e80,
    .musyx_song_count = 99,
    .reverbs =
        {{{.block = 0x803368a0, .coloration = 0.5f, .mix = 0.9f, .time = 3.0f, .damping = 0.5f, .pre_delay = 0.033f}}},
    .song_lists = {0x803deeb4, 0x803deeb0},
    .section_loops = 0x803b2acc,
    .section_loop_size = 0x38,
};

// Star Wars Rogue Squadron II: Rogue Leader (USA). Its sound library is MusyX.
//
// main.dol is a loader. It holds the OS, MusyX and a second DOL at 0x80100000: the game. The loader maps the game in at
// 0x7FC00000. The driver uses only the loader. Its start-up of the user's code (0x80006B18) initialises the disc, sets
// up the game's heaps over the whole arena and runs the static constructors, most of them the game's. The rip skips
// the heaps and the constructors. OSInit starts the arena at 0x800AE1E0, or at 0x800AC1E0 with no debugger. The rip
// moves both starts to 0x800AE3E0. The game's high-quality reverb takes the room below. The disc's library takes only
// paths in the 8.3 form unless bi2.bin allows long names. The game's bi2.bin does. The rip lets the library take the
// driver's long names without bi2.bin.
//
// The game's sound set-up (0x7FD49254 in the game) sets MusyX's memory hooks and calls sndInit: 56 voices, 40 for
// music, 32 for effects, one studio, the flags for Dolby Pro Logic II, and the ARAM up to 0xA00000. The game starts its
// output in surround. Its options offer only mono besides. The set-up then gives studio 0's aux A MusyX's high-quality
// reverb of 2 s, and turns on the delay between the ears (sndSetITDDefault) for studio 0's music and effects. A rip
// writes that setting into MusyX's table of them at 0x80096474.
//
// Sixty of the game's 124 songs play only a stream: a note whose sample is a buffer of one second that loops. The game
// probably fills the buffers from the disc as the song plays. GCSF doesn't rip streams. Those songs make no sound in a
// rip, and the ripper skips them. Eight more songs play a stream along with their notes. A rip leaves them out.
//
// The length analysis reads the first of MusyX's song instances, at 0x800694F0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kRogueLeaderUsa = {
    .name = "Star Wars Rogue Squadron II: Rogue Leader",
    .game_id = "GSWE64",
    .dol_sha1 = "bc595e8b8c89ec8307ef61658b6cd5c854670d60",
    .main = 0x80006a40,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x8002f010,
            .os_get_arena_lo = 0x8002908c,
            .os_get_arena_hi = 0x80029084,
            .startup = {}, // none: the start-up of the user's code initialises the disc
            .dvd_open = 0x80038e10,
            .dvd_read_async_prio = 0x80039120,
            .dvd_get_command_block_status = 0x8003af74,
            .dvd_close = 0x80038ed8,
            .ar_init = 0x8004014c,
            .aram_blocks = 0x80095ca0,
            .aram_block_count = 2,
            .arq_init = 0x80041364,
            .ai_init = 0x8004193c,
            .snd_set_hooks = 0x80026174,
            .snd_init = 0x8002300c,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 56,
            .music = 40,
            .sfx = 32,
            .studios = 1,
            .init_flags = 3, // SND_FLAGS_DEFAULT_STUDIO_DPL2 and SND_FLAGS_EMITTERGROUPSUSEMAXVOICES
            .aram_size = 0x9fc000,
            .snd_output_mode = 0x80011ff0,
            .output_mode = 2, // SND_OUTPUTMODE_SURROUND
            .snd_push_group = 0x8001cc50,
            .snd_seq_play = 0x8001d040, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x80011b10,
            .snd_fx_start_ex = 0, // no effects yet
            .snd_set_aux_processing_callbacks = 0x80012070,
            .aux_callback = {0x80026ea4, 0}, // sndAuxCallbackReverbHI for aux A, and nothing for aux B
            .aux_user = {0x800ae1e0, 0},     // aux A's reverb
            .aux_prepare = {0x80026f00, 0},  // sndAuxCallbackPrepareReverbHI for aux A
            .aux_midi = {0xff, 0xff},        // no MIDI channel controls aux A or aux B
        }
            .Words(),
    .library_name = "RogueLeader",
    .patches = {{0x80006b2c, 0x4800003c},  // the start-up of the user's code: on from DVDInit to its end
                {0x80027cb8, 0x3863e3e0},  // OSInit: the arena from 0x800AE3E0
                {0x80027cf0, 0x3863e3e0},  // and without a debugger too
                {0x80038bf8, 0x480000a4}}, // the disc's paths: long names (bi2.bin allows them)
    .musyx_files = MusyXFiles::kRogue,
    .musyx_songs = 0x80060c38,
    .musyx_song_count = 114,
    .embedded_dol = 0x80100000,
    .musyx_writes = {{0x80096474, 0x01010000}},
    .musyx_streamed_songs =
        {
            0x0046000b, // The Battle Of Yavin I
            0x0072006d, // The Battle Of Yavin III
            0x005a0030, // Hyperspace
            0x005d0039, // Imperial Attack
            0x0047000c, // The Asteroid Field
            0x004d001b, // Celebration & End Titles
            0x004d001a, // a song that the hall doesn't list
            0x0061003d, // and another
        },
    // The menus play this song at 0x67 in two of their three calls (0x7FCD9A28 and 0x7FD719A8 in the game).
    .musyx_song_volumes = {{0x006e0065, 0x67}},
    .reverbs = {{{.block = 0x800ae1e0,
                  .coloration = 0.67f,
                  .mix = 1.0f,
                  .time = 2.0f,
                  .damping = 0.2f,
                  .pre_delay = 0.04f,
                  .high = true,
                  .crosstalk = 1.0f}}},
    .song_lists = {0x8009646c, 0x80096468},
    .section_loops = 0x8006aa0c,
    .section_loop_size = 0x38,
};

// Pokémon Colosseum (USA). Its sound library is MusyX, under the game's sound layer.
//
// OSInit initialises the disc. The game's main initialises the ARAM with a stack of 2 entries at 0x80478DC0 and starts
// the audio interface. Its sound set-up (0x80166E88) sets MusyX's memory hooks and calls sndInit: 64 voices, 48 for
// music, 16 for effects, one studio, and 0x9FC000 bytes of ARAM. It then writes the settings of reverb preset 0 into
// aux A's high-quality reverb, prepares the reverb and gives it to studio 0's aux A. The output is in stereo unless the
// console is set to mono. The game's module common_rel holds its table of sounds (see MusyXFiles::kColosseum). The
// game's play function (0x80166AB8) sets a song's reverb preset, then plays the song at volume 0x7F. No call in the
// game's code gives a song another volume.
//
// The length analysis reads the first of MusyX's song instances, at 0x804285D0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kColosseumUsa = {
    .name = "Pokémon Colosseum",
    .game_id = "GC6E01",
    .dol_sha1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f",
    .main = 0x800055e0,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x800a238c,
            .os_get_arena_lo = 0x8009afb8,
            .os_get_arena_hi = 0x8009afb0,
            .startup = {}, // none: OSInit initialises the disc
            .dvd_open = 0x800a501c,
            .dvd_read_async_prio = 0x800a532c,
            .dvd_get_command_block_status = 0x800a7728,
            .dvd_close = 0x800a50e4,
            .ar_init = 0x800aca80,
            .aram_blocks = 0x80478dc0,
            .aram_block_count = 2,
            .arq_init = 0x800ae5c0,
            .ai_init = 0x800ac440,
            .snd_set_hooks = 0x801631ac,
            .snd_init = 0x8015fe88,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 64,
            .music = 48,
            .sfx = 16,
            .studios = 1,
            .init_flags = 0, // SND_FLAGS_DEFAULT
            .aram_size = 0x9fc000,
            .snd_output_mode = 0x8014d9bc,
            .output_mode = 1, // SND_OUTPUTMODE_STEREO
            .snd_push_group = 0x80159ef0,
            .snd_seq_play = 0x8015a368, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x8014d6d8,
            .snd_fx_start_ex = 0, // no effects yet
            .snd_set_aux_processing_callbacks = 0x8014daa8,
            .aux_callback = {0x8016442c, 0}, // sndAuxCallbackReverbHI for aux A, and nothing for aux B
            .aux_user = {0x80452500, 0},     // aux A's reverb
            .aux_prepare = {0x801644e0, 0},  // sndAuxCallbackPrepareReverbHI for aux A
            .aux_midi = {0xff, 0xff},        // no MIDI channel controls aux A or aux B
        }
            .Words(),
    .library_name = "PokémonColosseum",
    .musyx_files = MusyXFiles::kColosseum,
    .musyx_songs = 0x141654,
    .musyx_song_count = 1236,
    .musyx_file_table = 0x1133d8,
    .musyx_file_count = 79,
    .musyx_reverb_presets = 0x145044,
    // The settings come from each song's preset.
    .reverbs = {{{.block = 0x80452500, .high = true}}},
    .song_lists = {0x8047af14, 0x8047af10},
    .section_loops = 0x80429aec,
    .section_loop_size = 0x38,
};

// Resident Evil Zero (USA). Both discs hold the same code. Each disc holds the songs of its part of the game, and the
// songs that both parts play. A rip's library therefore takes the disc's number. The game's sound library is MusyX,
// under Capcom's sound layer.
//
// OSInit initialises the disc and lets the disc's library take long names. The game's sound set-up (0x80148DCC)
// initialises the ARAM with a stack of 24 entries at 0x8031B140. It then starts the audio interface, sets MusyX's
// memory hooks and calls sndInit: 64 voices, 32 for music, 32 for effects, one studio, and 10 MiB of ARAM. The output
// is in stereo unless the console is set to mono. The game's options offer only those two. The set-up then gives
// studio 0's aux A and aux B MusyX's high-quality reverb. Aux A's lasts 1.5 s, and aux B's 1 s.
//
// The length analysis reads the first of MusyX's song instances, at 0x8033AA90. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34.
const GameProfile kResidentEvilZeroUsa = {
    .name = "Resident Evil Zero",
    .game_id = "GBZE08",
    .dol_sha1 = "5c10973f82a1946ebad3dcfe1ec56e8623d50404",
    .main = 0x80005600,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x8019f9d4,
            .os_get_arena_lo = 0x80198dcc,
            .os_get_arena_hi = 0x80198dc4,
            .startup = {0x801a5ae0}, // DVDInit
            .dvd_open = 0x801a5540,
            .dvd_read_async_prio = 0x801a58b0,
            .dvd_get_command_block_status = 0x801a7dfc,
            .dvd_close = 0x801a5608,
            .ar_init = 0x801ad1cc,
            .aram_blocks = 0x8031b140,
            .aram_block_count = 0x18,
            .arq_init = 0x801aecdc,
            .ai_init = 0x801acaec,
            .snd_set_hooks = 0x801e6598,
            .snd_init = 0x801e32d8,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 64,
            .music = 32,
            .sfx = 32,
            .studios = 1,
            .init_flags = 0, // SND_FLAGS_DEFAULT
            .aram_size = 0xa00000,
            .snd_output_mode = 0x801cf3ec,
            .output_mode = 1, // SND_OUTPUTMODE_STEREO
            .snd_push_group = 0x801dcc8c,
            .snd_seq_play = 0x801dd440, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x801cf0dc,
            .snd_fx_start_ex = 0x801cf200,
            .snd_set_aux_processing_callbacks = 0x801cf4d8,
            .aux_callback = {0x801e8040, 0x801e8040}, // sndAuxCallbackReverbHI for aux A and aux B
            .aux_user = {0x8031b1a0, 0x8031b380},     // their reverbs
            .aux_prepare = {0x801e809c, 0x801e809c},  // sndAuxCallbackPrepareReverbHI for both
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
        }
            .Words(),
    .library_name = "ResidentEvilZero",
    .library_per_disc = true,
    .musyx_files = MusyXFiles::kResidentEvil,
    .musyx_songs = 0x80243bcc,
    .reverbs = {{{.block = 0x8031b1a0,
                  .coloration = 0.5f,
                  .mix = 0.8f,
                  .time = 1.5f,
                  .damping = 0.5f,
                  .pre_delay = 0.0f,
                  .high = true,
                  .crosstalk = 0.0f},
                 {.block = 0x8031b380,
                  .coloration = 0.5f,
                  .mix = 0.8f,
                  .time = 1.0f,
                  .damping = 1.0f,
                  .pre_delay = 0.0f,
                  .high = true,
                  .crosstalk = 0.0f}}},
    .song_lists = {0x8037a2cc, 0x8037a2c8},
    .section_loops = 0x8033bfac,
    .section_loop_size = 0x38,
};

// Nintendo Puzzle Collection (Japan). Its sound library is MusyX 2.0. The disc holds a menu and three games: Dr. Mario,
// Panel de Pon and Yoshi's Cookie. main.dol holds the OS, the disc's library and MusyX. main.dol loads the menu and
// each game as a module of code (see MusyXFiles::kPuzzleCollection).
//
// OSInit initialises the disc. main.dol's sound set-up (0x80006D5C) initialises the ARAM with a stack of 2 entries at
// 0x800CAF94 and starts the audio interface. The set-up sets MusyX's memory hooks and calls sndInit: 64 voices, 48 for
// music, 16 for effects, one studio, and 8 MiB of ARAM. The game's options set the output to mono, stereo or surround
// (0x800124C8). A rip plays in stereo, the options' default. Each module sets up studio 0's aux effects in the module's
// memory. A rip places the effects' blocks in a part of main.dol's BSS that only the game's graphics use: aux A's at
// 0x80100000, and aux B's at 0x80100200. The menu, Dr. Mario and Panel de Pon call sndVolume(127, 0, 0xFF). The menu
// and Dr. Mario then call sndVolume(110, 0, 0xFF). A song's start sets the song's group again. A rip leaves the second
// call out.
//
// The length analysis reads the first of MusyX's song instances, at 0x80203500. The instance's 16 sections start
// 0x14E8 bytes in, 0x38 bytes apart. Each section holds a count of loops at +0x34.
const GameProfile kPuzzleCollectionJpn = {
    .name = "Nintendo Puzzle Collection",
    .game_id = "GPZJ01",
    .dol_sha1 = "00ab0f4fd4cff96b9087ed1176f9a6e839b57dde",
    .main = 0x80006780,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x80049d7c,
            .os_get_arena_lo = 0x80043cdc,
            .os_get_arena_hi = 0x80043cd4,
            .startup = {}, // none: OSInit initialises the disc
            .dvd_open = 0x8004f50c,
            .dvd_read_async_prio = 0x8004f81c,
            .dvd_get_command_block_status = 0x80051a90,
            .dvd_close = 0x8004f5d4,
            .ar_init = 0x80058db8,
            .aram_blocks = 0x800caf94,
            .aram_block_count = 2,
            .arq_init = 0x8005a8c8,
            .ai_init = 0x8005874c,
            .snd_set_hooks = 0x80034924,
            .snd_init = 0x80031a98,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 64,
            .music = 48,
            .sfx = 16,
            .studios = 1,
            .init_flags = 0, // SND_FLAGS_DEFAULT
            .aram_size = 0x800000,
            .snd_output_mode = 0x80022a5c,
            .output_mode = 1, // SND_OUTPUTMODE_STEREO
            .snd_push_group = 0x8002bfc0,
            .snd_seq_play = 0x8002c3b0, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x8002264c,
            .snd_fx_start_ex = 0, // no effects
            .snd_set_aux_processing_callbacks = 0x80022b54,
            .aux_callback = {0x80035f00, 0x80037088}, // sndAuxCallbackReverbHI and sndAuxCallbackChorus
            .aux_user = {0x80100000, 0x80100200},     // aux A's reverb and aux B's chorus (see above)
            .aux_prepare = {0x80035f5c, 0x800373f4},  // sndAuxCallbackPrepareReverbHI and sndAuxCallbackPrepareChorus
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
            .snd_volume = 0x80022968,
        }
            .Words(),
    .library_name = "PuzzleCollection",
    .musyx_files = MusyXFiles::kPuzzleCollection,
    // The settings that the menu, Dr. Mario and Panel de Pon give aux A's reverb.
    .reverbs = {{{.block = 0x80100000,
                  .coloration = 0.5f,
                  .mix = 0.8f,
                  .time = 1.5f,
                  .damping = 0.5f,
                  .pre_delay = 0.0f,
                  .high = true,
                  .crosstalk = 0.0f}}},
    // Yoshi's Cookie's reverb.
    .other_reverb =
        {.block = 0x80100000, .coloration = 0.5f, .mix = 1.0f, .time = 1.6f, .damping = 1.0f, .pre_delay = 0.02f},
    .standard_reverb = {0x80035594, 0x800355f0}, // sndAuxCallbackReverbSTD and sndAuxCallbackPrepareReverbSTD
    .musyx_voice_limits = 0x8020ff10,            // in the state that sndInit (0x80031A98) sets up
    .song_lists = {0x8022d6fc, 0x8022d6f8},
    .section_loops = 0x80204a1c,
    .section_loop_size = 0x38,
};

// Metroid Prime 2: Echoes (USA). Its sound library is a later build of MusyX 2.0, under Retro Studios' audio system.
// It streams its music. Its MusyX groups are all effects groups.
//
// The game's start-up of the OS (0x8028BF68) also initialises the disc and a timer. The game initialises the ARAM with
// a stack of 3 entries at 0x803C5AB8. Its audio system starts the audio interface, sets MusyX's memory hooks and calls
// sndInit: 48 voices of each kind, three studios, the flag that encodes the default studio for Dolby Pro Logic II, and
// 0x7D200 bytes of ARAM. A static constructor (0x80009864) sets that size. The audio system then activates studios 1
// and 2 and feeds both into aux A of studio 0, for the acoustics of the game's rooms. A rip plays each effect on studio
// 0, without a room's acoustics. The output is in surround unless the console is set to mono. A rip plays it as on a
// console set to stereo. The game's start-up then calls sndVolume(127, 0, 0xFF) at 0x80007F90. That gives the volume
// groups of the effects their full volume.
//
// Before the game pushes a group, it adds a pool of ARAM for the samples of the group's audio group set (sndPoolAdd,
// 0x80397B80) and selects the pool (0x80397C4C). MusyX then uploads the samples into the pool through the game's
// callback (0x803077C0). The samples of the title music don't fit in MusyX's 0x7D200 bytes. A rip gives MusyX 0x400000
// bytes of ARAM instead. MusyX uploads each set's samples there. Where the samples lie in ARAM changes nothing that a
// listener hears.
const GameProfile kMetroidPrime2Usa = {
    .name = "Metroid Prime 2: Echoes",
    .game_id = "G2ME01",
    .dol_sha1 = "6ef9b491d0cc08bc81a124fdedb8bfaec34d0010",
    .main = 0x801efb00,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x80375eac,
            .os_get_arena_lo = 0x8036ebc4,
            .os_get_arena_hi = 0x8036ebbc,
            .startup = {0x8028bf68}, // the start-up of the OS and the disc
            .dvd_open = 0x8036017c,
            .dvd_read_async_prio = 0x8036048c,
            .dvd_get_command_block_status = 0x80362a20,
            .dvd_close = 0x80360244,
            .ar_init = 0x8035533c,
            .aram_blocks = 0x803c5ab8,
            .aram_block_count = 3,
            .arq_init = 0x80356e4c,
            .ai_init = 0x80381b4c,
            .snd_set_hooks = 0x803a1d30,
            .snd_init = 0x8039e168,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 48,
            .music = 48,
            .sfx = 48,
            .studios = 3,
            .init_flags = 1,       // SND_FLAGS_DEFAULT_STUDIO_DPL2
            .aram_size = 0x400000, // in a rip (see above)
            .snd_output_mode = 0x80389a58,
            .output_mode = 2, // SND_OUTPUTMODE_SURROUND
            .snd_push_group = 0x803974dc,
            .snd_seq_play = 0x80397dd4, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x803895a4,
            .snd_fx_start_ex = 0x803897cc,
            .snd_volume = 0x80389964,
        }
            .Words(),
    .library_name = "MetroidPrime2",
    .musyx_files = MusyXFiles::kRetro,
    .musyx_voices = {.count = 48,
                     .synth = 0x8041a228,
                     .synth_size = 0x458,
                     .dsp = 0x8041a320,
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x8038f660},
};

// Paper Mario: The Thousand-Year Door (USA). Its sound library is MusyX 2.0, the build of Metroid Prime 2's. It
// streams its music.
//
// The game has no DVDReadAsyncPrio. The driver reads through DVDReadPrio (0x802A03C0) instead. That one waits for the
// read and takes the priority where DVDReadAsyncPrio takes the callback. The driver passes 0 there.
//
// The game's sound set-up (0x800E239C) initialises the ARAM with a stack of 8 entries, its queue and the audio
// interface. It sets MusyX's memory hooks and calls sndInit: 64 voices, 12 for music, 44 for effects, one studio, the
// flag that encodes the default studio for Dolby Pro Logic II, and 0xBFC000 bytes of ARAM. It gives studio 0's aux A
// MusyX's high-quality reverb of 3 s and aux B MusyX's chorus. It calls sndVolume(127, 0, 0xFF) and sets the output
// to stereo. A rip writes the chorus's settings: a delay of 10 ms, no variation and a period of 500 ms.
const GameProfile kPaperMarioUsa = {
    .name = "Paper Mario: The Thousand-Year Door",
    .game_id = "G8ME01",
    .dol_sha1 = "bc62d5e4674d139fd50d6c05694b34f955e37039",
    .main = 0x800055e0,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x8029d5b0,
            .os_get_arena_lo = 0x8029673c,
            .os_get_arena_hi = 0x80296734,
            .startup = {0x802a0500}, // DVDInit
            .dvd_open = 0x802a00b0,
            .dvd_read_async_prio = 0x802a03c0, // DVDReadPrio, in place of DVDReadAsyncPrio (see above)
            .dvd_get_command_block_status = 0x802a2710,
            .dvd_close = 0x802a0178,
            .ar_init = 0x802a8d3c,
            .aram_blocks = 0x8041eb24,
            .aram_block_count = 8,
            .arq_init = 0x802aa87c,
            .ai_init = 0x802a868c,
            .snd_set_hooks = 0x802915c0,
            .snd_init = 0x8028dacc,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 64,
            .music = 12,
            .sfx = 44,
            .studios = 1,
            .init_flags = 1, // SND_FLAGS_DEFAULT_STUDIO_DPL2
            .aram_size = 0xbfc000,
            .snd_output_mode = 0x8027a300,
            .output_mode = 1, // SND_OUTPUTMODE_STEREO
            .snd_push_group = 0x802877f0,
            .snd_seq_play = 0x80287fe0, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x80279eac,
            .snd_fx_start_ex = 0x8027a0d4,
            .snd_set_aux_processing_callbacks = 0x8027a3ec,
            .aux_callback = {0x80292d78, 0x80294058}, // sndAuxCallbackReverbHI and sndAuxCallbackChorus
            .aux_user = {0x803e194c, 0x803e175c},     // aux A's reverb and aux B's chorus
            .aux_prepare = {0x80292dd4, 0x80294314},  // sndAuxCallbackPrepareReverbHI and sndAuxCallbackPrepareChorus
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
            .snd_volume = 0x8027a2a0,
            .snd_fx_ctrl = 0x80279fd4,
            .snd_fx_ctrl14 = 0x8027a034,
        }
            .Words(),
    .library_name = "PaperMario",
    .musyx_files = MusyXFiles::kPaperMario,
    .musyx_writes = {{0x803e17ec, 10}, {0x803e17f0, 0}, {0x803e17f4, 500}},
    .reverbs = {{{.block = 0x803e194c,
                  .coloration = 0.0f,
                  .mix = 0.8f,
                  .time = 3.0f,
                  .damping = 0.3f,
                  .pre_delay = 0.1f,
                  .high = true,
                  .crosstalk = 0.0f}}},
    .musyx_voices = {.count = 64,
                     .synth = 0x8041ee90,
                     .synth_size = 0x458,
                     .dsp = 0x8041ef88,
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x8027f8cc},
    .musyx_effects = 0x80316b28,
    .musyx_effect_count = 4242,
};

// The parameters of driver/musyx_upload.c: driver/musyx.c's (MusyXParams), then sndSetSampleDataUploadCallback,
// aramSyncTransferQueue, sndMasterVolume and the master volume. driver/musyx.c's Params says what each field means.
struct MusyXUploadParams
{
    MusyXParams musyx;
    uint32_t snd_set_upload_callback = 0, aram_sync = 0, snd_master_volume = 0, master_volume = 0;

    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;
};

std::vector<uint32_t> MusyXUploadParams::Words() const
{
    std::vector<uint32_t> words = musyx.Words();
    words.insert(words.end(), {snd_set_upload_callback, aram_sync, snd_master_volume, master_volume});
    return words;
}

// Pokémon Channel (USA). Its sound library is MusyX 2.0, the build of Metroid Prime's. The game plays its music as
// MusyX effects (see MusyXFiles::kPokemonChannel). One sample file holds every sample of the game, more than main memory
// holds. The game hands MusyX the samples a chunk at a time, through MusyX's upload callback. A rip does the same
// through driver/musyx_upload.c.
//
// OSInit initialises the disc. The game's start-up initialises the ARAM with a stack of 2 entries at 0x8035054C (at
// 0x80034A0C), its queue and the audio interface. The game's sound set-up (0x8001498C) sets MusyX's memory hooks and
// calls sndInit: 64 voices, none for music, 64 for effects, one studio, the flag that encodes the default studio for
// Dolby Pro Logic II, and 0xFFC000 bytes of ARAM. The output is in stereo unless the console is set to mono. The
// set-up then calls sndMasterVolume(100, 100, 1, 1) and sndVolume(127, 100, 0xFF). A rip makes both calls with a time
// of 0. The game gives studio 0 no aux effects. Its loader of groups (0x80015228) sets the upload callback, with chunks
// of 0x20000 bytes, before it pushes a group.
const GameProfile kPokemonChannelUsa = {
    .name = "Pokémon Channel",
    .game_id = "GPAE01",
    .dol_sha1 = "3523501ee45ea3d84e12096616cb05c1b9733c21",
    .main = 0x80036dfc,
    .driver = Driver::kMusyXUpload,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXUploadParams{
            .musyx =
                {
                    .os_sleep_thread = 0x800c9980,
                    .os_get_arena_lo = 0x800c476c,
                    .os_get_arena_hi = 0x800c4764,
                    .startup = {}, // none: OSInit initialises the disc
                    .dvd_open = 0x800cdde8,
                    .dvd_read_async_prio = 0x800ce0f8,
                    .dvd_get_command_block_status = 0x800d0360,
                    .dvd_close = 0x800cdeb0,
                    .ar_init = 0x800d5048,
                    .aram_blocks = 0x8035054c,
                    .aram_block_count = 2,
                    .arq_init = 0x800d6b64,
                    .ai_init = 0x800d4a44,
                    .snd_set_hooks = 0x8012edac,
                    .snd_init = 0x8012bda0,
                    .init_form = 1, // sndInit takes the number of studios
                    .voices = 64,
                    .music = 0,
                    .sfx = 64,
                    .studios = 1,
                    .init_flags = 1, // SND_FLAGS_DEFAULT_STUDIO_DPL2
                    .aram_size = 0xffc000,
                    .snd_output_mode = 0x80119cf0,
                    .output_mode = 1, // SND_OUTPUTMODE_STEREO
                    .snd_push_group = 0x80126348,
                    .snd_seq_play = 0x80126afc, // sndSeqPlayEx
                    .seq_play_form = 1,         // sndSeqPlayEx takes a studio
                    .snd_seq_volume = 0x801199b4,
                    .snd_fx_start_ex = 0x80119b38,
                    .snd_volume = 0x80119bfc,
                    .snd_fx_ctrl = 0x80119a38,
                    .snd_fx_ctrl14 = 0x80119a98,
                },
            .snd_set_upload_callback = 0x80126328,
            .aram_sync = 0x8012f090,
            .snd_master_volume = 0x80119c5c,
            .master_volume = 100,
        }
            .Words(),
    .library_name = "PokémonChannel",
    .musyx_files = MusyXFiles::kPokemonChannel,
    .musyx_songs = 0x8015eb50,
    .musyx_song_count = 38,
    .musyx_voices = {.count = 64,
                     .synth = 0x80350d60,
                     .synth_size = 0x404,
                     .dsp = 0x80350e3c,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x8011eca0},
    .viewer_names = 0x197c3,
    .viewer_effects = 0x4552,
};

// Star Wars Rogue Squadron III: Rebel Strike (USA). Its sound library is MusyX.
//
// main.dol is a loader. It holds the OS and the disc's library. The game's code and data, MusyX among them, are a
// second DOL that main.dol holds packed at 0x80200000. The loader's start-up (0x800040BC, before main) unpacks that DOL
// into ARAM and runs it from 0x7F200000 through the CPU's page table, a page at a time. The rip places the game's DOL
// 16 MB above its addresses, from 0x80200000 to 0x8086EAA0. It replaces that start-up with code that maps 0x7F000000 to
// 0x7FFFFFFF there with IBAT3 and DBAT3. MusyX's state lies in the loader's memory. The rip leaves it at the game's
// addresses. The arena starts at 0x81000000. The disc's library takes only paths in the 8.3 form unless bi2.bin allows
// long names. The rip lets it take the driver's long names.
//
// The game's sound set-up (0x7F5C1140 in the game) sets MusyX's memory hooks and calls sndInit: 56 voices, 20 for
// music, 50 for effects, one studio, the flags for Dolby Pro Logic II, and the ARAM up to 0x500000. The game's hooks
// are two mallocs and a free. The rip patches sndSetHooks to take the driver's malloc for both mallocs. The set-up
// starts the output in surround and turns on the delay between the ears (sndSetITDDefault) for studio 0's music and
// effects. A rip writes that setting into MusyX's table of them at 0x80097C54. The set-up then calls sndVolume(127, 0,
// 0xFF). Each level sets a reverb on studio 0's aux A, or none. The rip sets none.
const GameProfile kRebelStrikeUsa = {
    .name = "Star Wars Rogue Squadron III: Rebel Strike",
    .game_id = "GLRE64",
    .dol_sha1 = "dd1797f6dcdc88abeaecc153b1e76c21e7733cae",
    .main = 0x80004000,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x80026a88,
            .os_get_arena_lo = 0x800216a4,
            .os_get_arena_hi = 0x8002169c,
            .startup = {0x80027dd4}, // DVDInit
            .dvd_open = 0x80027650,
            .dvd_read_async_prio = 0x80027ab4,
            .dvd_get_command_block_status = 0x80029ed8,
            .dvd_close = 0x80027718,
            .ar_init = 0x80030180,
            .aram_blocks = 0, // none: the loader's start-up calls ARInit
            .arq_init = 0x80031c90,
            .ai_init = 0x80032270,
            .snd_set_hooks = 0x7f65a060,
            .snd_init = 0x7f65655c,
            .init_form = 1, // sndInit takes the number of studios
            .voices = 56,
            .music = 20,
            .sfx = 50,
            .studios = 1,
            .init_flags = 3,       // SND_FLAGS_DEFAULT_STUDIO_DPL2 and SND_FLAGS_EMITTERGROUPSUSEMAXVOICES
            .aram_size = 0x4fc000, // 0x500000 less ARAM's base
            .snd_output_mode = 0x7f644b28,
            .output_mode = 2, // SND_OUTPUTMODE_SURROUND
            .snd_push_group = 0x7f653608,
            .snd_seq_play = 0x7f653df8, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x7f6444b0,
            .snd_fx_start_ex = 0x7f6446d8,
            .aux_midi = {0xff, 0xff}, // no MIDI channel controls aux A or aux B
            .snd_volume = 0x7f644ac8,
        }
            .Words(),
    .library_name = "RebelStrike",
    // The loader's start-up maps the game's DOL with IBAT3 and DBAT3 instead of its page table: the 16 MB from
    // 0x7F000000 to 0, cached, read and write.
    .patches = {{0x800040bc, 0x3c607f00},  // lis r3, 0x7f00
                {0x800040c0, 0x606301ff},  // ori r3, r3, 0x1ff
                {0x800040c4, 0x38800002},  // li r4, 2
                {0x800040c8, 0x4c00012c},  // isync
                {0x800040cc, 0x7c9783a6},  // mtspr IBAT3L, r4
                {0x800040d0, 0x7c7683a6},  // mtspr IBAT3U, r3
                {0x800040d4, 0x7c9f83a6},  // mtspr DBAT3L, r4
                {0x800040d8, 0x7c7e83a6},  // mtspr DBAT3U, r3
                {0x800040dc, 0x4c00012c},  // isync
                {0x800040e0, 0x4e800020},  // blr
                {0x8002742c, 0x480000ac},  // the disc's paths: long names
                {0x8065a06c, 0x80030000},  // sndSetHooks: the driver's malloc for both of the game's mallocs
                {0x8065a074, 0x80030004}}, // and its free for the game's free
    .musyx_files = MusyXFiles::kRebelStrike,
    .musyx_writes = {{0x80097c54, 0x01010000}},
    .musyx_voices = {.count = 56,
                     .synth = 0x800a2848,
                     .synth_size = 0x458,
                     .dsp = 0x800a2898,
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x7f64b608},
    .packed_module = 0x80200000,
    .module_offset = 0x01000000,
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
        },
    .bank_files = {"sound/mpgcsnd.msm"},
    .library_name = "MarioParty4",
    .stream_file = "sound/mpgcstr.pdt",
    .sound_test_module = "dll/option.rel",
    .sound_test = 0xac58,
    .sound_test_count = 140,
    .messages = "mess/board_e.dat",
    .song_lists = {0x801d47a4, 0x801d47a0}, // seqActiveRoot and seqPausedRoot
    .section_loops = 0x801ae214,            // in seqInstance
    .section_loop_size = 0x38,
};

// The NDDEMO tech demo. Its disc ID is 00\0E01: a game code of two zeros and a NUL, then E, and the maker code 01. Its
// disc header leaves out the GameCube's magic word. Its sound library is an early MusyX. That library's sndInit takes
// no number of studios.
//
// __start calls OSInit before main. main initialises the disc, the video and the pads, then sets up the demo's audio
// (0x80026B00). The set-up starts the audio interface and initialises the ARAM with a stack of 2 entries at 0x8006D1CC
// and its queue. It sets MusyX's memory hooks and calls sndInit: 48 voices, 40 for music, 8 for effects, no flags and
// 0x800000 bytes of ARAM. It calls sndVolume(127, 0, 0xFF) and leaves the output at MusyX's default. It gives studio
// 0's aux A MusyX's high-quality reverb of 5 s and aux B MusyX's chorus. A rip writes the chorus's settings: a delay of
// 15 ms, no variation and a period of 500 ms.
//
// The demo has no DVDReadAsyncPrio. The driver reads through DVDReadPrio (0x80031708) instead. Paper Mario's profile
// does the same. The demo has no DVDGetCommandBlockStatus either. A getter in the demo's code (0x8001AD24) returns the
// word at +0xC of a command block, the block's state. The driver reads the state through it. The disc's library takes
// only paths in the 8.3 form unless bi2.bin allows long names. The demo's bi2.bin doesn't. The rip lets the library
// take the driver's long names.
//
// The length analysis reads the first of MusyX's song instances, at 0x80082A58. Its 16 sections start 0x14E4 bytes
// into it, 0x2C bytes apart, and each holds its count of loops at +0x26.
const GameProfile kNdDemoDisc = {
    .name = "NDDEMO",
    .game_id = std::string_view("00\0E01", 6),
    .dol_sha1 = "d46883ce81a884b65c6e40d483b196add1dd1bf4",
    .main = 0x800065a0,
    .driver = Driver::kMusyX,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXParams{
            .os_sleep_thread = 0x8002f264,
            .os_get_arena_lo = 0x80028c54,
            .os_get_arena_hi = 0x80028c4c,
            .startup = {0x80031844}, // DVDInit
            .dvd_open = 0x800313f8,
            .dvd_read_async_prio = 0x80031708,          // DVDReadPrio, in place of DVDReadAsyncPrio (see above)
            .dvd_get_command_block_status = 0x8001ad24, // the demo's getter of a block's state (see above)
            .dvd_close = 0x800314c0,
            .ar_init = 0x80037e8c,
            .aram_blocks = 0x8006d1cc,
            .aram_block_count = 2,
            .arq_init = 0x800390a4,
            .ai_init = 0x800378ac,
            .snd_set_hooks = 0x80058458,
            .snd_init = 0x80055588,
            .init_form = 0, // sndInit takes no number of studios
            .voices = 48,
            .music = 40,
            .sfx = 8,
            .aram_size = 0x800000,
            .snd_push_group = 0x8005032c,
            .snd_seq_play = 0x800505d8, // sndSeqPlayEx
            .seq_play_form = 1,         // sndSeqPlayEx takes a studio
            .snd_seq_volume = 0x80047cec,
            .snd_set_aux_processing_callbacks = 0x80047f8c,
            .aux_callback = {0x8005911c, 0x8005a19c}, // sndAuxCallbackReverbHI and sndAuxCallbackChorus
            .aux_user = {0x8006d1f0, 0x8006d3d0},     // aux A's reverb and aux B's chorus
            .aux_prepare = {0x80059178, 0x8005a508},  // sndAuxCallbackPrepareReverbHI and sndAuxCallbackPrepareChorus
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
            .snd_volume = 0x80047e98,
        }
            .Words(),
    .library_name = "NDDEMO",
    .patches = {{0x800311d4, 0x480000ac}}, // the disc's paths: long names
    .musyx_files = MusyXFiles::kNdDemo,
    .musyx_songs = 0x80065394,
    .musyx_song_count = 9,
    .musyx_writes = {{0x8006d460, 15}, {0x8006d464, 0}, {0x8006d468, 500}},
    .reverbs = {{{.block = 0x8006d1f0,
                  .coloration = 0.9f,
                  .mix = 0.5f,
                  .time = 5.0f,
                  .damping = 0.6f,
                  .pre_delay = 0.1f,
                  .high = true,
                  .crosstalk = 0.0f}}},
    .song_lists = {0x800a91ac, 0x800a91a8},
    .section_loops = 0x80083f62,
    .section_loop_size = 0x2c,
};

const std::vector<GameProfile> kProfiles = {kFourSwordsUsa,
                                            kFourSwordsJpn,
                                            kTwilightPrincessUsa,
                                            kWindWakerUsa,
                                            kMarioGolfUsa,
                                            kMarioPowerTennisUsa,
                                            kWarioWareUsa,
                                            kLuigisMansionUsa,
                                            kSuperMarioSunshineUsa,
                                            kPikminUsa,
                                            kJungleBeatUsa,
                                            kDoubleDashUsa,
                                            kPikmin2Usa,
                                            kPacManVsUsa,
                                            kCollectorsEditionUsa,
                                            kCrystalChroniclesUsa,
                                            kAnimalCrossingUsa,
                                            kColosseumBonusUsa,
                                            kMetroidPrimeUsa,
                                            kTalesOfSymphoniaUsa,
                                            kStarFoxAdventuresUsa,
                                            kRogueLeaderUsa,
                                            kColosseumUsa,
                                            kResidentEvilZeroUsa,
                                            kMetroidPrime2Usa,
                                            kPaperMarioUsa,
                                            kRebelStrikeUsa,
                                            kPokemonChannelUsa,
                                            kMarioParty4Usa,
                                            kPuzzleCollectionJpn,
                                            kNdDemoDisc};

} // namespace

const std::vector<GameProfile>& KnownProfiles()
{
    return kProfiles;
}

const GameProfile* FindProfile(const disc::GameFiles& game)
{
    const std::string sha1 = xsflib::Sha1::Hex(xsflib::Sha1::Of(game.dol));
    const std::string id = game.GameId();
    for (const GameProfile& profile : kProfiles)
    {
        const bool named = id == profile.game_id || (profile.program_id && id == profile.program_id);
        if (named && sha1 == profile.dol_sha1)
        {
            return &profile;
        }
    }

    return nullptr;
}

} // namespace gcsf::rip
