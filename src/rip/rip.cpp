// SPDX-License-Identifier: MIT

#include "rip/rip.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ntdlib/dol.h"
#include "ntdlib/flipper/z2_params.h"
#include "ntdlib/jaudio/aaf.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/jaudio/rarc.h"
#include "xsflib/ascii.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/files.h"
#include "xsflib/sha1.h"
#include "xsflib/text.h"

#include "gc/memory.h"
#include "gc/system.h"
#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/driver_blob.h"
#include "rip/driver_collectors_edition_blob.h"
#include "rip/driver_double_dash_blob.h"
#include "rip/driver_jungle_beat_blob.h"
#include "rip/driver_luigis_mansion_blob.h"
#include "rip/driver_mario_golf_blob.h"
#include "rip/driver_pacman_vs_blob.h"
#include "rip/driver_pikmin2_blob.h"
#include "rip/driver_pikmin_blob.h"
#include "rip/driver_super_mario_sunshine_blob.h"
#include "rip/driver_twilight_princess_blob.h"
#include "rip/driver_warioware_blob.h"
#include "rip/driver_zel_blob.h"

namespace gcsf::rip
{
namespace
{

// The driver's parameter block (driver/driver.c): the sound's ID, its handle, then the profile from frame_retraces on.
constexpr uint32_t kParamsSoundOffset = DriverBlock::kSoundOffset;
constexpr uint32_t kParamsHandleOffset = 0x10;
constexpr uint32_t kParamsProfileOffset = 0x14;

// The parameter block of a driver that NamesWaves: after the handle, the number of waves that the .minigcsf names and
// the waves, then the profile's params.
constexpr uint32_t kParamsWavesOffset = 0x14;
constexpr uint32_t kParamsListedProfileOffset = kParamsWavesOffset + 4 + kMaxMiniWaves * 4;

// A driver's image: its blob, the address it's loaded at, its entry and its parameter block.
struct DriverImage
{
    std::span<const uint8_t> blob;
    uint32_t base;
    uint32_t entry;
    uint32_t params;
};

// The image of a profile's driver, or nullopt if GCSF has no such driver yet.
std::optional<DriverImage> ImageOf(Driver driver)
{
    switch (driver)
    {
    case Driver::kFourSwords:
        return DriverImage{kDriverBlob, kDriverBase, kDriverEntry, kDriverParams};
    case Driver::kWindWaker:
        return DriverImage{zel::kDriverBlob, zel::kDriverBase, zel::kDriverEntry, zel::kDriverParams};
    case Driver::kTwilightPrincess:
        return DriverImage{twilight_princess::kDriverBlob, twilight_princess::kDriverBase,
                           twilight_princess::kDriverEntry, twilight_princess::kDriverParams};
    case Driver::kMarioGolf:
        return DriverImage{mario_golf::kDriverBlob, mario_golf::kDriverBase, mario_golf::kDriverEntry,
                           mario_golf::kDriverParams};
    case Driver::kWarioWare:
        return DriverImage{warioware::kDriverBlob, warioware::kDriverBase, warioware::kDriverEntry,
                           warioware::kDriverParams};
    case Driver::kLuigisMansion:
        return DriverImage{luigis_mansion::kDriverBlob, luigis_mansion::kDriverBase, luigis_mansion::kDriverEntry,
                           luigis_mansion::kDriverParams};
    case Driver::kSuperMarioSunshine:
        return DriverImage{super_mario_sunshine::kDriverBlob, super_mario_sunshine::kDriverBase,
                           super_mario_sunshine::kDriverEntry, super_mario_sunshine::kDriverParams};
    case Driver::kPikmin:
        return DriverImage{pikmin::kDriverBlob, pikmin::kDriverBase, pikmin::kDriverEntry, pikmin::kDriverParams};
    case Driver::kJungleBeat:
        return DriverImage{jungle_beat::kDriverBlob, jungle_beat::kDriverBase, jungle_beat::kDriverEntry,
                           jungle_beat::kDriverParams};
    case Driver::kDoubleDash:
        return DriverImage{double_dash::kDriverBlob, double_dash::kDriverBase, double_dash::kDriverEntry,
                           double_dash::kDriverParams};
    case Driver::kPikmin2:
        return DriverImage{pikmin2::kDriverBlob, pikmin2::kDriverBase, pikmin2::kDriverEntry, pikmin2::kDriverParams};
    case Driver::kPacManVs:
        return DriverImage{pacman_vs::kDriverBlob, pacman_vs::kDriverBase, pacman_vs::kDriverEntry,
                           pacman_vs::kDriverParams};
    case Driver::kCollectorsEdition:
        return DriverImage{collectors_edition::kDriverBlob, collectors_edition::kDriverBase,
                           collectors_edition::kDriverEntry, collectors_edition::kDriverParams};
    }

    return std::nullopt;
}

// The words that a .minigcsf or the length analysis writes to the parameter block for a driver that NamesWaves: the
// number of waves, then the waves. Throws std::invalid_argument if there are too many.
std::vector<uint8_t> WaveList(const std::vector<uint32_t>& waves)
{
    if (waves.size() > kMaxMiniWaves)
    {
        throw std::invalid_argument("a sound names " + std::to_string(waves.size()) + " waves, more than " +
                                    std::to_string(kMaxMiniWaves));
    }

    std::vector<uint8_t> words(4 + waves.size() * 4);
    xsflib::StoreBe32(words.data(), static_cast<uint32_t>(waves.size()));
    for (std::size_t i = 0; i < waves.size(); i++)
    {
        xsflib::StoreBe32(words.data() + 4 + i * 4, waves[i]);
    }

    return words;
}

// The length analysis's rules: ntdlib's defaults (a 600 s limit, a 0.5 s tail, two loops and a 10 s fade) at GCSF's
// output rate, 1 ms at a time.
constexpr ntdlib::jaudio::GameLengthRules kLengthRules{.sample_rate = kPlayerSampleRate, .block_frames = 32};

// Four Swords Adventures (USA). Its sound library is JAudio2, in the generation of Twilight Princess's.
const GameProfile kFourSwordsUsa = {
    .name = "The Legend of Zelda: Four Swords Adventures",
    .game_id = "G4SE01",
    .dol_sha1 = "d86b3d9f785566e34264035e140ef3daf3cc19f0",
    .main = 0x801344c0,
    .sound_archive = "GC4Sword_usa/AudioRes/JaiInit.baa",
    .sequence_archive = "GC4Sword_usa/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "GC4Sword_usa/AudioRes/Waves/",
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
    .sequence_vtable = 0x8049175c,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x804916c8,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x80035184, // JASSeqParser::parse
    .jump_handler = 0x80033ec8,  // the sequence parser's command 0xC7
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
    .sequence_vtable = 0x804883dc,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x80488348,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x8003525c, // JASSeqParser::parse
    .jump_handler = 0x80033fa0,  // the sequence parser's command 0xC7
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
    .sequence_vtable = 0x803c98f4,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x803c9860,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x802958d4,                       // JASSeqParser::parse
    .jump_handler = 0x80294328,                        // the sequence parser's command 0xC7
    .conditional_jump = 0x8029436c,                    // its command 0xC8, where it takes the jump
    .wave_handle_functions = {0x802988dc, 0x80298e60}, // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
    .wave_bank_table = 0x80450cc0,                     // the section heap's instance
    .wave_bank_table_offset = 0xfc,                    // its table of wave banks
    .wave_bank_count_offset = 0xf8,                    // the table's count
};

// The Wind Waker (USA). Its sound library is JAudio1, with its start-up archive in AAF. Its waves don't all fit in
// ARAM. The driver loads the waves that stay loaded, and the waves that a .minigcsf names (driver/zel.c). The sound
// library also gets all but 64 KiB of the ARAM that the game gives graphics. A rip draws nothing. The Wind Waker HD's
// waves that stay loaded take more than the game's 10 MiB for sound. The GameCube's sounds play the same with either
// split.
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
    .wait_offset = 8,
    .note_wait_offset = 0,
    .depth_offset = 0xc,
    .parse_command = 0x80280268, // the loop over a track's commands in JASystem::TSeqParser::parseSeq
    .jump_handler = 0x8027eacc,  // the sequence parser's command 0xC8, where it takes the jump
    .command_track_register = 31,
    .jump_track_register = 28,
    .jump_target_register = 26,
    .wave_handle_functions = {0x80285e28, 0x8028670c}, // TBasicWaveBank's and TSimpleWaveBank's getWaveHandle
    .wave_bank_table = 0x803f74d4,                     // JASystem::WaveBankMgr::sWaveBank
    .parameters_offset = 0x3c,
    .parameters_track_offset = 0x1360,
    .effects_sequence = 0x803f75fc,
    .effect_slot_offset = 4,
    .parent_offset = 0x31c,
    .children_offset = 0x320,
};

// Mario Golf: Toadstool Tour (USA). Its sound library is MusyX, under a layer of Camelot code. The parameters follow
// driver/mario_golf.c's Params from arena_lo.
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
        {
            0x80550000, // arena_lo
            0x81700000, // arena_hi
            0x4000,     // heap1_size
            0xc0000,    // heap7_size
            0x1000000,  // heap0_size
            7,          // song_heap
            0x80032d90, // OSGetArenaLo
            0x80032d88, // OSGetArenaHi
            0x80032da0, // OSSetArenaLo
            0x80032d98, // OSSetArenaHi
            0x8003272c, // OSInitAlloc
            0x8003279c, // OSCreateHeap
            0x8003271c, // OSSetCurrentHeap
            0x80032320, // OSAllocFromHeap
            0x802d7b10, // __OSCurrHeap
            0x8003aa58, // OSSleepThread
            0x80055a5c, // VIInit
            0x8002ea00, // ARInit
            0x802d7e4c, // the ARAM allocation stack
            0x8003095c, // ARQInit
            0x80009dc8, // the main context's set-up
            0x8023f9cc, // the main context
            0x8001a510, // Camelot's heap table
            0x8001a400, // Camelot's heap creation
            0x8001a5c4, // Camelot's heap selection
            0x8000b990, // the file loader's set-up
            0x8000e2f4, // the system set-up's steps before the sound
            0x800140c0, 0,
            0x80012ecc, // the sound set-up
            0x80012a9c, // the sound banks
            0x80013230, // playSong
            0x800138ec, // playJingle
        },
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
        {
            0x80580000, // arena_lo
            0x81700000, // arena_hi
            0x90000,    // heap1_size
            0xc0000,    // heap7_size
            0x800000,   // heap0_size
            7,          // song_heap
            0x8005a0bc, // OSGetArenaLo
            0x8005a0b4, // OSGetArenaHi
            0x8005a0cc, // OSSetArenaLo
            0x8005a0c4, // OSSetArenaHi
            0x80059a48, // OSInitAlloc
            0x80059ab8, // OSCreateHeap
            0x80059a38, // OSSetCurrentHeap
            0x8005963c, // OSAllocFromHeap
            0x80309758, // __OSCurrHeap
            0x80062bd0, // OSSleepThread
            0x8007e884, // VIInit
            0x80055a2c, // ARInit
            0x80309960, // the ARAM allocation stack
            0x80057988, // ARQInit
            0x80006a8c, // the main context's set-up
            0x80225bb8, // the main context
            0x80029258, // Camelot's heap table
            0x80029130, // Camelot's heap creation
            0x80029314, // Camelot's heap selection
            0x80008d20, // the file loader's set-up
            0,          0, 0,
            0x8001dff0, // the sound set-up
            0x8001a214, // the sound banks
            0x80019fc4, // playSong
            0x8001afd8, // playJingle
        },
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
            0x800a11f4, // OSSleepThread
            0x8004166c, // the game's heaps
            2,          // max_heaps
            0x80018cf4, // the game's file loader
            0x802663c0, // "gcwario_sound.sdir"
            0x802663d4, // "gcwario_sound.pool"
            0x802663e8, // "gcwario_sound.proj"
            0x802663fc, // "sound_data.bin"
            0x802663ac, // "gcwario_sound.samp"
            0x80296488, // the sound set-up's sample directory
            0x8029648c, // its pool
            0x80296490, // its project
            0x80296494, // its songs
            0x80296484, // its samples
            0x800327b8, // the sound set-up
            0x800348b8, // the game's play function
            1,          // the mode in which the game plays music
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
    .wait_offset = 0x8c,
    .note_wait_offset = 0,
    .depth_offset = 8,
    .relative_positions = true,
    .parse_command = 0x801a5270, // the head of the loop over a track's commands in the track's tick
    .jump_handler = 0x801a7864,  // the conditional jump and call (0xC8, 0xC4), where they take the jump
    .command_track_register = 31,
    .jump_track_register = 3,
    .jump_target_register = 30,
    .parameters_offset = 0x38,
    .parameters_track_offset = 0,
    .root_tracks = 0x8049a448,
    .root_track_count = 16,
    .effects_sequence = 0x803f015c,
    .effect_slot_offset = 0,
    .parent_offset = 0x40,
    .children_offset = 0x44,
    // Many effects hold a note with a wait of 16,000 or 32,000 ticks until the game stops them. At the effects' 96
    // ticks a second, that's 2 min 47 s or 5 min 33 s.
    .endless_wait = 0x2000,
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
            0x000,                // staying_waves: the first wave system's group 0,
            0x210,                // the scene system's group 16,
            0x100,                // the second wave system's group 0
            0xffffffff,           // and no more
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
    .wait_offset = 8,
    .note_wait_offset = 0,
    .depth_offset = 0xc,
    .parse_command = 0x80321328, // the loop over a track's commands in the track's tick
    .jump_handler = 0x8031fcac,  // the sequence parser's command 0xC8, where it takes the jump
    .command_track_register = 31,
    .jump_track_register = 29,
    .jump_target_register = 27,
    .wave_handle_functions = {0x8030e40c, 0x8030ed10}, // TBasicWaveBank's and TSimpleWaveBank's getWaveHandle
    .wave_bank_table = 0x8040e544,                     // the wave bank manager's table of banks
    // The sound library takes the table of root tracks and the variable that holds the effects sequence from the
    // root heap at start-up. The driver's start-up is the same for every sound. Their addresses therefore don't change.
    .parameters_offset = 0x38,
    .parameters_track_offset = 0,
    .root_tracks = 0x804afb70,
    .root_track_count = 8,
    .effects_sequence = 0x8044f928,
    .effect_slot_offset = 0,
    .parent_offset = 0x2c0,
    .children_offset = 0x2c4,
    // Some effects hold a note with a wait of 32,767 ticks until the game stops them. At the effects' 96 ticks a
    // second, that's 5 min 41 s. DOLPIC's root track waits 22,080 ticks a pass while its children play.
    .endless_wait = 0x7000,
    // An effect's child track holds a note while its timer routine polls the game every 5 ticks.
    .interrupt_return_offset = 0x44,
};

// Pikmin (USA, disc version 1). Its sound library is an early JAudio1. The game runs it through its C functions (Jac,
// Jaf, Jaq and Jam). The game's code holds the sequence index. The game plays a piece of music by its number in the
// index, in one of two slots, with the wave group of the wave system of the same number. The addresses come from the
// disc's map (dataDir/build.map). Each was checked against main.dol. The parameters follow driver/pikmin.c's Params
// from frame_retraces.
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
            0x20,       // audio_heap_align
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
    .wait_offset = 0x8c,
    .note_wait_offset = 0,
    .depth_offset = 8,
    .relative_positions = true,
    .parse_command = 0x80013130, // the head of the loop over a track's commands in Jam_SeqmainNote
    .jump_handler = 0x800119c4,  // Cmd_CallF, for the jump (0xC8) and the call (0xC4), where it takes the jump
    .command_track_register = 31,
    .jump_track_register = 3,
    .jump_target_register = 30,
    // The driver's word `sound` points at a word that holds the piece's root sequence.
    .parameters_offset = 0,
    .parameters_track_offset = 0,
    .parent_offset = 0x40,
    .children_offset = 0x44,
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
            0x802e82d8,           // VIInit
            0x802e8788,           // VIWaitForRetrace
            0x802ddd84,           // OSGetCurrentThread
            0x80204418,           // start_jsystem: OSInit, DVDInit and JKRExpHeap::createRoot
            0x8020f16c,           // create_heaps: the system heap, the game heap and the audio heap
            0x803f2efc,           // heaps
            0x80257cfc,           // JKRHeap::becomeCurrentHeap
            0x803f347c,           // JKRHeap::sCurrentHeap
            0x8025bee0,           // JKRAram::create
            0x802582f8,           // operator new
            0x80258344,           // operator new with a heap
            0x802583bc,           // operator new[] with a heap
            0x8025840c,           // operator delete[]
            0x8025b670,           // JKRThread's constructor for an OSThread
            0x802e460c,           // DVDConvertPathToEntrynum
            0x80239808,           // file_size
            0x80262890,           // JKRDvdRipper::loadToMainRAM
            0x803b2938,           // "/AudioRes/Donkey.szs"
            0x803b29b8,           // "/AudioRes/Seqs/JaiSeq.arc"
            0x803b2a38,           // "/AudioRes/Info/JaiChord.arc"
            0x803b2ab8,           // "/AudioRes/Info/ChordSeDataBase.csd"
            0x8025f814,           // JKRMemArchive's constructor
            0x8023e6bc,           // create_audio_manager
            0x8023f390,           // the audio manager's frame process
            0x803f0dc0,           // JASGlobalInstance<JAUSectionHeap>::sInstance
            0xfc,                 // wave_banks_offset
            0x8001e430,           // JAUSection::loadWaveArc with a mask of archives
            0x4c,                 // wave_arc_state_offset
            1,                    // music_section: music's sound IDs are 0x01xxxxxx
            0xd34,                // music_player_offset: the audio manager's BGM player
            0x80351e58,           // music_tables: each BGM's tables of track states
            0x803533bc,           // music_states: the second table's states
            0x80253ef8,           // set_music_state: the BGM player's function for a state of the second table
        },
    .sequence_vtable = 0x8035cfec,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x8035cf40,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x8000bb9c,                       // JASSeqParser::parse
    .jump_handler = 0x8000a628,                        // the sequence parser's command 0xC7
    .conditional_jump = 0x8000a66c,                    // its command 0xC8, where it takes the jump
    .wave_handle_functions = {0x8000f178, 0x8000f7d4}, // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
    .wave_bank_table = 0x803f0dc0,                     // the section heap's instance
    .wave_bank_table_offset = 0xfc,                    // its table of wave banks
    .wave_bank_count_offset = 0xf8,                    // the table's count
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
            0x8000,               // transfer_size
            0xc0000,              // audio_heap_size
            0x800e5b8c,           // OSInit
            0x800abe14,           // DVDInit
            0x800af1d8,           // VIInit
            0x800af688,           // VIWaitForRetrace
            0x800eb178,           // OSGetCurrentThread
            0x80012728,           // JKRExpHeap::createRoot
            0x800127a8,           // JKRExpHeap::create
            0x80015f40,           // JKRSolidHeap::create
            0x8000a540,           // JKRAram::create
            0x8000ce50,           // JKRAramStream::setTransBuffer
            0x800155a8,           // operator new
            0x800157a8,           // operator delete
            0x800169d4,           // JKRThread's constructor for an OSThread
            0x800ab56c,           // DVDConvertPathToEntrynum
            0x80011a44,           // JKRDvdRipper::loadToMainRAM
            0x80336e30,           // "AudioRes/GCKart.baa"
            0x803cbd98,           // the variable that holds the audio heap
            0x803cb658,           // the variable that holds the audio object
            0x80114740,           // audio_start: the audio object's first step
            0x80113ddc,           // audio_setup: the audio object's set-up
            0x80114878,           // the audio object's frame process
            0,                    // sound_manager_offset
            0x14,                 // section_offset
            0x800a77d0,           // JAUSection::loadWaveArc
            0x30,                 // section_data_offset
            0xec,                 // wave_banks_offset
            0x4c,                 // wave_arc_state_offset
            0xc,                  // sound_info_offset
            0x8011fde8,           // is_level_sound: an effect whose entry's first byte isn't 0
        },
    .sequence_vtable = 0x80341754,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x803416c0,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x8009d090,    // JASSeqParser::parse
    .jump_handler = 0x8009bd84,     // the sequence parser's command 0xC7
    .conditional_jump = 0x8009be80, // its command 0xC8, where it takes the jump
};

// How Pikmin 2's driver makes a piece's object: driver/pikmin2.c's PIECE_ values. Each is a way in which the game
// makes a piece's object.
enum class PieceKind : uint32_t
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
using SoundInfoWords = std::array<uint32_t, 4>;
constexpr SoundInfoWords kAreaInfo = {0x1f00, 127u << 24 | 1u << 16, 0x3f800000, 50u << 24}; // initMainBgm's
constexpr SoundInfoWords kBossInfo = {0, 127u << 24 | 2u << 16, 0x3f800000, 40u << 24};      // initBossBgm's
constexpr SoundInfoWords kExtraInfo = {0, 127u << 24 | 1u << 16, 0x3f800000, 50u << 24};     // initAdditionalBgm's
constexpr SoundInfoWords kKeyInfo = {0x1f00, 127u << 24 | 4u << 16, 0x3f800000, 35u << 24};  // the key's music

// The wave scene of a piece that the game never plays. The ripper finds the groups that hold its waves.
constexpr uint8_t kFindWaves = 0xff;

// One of Pikmin 2's pieces of music: its file in the sequence archive, how the game makes its object, the wave scene
// that the game loads for it (a group of the wave system, besides group 0), an AutoBgm's conductor, the object's sound
// information, and the beat interval that the game gives the root track (0 keeps the default). A boss's music then has
// a start point: the part of the music that the boss starts.
struct Pikmin2Piece
{
    const char* sequence;
    PieceKind kind;
    uint8_t wave_scene;
    const char* conductor = "";
    SoundInfoWords info = kAreaInfo;
    uint16_t beat_interval = 0;
    uint8_t start_point = 0;
};

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
const Pikmin2Piece kPikmin2Pieces[] = {
    {"2pbattle.bms", PieceKind::kBattle, kFindWaves},
    {"a_iron.bms", PieceKind::kAutoFree, kFindWaves, "iron.cnd"},
    {"a_zawinul01.bms", PieceKind::kAutoFree, kFindWaves, "iron.cnd"},
    {"battle_t.bms", PieceKind::kDirected, 6},
    {"book.bms", PieceKind::kPlain, 36},
    {"caveboss.bms", PieceKind::kAutoFree, kFindWaves, "caveboss.cnd"},
    {"caveconc.bms", PieceKind::kAuto, 26, "caveconc_00_0.cnd"},
    {"caveglass.bms", PieceKind::kAuto, 23, "caveglass_00_0.cnd"},
    {"cavekeyget.bms", PieceKind::kAuto, 5, "cavekeyget.cnd", kKeyInfo},
    {"cavemetal.bms", PieceKind::kAuto, 10, "cavemetal_00_0.cnd"},
    {"caverelax.bms", PieceKind::kAuto, 28, "caverelax.cnd"},
    {"cavesoil.bms", PieceKind::kAuto, 22, "cavesoil_00_0.cnd"},
    {"cavetile.bms", PieceKind::kAuto, 24, "cavetile_00_0.cnd"},
    {"cavetsumiki.bms", PieceKind::kAuto, 25, "cavetsumiki_00_0.cnd"},
    {"comp_result.bms", PieceKind::kPlain, 19},
    {"c_menu.bms", PieceKind::kPlain, 20},
    {"c_result.bms", PieceKind::kPlain, kFindWaves},
    {"ff_annihi.bms", PieceKind::kPlain, kFindWaves},
    {"ff_doped.bms", PieceKind::kPlain, kFindWaves},
    {"ff_doping.bms", PieceKind::kPlain, kFindWaves},
    {"ff_keyget.bms", PieceKind::kPlain, kFindWaves},
    {"ff_playerdown.bms", PieceKind::kPlain, kFindWaves},
    {"ff_treasureget.bms", PieceKind::kPlain, kFindWaves},
    {"forest.bms", PieceKind::kMain, 2},
    {"f_result.bms", PieceKind::kPlain, 19},
    {"hiscore.bms", PieceKind::kPlain, 21, "", kExtraInfo},
    {"id_test.bms", PieceKind::kFree, kFindWaves},
    {"kuro_post.bms", PieceKind::kPlain, 48, "", kExtraInfo},
    {"kuro_pre.bms", PieceKind::kPlain, 48},
    {"last.bms", PieceKind::kMain, 4, "", kAreaInfo, 30},
    {"l_boss.bms", PieceKind::kDirected, 35, "", kBossInfo, 60, 1},
    {"m_boss.bms", PieceKind::kDirected, 0, "", kBossInfo, 60, 9},
    {"new_00.bms", PieceKind::kAuto, 37, "new_00_0.cnd"},
    {"new_01.bms", PieceKind::kAuto, 38, "new_01_0.cnd"},
    {"new_02.bms", PieceKind::kAuto, 39, "new_02_0.cnd"},
    {"new_03.bms", PieceKind::kAuto, 40, "new_03_0.cnd"},
    {"new_04.bms", PieceKind::kAuto, 41, "new_04_0.cnd"},
    {"new_05.bms", PieceKind::kAuto, 42, "new_05_0.cnd"},
    {"new_06.bms", PieceKind::kAuto, 43, "new_06_0.cnd"},
    {"new_07.bms", PieceKind::kAuto, 44, "new_07_0.cnd"},
    {"new_08.bms", PieceKind::kAuto, 45, "new_08_0.cnd"},
    {"n_tutorial.bms", PieceKind::kMain, 15},
    {"n_tutorial_1stday.bms", PieceKind::kPlain, 15, "", kExtraInfo},
    {"result.bms", PieceKind::kPlain, kFindWaves},
    {"u_result.bms", PieceKind::kPlain, kFindWaves},
    {"worldmap.bms", PieceKind::kPlain, 16},
    {"worldmap_intro.bms", PieceKind::kPlain, 16},
    {"yakushima.bms", PieceKind::kMain, 3, "", kAreaInfo, 30},
};

// The bytes that driver/pikmin2.c's Piece gives each file name.
constexpr std::size_t kPieceNameSize = 24;

// Pikmin 2 (USA). Its sound library is a later JAudio1, under the game's sound layer (PSSystem, PSGame and PSM). The
// start-up archive is an AAF. The game starts its music by file name, through the objects of its sound scenes
// (driver/pikmin2.c). The sound table lists its effects, and PSound.asn names them. Its one wave system has 49 groups,
// one .aw each: wScene_0.aw holds most effects and stays loaded, and a scene loads at most two others. The parameters
// follow driver/pikmin2.c's Params from frame_retraces to rappas. The table of pieces follows them.
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
    .wait_offset = 0x14,
    .note_wait_offset = 0,
    .depth_offset = 0x18,
    .parse_command = 0x8009e198, // the head of the loop over a track's commands in JASSeqParser::parseSeq
    .jump_handler = 0x8009ce24,  // the sequence parser's command 0xC8, where it takes the jump
    .command_track_register = 31,
    .jump_track_register = 27,
    .jump_target_register = 25,
    .wave_handle_functions = {0x8009a134, 0x8009b8fc}, // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
    .wave_bank_table = 0x805150d4,                     // JASWaveBankMgr::sWaveBank
    .reader_offset = 0xc,
    .parameters_inline = true,
    .interrupt_offset = 0x50, // JASSeqCtrl's saved position, +0x44 in the sequence control
    .idle_in_calls = true,    // the root tracks poll the game once a beat in a call that never returns
    .parameters_offset = 0x48,
    .parameters_track_offset = 0x2c4,
    .effects_sequence = 0x80515260, // JAInter::SeMgr::seHandle
    .effect_slot_offset = 0x14,     // JAISound's track number, after its JSULink and its vtable
    .parent_offset = 0x2f8,
    .children_offset = 0x2fc,
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
    .wait_offset = 0x14,
    .note_wait_offset = 0,
    .depth_offset = 0x18,
    .parse_command = 0x8002bbd8, // the head of the loop over a track's commands in the sequence parser
    .jump_handler = 0x8002a830,  // the sequence parser's command 0xC8, where it takes the jump
    .command_track_register = 31,
    .jump_track_register = 27,
    .jump_target_register = 25,
    .wave_handle_functions = {0x80032c9c, 0x8003358c}, // the basic and simple wave banks' getWaveHandle
    .wave_bank_table = 0x801a42cc,                     // the wave bank manager's table
    .reader_offset = 0xc,
    .parameters_inline = true,
    .interrupt_offset = 0x50,
    .idle_in_calls = true, // an effect's slot track polls in a loop inside the call of the effect's routine
    .parameters_offset = 0x48,
    .parameters_track_offset = 0x2c4,
    .effects_sequence = 0x801a43f8,
    .effect_slot_offset = 0x14,
    .parent_offset = 0x2f8,
    .children_offset = 0x2fc,
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
            0x8002dff0,           // OSInit
            0x800368f0,           // DVDInit
            0x80039c7c,           // VIInit
            0x8003a12c,           // VIWaitForRetrace
            0x800337b4,           // OSGetCurrentThread
            0x8006e820,           // JKRExpHeap::createRoot
            0x8006e8a0,           // JKRExpHeap::create
            0x80072020,           // JKRSolidHeap::create
            0x800664c4,           // JKRAram::create
            0x80071670,           // operator new
            0x80072e80,           // JKRThread's constructor for an OSThread
            0x80069a84,           // JKRArchive::mount
            0x8006bdf4,           // load_file: the loader's reader of a whole file
            0x800a96a0,           // "AudioRes/Seqs/ZeldaPakSeqs.arc"
            0x800a96c0,           // "AudioRes/ZeldaPak.baa"
            0x800289e0,           // audio_setup
            0x80029540,           // audio_frame: the sound manager's frame work
            0x820,                // section_offset: the section heap in the sound manager
            0x30,                 // section_data_offset
            0xec,                 // wave_banks_offset
            0x4c,                 // wave_arc_state_offset
        },
    .sequence_vtable = 0x8011e5dc,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x8011e548, // the vtable of the JAISound in a JAISe, after the JAISe's JSULink
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x800230d4,    // JASSeqParser::parse
    .jump_handler = 0x80021e24,     // the sequence parser's command 0xC7
    .conditional_jump = 0x80021f1c, // its command 0xC8, where it takes the jump
};

const std::vector<GameProfile> kProfiles = {
    kFourSwordsUsa,       kFourSwordsJpn, kTwilightPrincessUsa, kWindWakerUsa,          kMarioGolfUsa,
    kMarioPowerTennisUsa, kWarioWareUsa,  kLuigisMansionUsa,    kSuperMarioSunshineUsa, kPikminUsa,
    kJungleBeatUsa,       kDoubleDashUsa, kPikmin2Usa,          kPacManVsUsa,           kCollectorsEditionUsa};

// Pikmin 2's driver parameters: the profile's, then the number of pieces and the pieces (driver/pikmin2.c's Piece).
std::vector<uint32_t> Pikmin2Parameters(const GameProfile& p)
{
    std::vector<uint32_t> words = p.params;
    words.push_back(static_cast<uint32_t>(std::size(kPikmin2Pieces)));
    for (const Pikmin2Piece& piece : kPikmin2Pieces)
    {
        words.push_back(static_cast<uint32_t>(piece.kind) | uint32_t{piece.start_point} << 8 |
                        uint32_t{piece.beat_interval} << 16);
        words.insert(words.end(), piece.info.begin(), piece.info.end());
        for (const std::string_view name : {std::string_view(piece.sequence), std::string_view(piece.conductor)})
        {
            std::array<uint8_t, kPieceNameSize> bytes = {};
            std::copy_n(name.begin(), std::min(name.size(), bytes.size() - 1), bytes.begin());
            for (std::size_t i = 0; i < bytes.size(); i += 4)
            {
                words.push_back(xsflib::LoadBe32(bytes.data() + i));
            }
        }
    }

    return words;
}

// Pikmin 2's sounds: its pieces of music, each named by its file in the sequence archive, without the extension. The
// archive may be compressed with Yaz0. Throws xsflib::FormatError if the archive is damaged or lacks a piece's file.
std::vector<jaudio::SoundEntry> Pikmin2Sounds(const std::vector<uint8_t>& sequence_archive)
{
    std::vector<uint8_t> decoded;
    std::span<const uint8_t> archive = sequence_archive;
    if (jaudio::IsYaz0(sequence_archive))
    {
        decoded = jaudio::DecodeYaz0(sequence_archive);
        archive = decoded;
    }

    const std::vector<jaudio::ArchiveFile> files = jaudio::ReadRarc(archive);
    std::vector<jaudio::SoundEntry> sounds;
    for (const Pikmin2Piece& piece : kPikmin2Pieces)
    {
        const std::string_view name = piece.sequence;
        const auto is_piece = [name](const jaudio::ArchiveFile& file)
        {
            return file.name == name;
        };
        const auto file = std::find_if(files.begin(), files.end(), is_piece);
        if (file == files.end())
        {
            throw xsflib::FormatError("the sequence archive lacks " + std::string(name));
        }

        jaudio::SoundEntry sound;
        sound.name = name.substr(0, name.rfind('.'));
        sound.section = 0x80; // JAudio1's section of sequences
        sound.index = static_cast<uint16_t>(sounds.size());
        sound.section_name = "BGM";
        sound.source = jaudio::SoundEntry::Source::kArchive;
        sound.resource = file->id;
        sounds.push_back(std::move(sound));
    }

    return sounds;
}

// Whether the sound is one of Pikmin 2's AutoBgm pieces. Their conductors pick what the tracks play, at random.
bool IsAutoBgm(const GameProfile& p, uint32_t id)
{
    const uint32_t index = id & 0xffff;
    if (p.driver != Driver::kPikmin2 || (id & jaudio::kAafTypeMask) != jaudio::kAafSequence ||
        index >= std::size(kPikmin2Pieces))
    {
        return false;
    }

    const PieceKind kind = kPikmin2Pieces[index].kind;
    return kind == PieceKind::kAuto || kind == PieceKind::kAutoFree;
}

// The profile's driver parameters, in the order of the driver's parameter block.
std::vector<uint32_t> DriverParameters(const GameProfile& p)
{
    if (p.driver == Driver::kPikmin2)
    {
        return Pikmin2Parameters(p);
    }

    if (p.driver != Driver::kFourSwords)
    {
        return p.params;
    }

    return {p.frame_retraces,       p.max_heaps,
            p.system_heap_size,     p.aram_audio_size,
            p.aram_graph_size,      p.aram_priorities,
            p.audio_heap_size,      p.disc_heap_size,
            p.audio_object_size,    p.vi_init,
            p.vi_wait_for_retrace,  p.get_current_thread,
            p.get_thread_priority,  p.create_root_heap,
            p.create_exp_heap,      p.create_solid_heap,
            p.create_aram,          p.alloc,
            p.alloc_from,           p.thread_ctor,
            p.become_current_heap,  p.system_heap,
            p.game_globals,         p.audio_object_ctor,
            p.create_disc_thread,   p.audio_setup,
            p.audio_frame,          p.sound_manager_offset,
            p.section_offset,       p.load_wave_bank,
            p.section_data_offset,  p.wave_banks_offset,
            p.wave_arc_state_offset};
}

// How GCSF reads a start-up archive: its messages, and its rules for the sound table and the wave systems.
const jaudio::BaaOptions kBaaOptions{
    .not_an_archive = "it isn't a BAA archive",
    .unknown_command = "the BAA archive has a command that isn't known",
    .past_end = "the BAA archive is damaged",
    .table_damaged = "the BAA archive's sound table is damaged",
    .no_table = "the BAA archive has no sound table",
    .no_sounds = "the BAA archive's sound table lists no sounds",
    .wave_system_damaged = "a wave system in the BAA archive is damaged",
    .min_size = 0,
    .tables_reach_end = true,
    .check_magic = true,
    .skip_empty = true,
    .cap_counts = true,
    .check_name_counts = true,
    .names_inside = true,
    .sources = true,
    .section_names = true,
};

// Reads a file of the host's file system. A file larger than 1 GiB, the largest that GCSF reads into memory, can't be
// read.
bool ReadHostFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && xsflib::ReadFile(path, data, std::size_t{1} << 30);
}

// Reads another release's sequence table as the sequence archive at `path`. `read_disc` reads the disc's archive. Each
// file's data then comes from the table (see jaudio::RepackSequenceTable). `folder` must hold one table, a `.bdt` file.
// The Wind Waker HD keeps its sequences that way. AppleDouble files don't count. macOS writes one, named "._" and the
// file's name, beside each file that it copies to a FAT or exFAT drive.
bool ReadSequenceTable(const std::filesystem::path& folder,
                       const std::function<bool(const std::string&, std::vector<uint8_t>&)>& read_disc,
                       const std::string& path, std::vector<uint8_t>& data)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> tables;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code type_error;
        if (it->path().extension() == ".bdt" && !xsflib::Utf8(it->path().filename()).starts_with("._") &&
            it->is_regular_file(type_error))
        {
            tables.push_back(it->path());
        }
    }

    std::vector<uint8_t> table;
    std::vector<uint8_t> archive;
    if (ec || tables.size() != 1 || !ReadHostFile(tables.front(), table) || !read_disc || !read_disc(path, archive))
    {
        return false;
    }

    try
    {
        data = jaudio::RepackSequenceTable(archive, table);
    }
    catch (const xsflib::FormatError&)
    {
        return false;
    }

    return true;
}

// The files of a sequence archive, the archive decoded first if it's compressed with Yaz0. `decoded` then holds the
// decoded archive, and the files' spans point into it. An earlier JAudio1's archive isn't a RARC. The sequence index in
// its start-up archive divides it (Luigi's Mansion's `JaiArcS.arc`). Throws xsflib::FormatError if the archive or the
// index is damaged.
std::vector<jaudio::ArchiveFile> SequenceFiles(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                               const std::vector<uint8_t>& sequence_archive,
                                               std::vector<uint8_t>& decoded)
{
    std::span<const uint8_t> archive = sequence_archive;
    if (jaudio::IsYaz0(sequence_archive))
    {
        decoded = jaudio::DecodeYaz0(sequence_archive);
        archive = decoded;
    }

    if (profile.library == SoundLibrary::kJAudio1)
    {
        if (auto files = jaudio::ReadAafSequenceFiles(sound_archive, archive))
        {
            return std::move(*files);
        }
    }

    return jaudio::ReadRarc(archive);
}

// A start-up archive as the sound library reads it. Donkey Kong Jungle Beat compresses its archive with Yaz0, and the
// game decodes it as it loads it. A compressed archive is decoded into `decoded`. Throws xsflib::FormatError if the
// compressed data is damaged.
std::span<const uint8_t> StartUpArchive(const std::vector<uint8_t>& sound_archive, std::vector<uint8_t>& decoded)
{
    if (!jaudio::IsYaz0(sound_archive))
    {
        return sound_archive;
    }

    decoded = jaudio::DecodeYaz0(sound_archive);

    return decoded;
}

} // namespace

const std::vector<GameProfile>& KnownProfiles()
{
    return kProfiles;
}

const GameProfile* FindProfile(const disc::GameFiles& game)
{
    const std::string sha1 = xsflib::Sha1::Hex(xsflib::Sha1::Of(game.dol));
    for (const GameProfile& profile : kProfiles)
    {
        if (game.GameId() == profile.game_id && sha1 == profile.dol_sha1)
        {
            return &profile;
        }
    }

    return nullptr;
}

bool ListsSongs(const GameProfile& profile)
{
    return profile.library == SoundLibrary::kMusyX || profile.library == SoundLibrary::kJac;
}

bool NamesWaves(const GameProfile& profile)
{
    return profile.driver == Driver::kWindWaker || profile.driver == Driver::kTwilightPrincess ||
           profile.driver == Driver::kSuperMarioSunshine || profile.driver == Driver::kJungleBeat ||
           profile.driver == Driver::kPikmin2;
}

std::optional<std::vector<uint32_t>> GameWaveGroups(const GameProfile& profile, uint32_t id)
{
    const uint32_t index = id & 0xffff;
    if (profile.driver != Driver::kPikmin2 || (id & jaudio::kAafTypeMask) != jaudio::kAafSequence ||
        index >= std::size(kPikmin2Pieces) || kPikmin2Pieces[index].wave_scene == kFindWaves)
    {
        return std::nullopt;
    }

    // Group 0 holds the effects and stays loaded. The driver loads the scene's group.
    std::vector<uint32_t> groups = {0};
    if (kPikmin2Pieces[index].wave_scene != 0)
    {
        groups.push_back(kPikmin2Pieces[index].wave_scene);
    }

    return groups;
}

void UnpackSoundArchive(disc::GameFiles& game, const GameProfile& profile)
{
    if (!profile.packed_archive)
    {
        return;
    }

    const auto from_game = game.read_file;
    const std::string sound_archive = profile.sound_archive;
    const std::string packed_archive = profile.packed_archive;
    const std::string name = xsflib::AsciiLower(profile.packed_sound_archive);
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        if (path != sound_archive)
        {
            return from_game && from_game(path, data);
        }

        std::vector<uint8_t> packed;
        if (!from_game || !from_game(packed_archive, packed))
        {
            return false;
        }

        try
        {
            std::vector<uint8_t> decoded;
            std::span<const uint8_t> archive = packed;
            if (jaudio::IsYaz0(packed))
            {
                decoded = jaudio::DecodeYaz0(packed);
                archive = decoded;
            }

            for (const jaudio::ArchiveFile& file : jaudio::ReadRarc(archive))
            {
                if (xsflib::AsciiLower(file.name) == name)
                {
                    data.assign(file.data.begin(), file.data.end());
                    return true;
                }
            }
        }
        catch (const xsflib::FormatError&)
        {
        }

        return false;
    };
}

std::optional<std::filesystem::path> UseSoundFiles(disc::GameFiles& game, const GameProfile& profile,
                                                   const std::filesystem::path& folder, std::string* error)
{
    namespace fs = std::filesystem;
    const auto fail = [error](const std::string& message) -> std::optional<fs::path>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    // The start-up archive's folder on the disc ("Audiores/"), and its file name.
    const std::string sound_archive = profile.sound_archive;
    const std::size_t slash = sound_archive.rfind('/');
    const std::string sound_folder = slash == std::string::npos ? "" : sound_archive.substr(0, slash + 1);
    const std::string file_name = sound_archive.substr(sound_folder.size());
    const fs::path file_path = xsflib::PathFromUtf8(file_name);

    std::vector<fs::path> found;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        std::error_code type_error;
        if (it->path().filename() == file_path && it->is_regular_file(type_error))
        {
            found.push_back(it->path().parent_path());
        }
    }

    if (ec)
    {
        return fail(xsflib::Utf8(folder) + ": " + ec.message());
    }

    if (found.empty())
    {
        return fail(xsflib::Utf8(folder) + " holds no " + file_name);
    }

    if (found.size() > 1)
    {
        std::sort(found.begin(), found.end());
        std::string folders;
        for (const fs::path& path : found)
        {
            folders += (folders.empty() ? "" : ", ") + xsflib::Utf8(path);
        }

        return fail(xsflib::Utf8(folder) + " holds more than one " + file_name + " (in " + folders +
                    "). Give the folder of the one to use.");
    }

    // The sound files come from the folder, and the rest from the game. A sound file that the folder lacks belongs to
    // the other release, and isn't read from the game. A sequence table in place of the sequence archive is read into
    // the game's archive.
    const fs::path root = found.front();
    const auto from_game = game.read_file;
    const std::string sequence_archive = profile.sequence_archive ? profile.sequence_archive : "";
    const std::string wave_folder = profile.wave_folder;
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        const bool sound_file = path == sound_archive || (!sequence_archive.empty() && path == sequence_archive) ||
                                path.starts_with(wave_folder);
        if (!sound_file || !path.starts_with(sound_folder))
        {
            return from_game && from_game(path, data);
        }

        const fs::path host = root / xsflib::PathFromUtf8(path.substr(sound_folder.size()));
        if (ReadHostFile(host, data))
        {
            return true;
        }

        return path == sequence_archive && ReadSequenceTable(host.parent_path(), from_game, path, data);
    };

    return root;
}

namespace
{

// A JAudio1 sound name table (.asn): a 16-byte header, then 32-byte records. A record holds a name in up to 28 bytes,
// then a word. The first 18 records name the sound table's categories: 16 of effects, the sequences and the streams.
// Each record after them names a sound, and its word is the sound's ID.
constexpr std::size_t kNamesHeader = 0x10;
constexpr std::size_t kNameRecordSize = 0x20;
constexpr std::size_t kNameSize = 0x1c;
constexpr std::size_t kNamedCategories = 18;

// The key under which a sound's name is found: its ID without an effect's bits 10 and 11, and without the category of
// a sequence or a stream. Those bits set how long an effect plays. The sound table gives a sequence category 0, and the
// names give it the table's category for sequences.
uint32_t NameKey(uint32_t id)
{
    const uint32_t type = id & jaudio::kAafTypeMask;
    return type == jaudio::kAafEffect ? id & ~(jaudio::kAafToEnd | jaudio::kAafToEnd >> 1)
                                      : type | (id & jaudio::kAafIndexMask);
}

// The sounds that a name table names, by NameKey: each sound's name and ID. Throws xsflib::FormatError if the table is
// damaged.
std::map<uint32_t, std::pair<std::string, uint32_t>> ReadSoundNames(std::span<const uint8_t> table)
{
    if (table.size() < kNamesHeader || (table.size() - kNamesHeader) / kNameRecordSize < kNamedCategories)
    {
        throw xsflib::FormatError("the sound name table is damaged");
    }

    std::map<uint32_t, std::pair<std::string, uint32_t>> names;
    const xsflib::Reader r(table, true);
    for (std::size_t at = kNamesHeader + kNamedCategories * kNameRecordSize; at + kNameRecordSize <= table.size();
         at += kNameRecordSize)
    {
        const uint32_t id = r.U32(at + kNameSize);
        names.try_emplace(NameKey(id), r.CString(at, kNameSize), id);
    }

    return names;
}

} // namespace

std::vector<jaudio::SoundEntry> ListSounds(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                           const std::vector<uint8_t>& sequence_archive,
                                           const std::vector<uint8_t>* names)
{
    if (profile.library != SoundLibrary::kJAudio1)
    {
        jaudio::BaaOptions options = kBaaOptions;
        options.first_layouts = profile.first_layouts;
        std::vector<uint8_t> decoded;
        return jaudio::ReadSounds(StartUpArchive(sound_archive, decoded), options);
    }

    // A JAudio1 sound table names no sound. A sequence takes its file's name in the sequence archive, without the
    // extension. A name table's name takes precedence. A sequence then takes the game's ID too, and plays as the game
    // plays it. An effect keeps its ID: it plays to its end. The archive may be compressed with Yaz0.
    const auto named = names ? ReadSoundNames(*names) : std::map<uint32_t, std::pair<std::string, uint32_t>>{};
    std::vector<uint8_t> decoded;
    std::map<uint32_t, std::string> file_names;
    for (const jaudio::ArchiveFile& file : SequenceFiles(profile, sound_archive, sequence_archive, decoded))
    {
        file_names.emplace(file.id, file.name.substr(0, file.name.rfind('.')));
    }

    // Pikmin 2's table lists sequences whose data the game's sound layer gives them. The game's pieces of music take
    // their place, ahead of the effects (see Pikmin2Sounds).
    std::vector<jaudio::SoundEntry> sounds = jaudio::ReadAafSounds(sound_archive);
    if (profile.driver == Driver::kPikmin2)
    {
        std::erase_if(sounds, [](const jaudio::SoundEntry& sound)
                      { return sound.source == jaudio::SoundEntry::Source::kArchive; });
    }

    for (jaudio::SoundEntry& sound : sounds)
    {
        const uint32_t id = sound.Id();
        if (sound.source == jaudio::SoundEntry::Source::kArchive)
        {
            const auto name = file_names.find(sound.resource);
            if (name == file_names.end())
            {
                throw xsflib::FormatError(
                    "the AAF archive's sound table names a sequence that the sequence archive lacks");
            }

            sound.name = name->second;
            sound.section_name = "BGM";
            if (const auto game = named.find(NameKey(id)); game != named.end())
            {
                sound.name = game->second.first;
                sound.group = static_cast<uint8_t>(game->second.second >> 16);
            }

            continue;
        }

        // An effect is named by its category and its index.
        char label[32];
        std::snprintf(label, sizeof(label), "se%u-%04u", static_cast<unsigned>(jaudio::AafCategory(id)),
                      static_cast<unsigned>(id & jaudio::kAafIndexMask));
        const auto game = named.find(NameKey(id));
        sound.name = game != named.end() ? game->second.first : label;
        sound.section_name = "SE";
        sound.group_name = std::to_string(jaudio::AafCategory(id));
    }

    if (profile.driver == Driver::kPikmin2)
    {
        std::vector<jaudio::SoundEntry> pieces = Pikmin2Sounds(sequence_archive);
        sounds.insert(sounds.begin(), std::make_move_iterator(pieces.begin()), std::make_move_iterator(pieces.end()));
    }

    return sounds;
}

std::vector<jaudio::WaveSystem> WaveSystems(const GameProfile& profile, const std::vector<uint8_t>& sound_archive)
{
    if (profile.library == SoundLibrary::kJAudio1)
    {
        return jaudio::ReadAafWaveSystems(sound_archive);
    }

    std::vector<uint8_t> decoded;
    return jaudio::ReadWaveSystems(StartUpArchive(sound_archive, decoded), kBaaOptions);
}

bool IsMusic(const jaudio::SoundEntry& sound)
{
    const std::string section = xsflib::AsciiLower(sound.section_name);
    return section == "bgm" || section == "type_bgm";
}

Sound SoundOf(const jaudio::SoundEntry& entry)
{
    std::string label = entry.name;
    if (label.empty())
    {
        char id[16];
        std::snprintf(id, sizeof(id), "%08X", static_cast<unsigned>(entry.Id()));
        label = id;
    }

    return {entry.Id(), std::move(label), IsMusic(entry), entry.stream};
}

namespace
{

// A MusyX game's song, from a table in its code.
struct SongEntry
{
    uint32_t table;  // 0 for the songs, 1 for the jingles
    uint32_t number; // in the table
    uint32_t file_number;
    std::string name; // the file's name
};

// The songs of a MusyX game's tables, the songs then the jingles. Throws xsflib::FormatError if a table or a name lies
// outside the game's code.
std::vector<SongEntry> SongEntries(const ntdlib::Dol& dol, const GameProfile& profile)
{
    // The bytes of the game's code and data from `address` to the end of its section.
    const auto at = [&dol](uint32_t address) -> std::span<const uint8_t>
    {
        for (const ntdlib::Dol::Section& section : dol.Sections())
        {
            if (address >= section.address && address - section.address < section.data.size())
            {
                return section.data.subspan(address - section.address);
            }
        }

        throw xsflib::FormatError("main.dol has nothing at " + xsflib::Hex(address, 8));
    };

    // An entry holds the name or its address, then the file number, the song's number and its volume.
    const uint32_t name_size = profile.song_name_size != 0 ? profile.song_name_size : 4;
    const uint32_t entry_size = name_size + 12;
    std::vector<SongEntry> songs;
    const uint32_t tables[2][2] = {{profile.song_table, profile.song_count},
                                   {profile.jingle_table, profile.jingle_count}};
    for (uint32_t table = 0; table < 2; table++)
    {
        for (uint32_t number = 0; number < tables[table][1]; number++)
        {
            const std::span<const uint8_t> entry = at(tables[table][0] + number * entry_size);
            if (entry.size() < entry_size)
            {
                throw xsflib::FormatError("main.dol's song table ends early");
            }

            const std::span<const uint8_t> name =
                profile.song_name_size != 0 ? entry.first(name_size) : at(xsflib::LoadBe32(entry.data()));
            const auto end = std::find(name.begin(), name.end(), uint8_t{0});
            if (end == name.end())
            {
                throw xsflib::FormatError(profile.song_name_size != 0 ? "main.dol's song name runs past its entry"
                                                                      : "main.dol's song name runs past its section");
            }

            songs.push_back(
                {table, number, xsflib::LoadBe32(entry.data() + name_size), std::string(name.begin(), end)});
        }
    }

    return songs;
}

} // namespace

namespace
{

// The bytes of the game's code and data from `address` to the end of its section. Throws xsflib::FormatError if no
// section holds the address.
std::span<const uint8_t> DolBytes(const ntdlib::Dol& dol, uint32_t address)
{
    for (const ntdlib::Dol::Section& section : dol.Sections())
    {
        if (address >= section.address && address - section.address < section.data.size())
        {
            return section.data.subspan(address - section.address);
        }
    }

    throw xsflib::FormatError("main.dol has nothing at " + xsflib::Hex(address, 8));
}

// The songs that a MusyX game's code names (GameProfile::named_songs), in the order of its table. Throws
// xsflib::FormatError if the table or a name lies outside the game's code.
std::vector<Sound> NamedSongs(const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<Sound> songs;
    for (uint32_t i = 0; i < profile.named_song_count; i++)
    {
        const std::span<const uint8_t> entry = DolBytes(dol, profile.named_songs + i * profile.named_song_size);
        if (entry.size() < profile.named_song_size)
        {
            throw xsflib::FormatError("main.dol's song table ends early");
        }

        const std::span<const uint8_t> name = DolBytes(dol, xsflib::LoadBe32(entry.data() + profile.named_song_name));
        const auto end = std::find(name.begin(), name.end(), uint8_t{0});
        if (end == name.end())
        {
            throw xsflib::FormatError("main.dol's song name runs past its section");
        }

        // The game's text marks the space between two words with the bytes 0x94 0xBC.
        std::string label(name.begin(), end);
        for (std::size_t at = label.find("\x94\xbc"); at != std::string::npos; at = label.find("\x94\xbc", at))
        {
            label.replace(at, 2, " ");
        }

        songs.push_back({xsflib::LoadBe32(entry.data()), std::move(label), true});
    }

    return songs;
}

// A Jac game's pieces of music, from its sequence index (see ListSongs). The index has a 32-byte header: "BARC", four
// bytes, a word, the number of files and the sequence archive's name. Then comes a 32-byte entry for each file: its
// name in up to 14 bytes, two bytes, two words, and the file's offset and size in the archive. An empty file is a
// dummy. Throws xsflib::FormatError if the index lies outside the game's code or isn't a BARC.
std::vector<Sound> JacPieces(const ntdlib::Dol& dol, const GameProfile& profile)
{
    constexpr std::size_t kHeader = 0x20, kEntry = 0x20, kNameSize = 14, kSize = 0x1c;
    constexpr uint32_t kFirstPiece = 2;
    const std::span<const uint8_t> index = DolBytes(dol, profile.sequence_index);
    if (index.size() < kHeader || !std::equal(index.begin(), index.begin() + 4, "BARC"))
    {
        throw xsflib::FormatError("main.dol has no sequence index at " + xsflib::Hex(profile.sequence_index, 8));
    }

    const uint32_t count = xsflib::LoadBe32(index.data() + 0xc);
    if (index.size() < kHeader + std::size_t{count} * kEntry)
    {
        throw xsflib::FormatError("main.dol's sequence index ends early");
    }

    // A sound's ID holds the piece's number in a byte.
    constexpr uint32_t kMaxPieces = 0x100;
    if (count > kMaxPieces)
    {
        throw xsflib::FormatError("main.dol's sequence index has " + std::to_string(count) + " files");
    }

    const auto id = [](uint32_t piece, uint32_t part, uint32_t system, uint32_t group)
    {
        return jaudio::kAafSequence | group << 24 | system << 16 | part << 8 | piece;
    };
    std::vector<Sound> pieces;
    for (uint32_t number = kFirstPiece; number < count; number++)
    {
        const uint8_t* entry = index.data() + kHeader + number * kEntry;
        if (xsflib::LoadBe32(entry + kSize) == 0)
        {
            continue;
        }

        std::string label(reinterpret_cast<const char*>(entry),
                          std::find(entry, entry + kNameSize, uint8_t{0}) - entry);
        label = label.substr(0, label.rfind('.'));
        if (number != profile.parted_piece)
        {
            pieces.push_back({id(number, 0, number, 0), std::move(label), true});
            continue;
        }

        for (const JacPart& part : profile.parts)
        {
            pieces.push_back(
                {id(number, part.number, part.system, part.group), label + "-" + std::to_string(part.number), true});
        }
    }

    return pieces;
}

} // namespace

std::vector<Sound> ListSongs(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    if (profile.library == SoundLibrary::kJac)
    {
        return JacPieces(*dol, profile);
    }

    if (profile.named_songs)
    {
        return NamedSongs(*dol, profile);
    }

    std::vector<Sound> songs;
    for (SongEntry& song : SongEntries(*dol, profile))
    {
        std::string label = std::move(song.name);
        if (label.size() > 4 && xsflib::AsciiLower(label.substr(label.size() - 4)) == ".sng")
        {
            label.resize(label.size() - 4);
        }

        songs.push_back({song.table << 24 | song.number, std::move(label), true});
    }

    return songs;
}

std::string SongPath(uint32_t file_number)
{
    const uint32_t n = file_number >> 1;
    return {static_cast<char>('A' + n / 676 % 26), '/', static_cast<char>('A' + n / 26 % 26), '/',
            static_cast<char>('A' + n % 26)};
}

std::vector<UnplayedSequence> UnplayedSequences(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                                const std::vector<jaudio::SoundEntry>& sounds,
                                                const std::vector<uint8_t>& sequence_archive)
{
    // Pikmin 2's sounds are every piece of music already.
    if (profile.driver == Driver::kPikmin2)
    {
        return {};
    }

    // The files that the table's sounds play, and the volume that most of them play at. A tie goes to the volume that
    // comes first. JAudio1's effects sequence plays a file too.
    const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
    std::set<uint32_t> played;
    if (jaudio1)
    {
        played.insert(jaudio::AafEffectsSequenceFile(sound_archive));
    }

    std::map<uint8_t, int> volumes;
    const jaudio::SoundEntry* first = nullptr;
    for (const jaudio::SoundEntry& sound : sounds)
    {
        if (sound.source == jaudio::SoundEntry::Source::kArchive)
        {
            played.insert(sound.resource);
            volumes[sound.volume]++;
            first = first ? first : &sound;
        }
    }

    if (!first)
    {
        return {};
    }

    uint8_t volume = first->volume;
    int most = 0;
    for (const jaudio::SoundEntry& sound : sounds)
    {
        if (sound.source == jaudio::SoundEntry::Source::kArchive && volumes[sound.volume] > most)
        {
            volume = sound.volume;
            most = volumes[sound.volume];
        }
    }

    // Archives compressed with Yaz0 are decoded first. A copy of the start-up archive stays decoded. The game loads it
    // as it is.
    std::vector<uint8_t> decoded_baa;
    const std::span<const uint8_t> start_up = StartUpArchive(sound_archive, decoded_baa);
    std::vector<uint8_t> decoded;
    std::vector<UnplayedSequence> unplayed;
    for (const jaudio::ArchiveFile& file : SequenceFiles(profile, sound_archive, sequence_archive, decoded))
    {
        if (played.contains(file.id) || file.data.empty())
        {
            continue;
        }

        UnplayedSequence u;
        u.sound = *first;
        u.sound.name = file.name.substr(0, file.name.rfind('.'));
        u.sound.volume = volume;
        u.sound.resource = file.id;
        u.file_name = file.name;

        // The sound's entry: the volume and the file's number, where the first sound's layout has them.
        u.sound_archive.assign(start_up.begin(), start_up.end());
        uint8_t* entry = u.sound_archive.data() + first->offset;
        const jaudio::ItemLayout layout = jaudio1 ? jaudio::kAafItemLayout : jaudio::LayoutOf(first->type);
        entry[layout.volume] = volume;
        xsflib::StoreBe16(entry + *layout.file, file.id);
        unplayed.push_back(std::move(u));
    }

    return unplayed;
}

std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error,
                                                 const std::vector<std::string>* wave_archives)
{
    const auto fail = [error](const std::string& message) -> std::optional<std::vector<uint8_t>>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        return fail("main.dol isn't a DOL");
    }

    // The game's code and data, section by section.
    ProgramBuilder program;
    for (const ntdlib::Dol::Section& section : dol->Sections())
    {
        program.AddMemory(section.address, section.data.data(), section.data.size());
    }

    // The driver, with the game's profile in its parameters. A .minigcsf fills in the sound. For a driver that
    // NamesWaves, it also fills in the waves.
    const std::optional<DriverImage> image = ImageOf(profile.driver);
    if (!image || image->params != DriverBlock::kAddress)
    {
        return fail(std::string("GCSF has no driver for ") + profile.name);
    }

    std::vector<uint8_t> driver(image->blob.begin(), image->blob.end());
    const std::vector<uint32_t> parameters = DriverParameters(profile);
    const uint32_t first =
        (image->params - image->base) + (NamesWaves(profile) ? kParamsListedProfileOffset : kParamsProfileOffset);
    if (first + parameters.size() * 4 > driver.size())
    {
        return fail(std::string("the profile for ") + profile.name + " has more parameters than its driver takes");
    }

    for (std::size_t i = 0; i < parameters.size(); i++)
    {
        xsflib::StoreBe32(driver.data() + first + i * 4, parameters[i]);
    }

    program.AddMemory(image->base, driver);

    // main branches to the driver. The profile's patches follow.
    uint8_t branch[4];
    xsflib::StoreBe32(branch, 0x48000000 | ((image->entry - profile.main) & 0x03fffffc));
    program.AddMemory(profile.main, branch, sizeof(branch));
    for (const auto& [address, word] : profile.patches)
    {
        uint8_t bytes[4];
        xsflib::StoreBe32(bytes, word);
        program.AddMemory(address, bytes, sizeof(bytes));
    }

    ProcessDescriptor d;
    d.entry = dol->Entry();
    d.status_address = DriverBlock::kAddress + DriverBlock::kStatusOffset;

    // A MusyX game: the disc's header, the sound banks and every song's file.
    if (profile.library == SoundLibrary::kMusyX)
    {
        std::vector<std::string> paths(profile.bank_files.begin(), profile.bank_files.end());
        try
        {
            for (const SongEntry& song : SongEntries(*dol, profile))
            {
                paths.push_back(SongPath(song.file_number));
            }
        }
        catch (const xsflib::FormatError& e)
        {
            return fail(e.what());
        }

        program.AddFile("sys/boot.bin", game.boot);
        for (const std::string& path : paths)
        {
            std::vector<uint8_t> bytes;
            if (!game.read_file || !game.read_file(path, bytes))
            {
                return fail("can't read " + path);
            }

            program.AddFile(path, bytes);
        }

        return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
    }

    // A Jac game: the disc's header, the bank file, the sequence archive and the wave archives that the bank file's
    // wave systems name. The bank file starts with the offset and the number of its wave systems' entries, then those
    // of its banks' entries. An entry gives a wave system's offset and size in the file. A size of 0 means none. A
    // group can have no archive.
    if (profile.library == SoundLibrary::kJac)
    {
        std::vector<uint8_t> banks;
        if (!game.read_file || !game.read_file(profile.sound_archive, banks))
        {
            return fail(std::string("can't read ") + profile.sound_archive);
        }

        std::vector<std::string> paths = {profile.sequence_archive};
        try
        {
            const xsflib::Reader r(banks, true);
            const uint32_t list = r.U32(0);
            const uint32_t systems = r.U32(4);
            for (uint32_t number = 0; number < systems; number++)
            {
                const uint32_t offset = r.U32(list + number * 8);
                const uint32_t size = r.U32(list + number * 8 + 4);
                if (size == 0)
                {
                    continue;
                }

                for (const jaudio::WaveGroup& group : jaudio::ReadWaveSystem(r.Sub(offset, size), number).groups)
                {
                    const std::string path = profile.wave_folder + group.archive;
                    if (!group.archive.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
                    {
                        paths.push_back(path);
                    }
                }
            }
        }
        catch (const xsflib::FormatError& e)
        {
            return fail(std::string(profile.sound_archive) + ": " + e.what());
        }

        program.AddFile("sys/boot.bin", game.boot);
        program.AddFile(profile.sound_archive, banks);
        for (const std::string& path : paths)
        {
            std::vector<uint8_t> bytes;
            if (!game.read_file(path, bytes))
            {
                return fail("can't read " + path);
            }

            program.AddFile(path, bytes);
        }

        return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
    }

    // The disc: its header, the start-up archive, the sequence archive, the other sound files and the wave archives. A
    // JAudio1 library always has the wave archives that the sound library loads at start-up.
    std::vector<uint8_t> sound_archive;
    if (!game.read_file || !game.read_file(profile.sound_archive, sound_archive))
    {
        return fail(std::string("can't read ") + profile.sound_archive);
    }

    std::vector<std::string> paths;
    if (profile.sequence_archive)
    {
        paths.emplace_back(profile.sequence_archive);
    }

    paths.insert(paths.end(), profile.other_sound_files.begin(), profile.other_sound_files.end());
    try
    {
        const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
        std::vector<std::string> names;
        if (wave_archives)
        {
            names = *wave_archives;
            std::vector<std::string> staying;
            if (jaudio1)
            {
                staying = jaudio::AafStayingWaveArchiveNames(sound_archive);
                for (const std::string& name :
                     jaudio::GroupArchives(jaudio::ReadAafWaveSystems(sound_archive), profile.staying_groups))
                {
                    staying.push_back(name);
                }
            }

            for (const std::string& name : staying)
            {
                if (std::find(names.begin(), names.end(), name) == names.end())
                {
                    names.push_back(name);
                }
            }
        }
        else
        {
            std::vector<uint8_t> decoded;
            names = jaudio1 ? jaudio::AafWaveArchiveNames(sound_archive)
                            : jaudio::WaveArchiveNames(StartUpArchive(sound_archive, decoded), kBaaOptions);
        }

        for (const std::string& name : names)
        {
            paths.push_back(profile.wave_folder + name);
        }
    }
    catch (const xsflib::FormatError& e)
    {
        return fail(std::string(profile.sound_archive) + ": " + e.what());
    }

    // The other files that the game's sound layer reads.
    for (const char* path : profile.other_files)
    {
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }

    program.AddFile("sys/boot.bin", game.boot);
    program.AddFile(profile.sound_archive, sound_archive);
    for (const std::string& path : paths)
    {
        std::vector<uint8_t> bytes;
        if (!game.read_file(path, bytes))
        {
            return fail("can't read " + path);
        }

        program.AddFile(path, bytes);
    }

    return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
}

std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags, const FileChunk* file,
                               const std::vector<uint32_t>* waves)
{
    uint8_t sound[4];
    xsflib::StoreBe32(sound, id);
    ProgramBuilder program;
    program.AddMemory(DriverBlock::kAddress + kParamsSoundOffset, sound, sizeof(sound));
    if (waves)
    {
        program.AddMemory(DriverBlock::kAddress + kParamsWavesOffset, WaveList(*waves));
    }

    if (file)
    {
        program.AddFile(file->path, file->data);
    }

    tags["_lib"] = lib_name;

    return WritePsf(kPsfVersionGcsf, {}, program.Data(), tags);
}

namespace
{

// What the shared length analysis reads of GCSF's player (see ntdlib::jaudio::AnalyzeGameSound).
class AnalysisMachine
{
public:
    explicit AnalysisMachine(Player& player) : player_(player), system_(*player.System())
    {
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        return player_.Render(out, frames);
    }

    uint32_t Word(uint32_t address) const
    {
        const uint8_t* p = system_.Memory().At(ToPhysical(address), 4);
        return p != nullptr ? xsflib::LoadBe32(p) : 0;
    }

    std::optional<uint8_t> Byte(uint32_t address) const
    {
        const uint8_t* p = system_.Memory().At(ToPhysical(address), 1);
        return p != nullptr ? std::optional<uint8_t>(*p) : std::nullopt;
    }

    ppc::Cpu& Cpu()
    {
        return system_.Cpu();
    }

private:
    Player& player_;
    gc::System& system_;
};

// What the length analysis watches in the profile's game.
ntdlib::jaudio::GameWatch WatchOf(const GameProfile& p)
{
    ntdlib::jaudio::GameWatch watch;
    watch.parse_command = p.parse_command;
    watch.jump_handler = p.jump_handler;
    watch.command_track_register = p.command_track_register;
    watch.depth_offset = p.depth_offset;
    watch.relative_positions = p.relative_positions;
    watch.sequence_vtable = p.sequence_vtable;
    watch.sequence_track_offset = p.sequence_track_offset;
    watch.effect_vtable = p.effect_vtable;
    watch.effect_track_offset = p.effect_track_offset;
    watch.wait_offset = p.wait_offset;
    watch.note_wait_offset = p.note_wait_offset;
    watch.wave_handle_functions[0] = p.wave_handle_functions[0];
    watch.wave_handle_functions[1] = p.wave_handle_functions[1];
    watch.wave_bank_table = p.wave_bank_table;
    watch.wave_bank_table_offset = p.wave_bank_table_offset;
    watch.wave_bank_count_offset = p.wave_bank_count_offset;
    watch.reader_offset = p.reader_offset;
    watch.parameters_inline = p.parameters_inline;
    watch.interrupt_offset = p.interrupt_offset;
    watch.idle_in_calls = p.idle_in_calls;
    watch.jump_track_register = p.jump_track_register;
    watch.jump_target_register = p.jump_target_register;
    watch.conditional_jump = p.conditional_jump;
    watch.conditional_track_register = p.conditional_track_register;
    watch.conditional_arguments_register = p.conditional_arguments_register;
    watch.jaudio1 = p.library == SoundLibrary::kJAudio1 || p.library == SoundLibrary::kJac;
    watch.parameters_offset = p.parameters_offset;
    watch.parameters_track_offset = p.parameters_track_offset;
    watch.root_tracks = p.root_tracks;
    watch.root_track_count = p.root_track_count;
    watch.effects_sequence = p.effects_sequence;
    watch.effect_slot_offset = p.effect_slot_offset;
    watch.parent_offset = p.parent_offset;
    watch.children_offset = p.children_offset;
    watch.endless_wait = p.endless_wait;
    watch.interrupt_return_offset = p.interrupt_return_offset;

    return watch;
}

// A MusyX song in game mode, as xsflib's MeasureLength plays it (see xsflib/rip/length.h). A frame is one block of the
// analysis's output (kLengthRules.block_frames). The song loops in a frame in which its tracks' slots fall. It plays
// while it's in one of MusyX's lists of songs. It also counts as playing before it first gets there. A jingle's thread
// waits half a second before it starts the jingle. The output keeps a constant level on each channel once the song has
// stopped. Sound is therefore a sample that differs by more than xsflib's kSilenceLevel from its channel's recent
// level. That level is a running average over about 50 ms. The song rings while it has sounded in the last 0.5 s. The
// analysis therefore keeps the end of a release. The adapter follows no voices and finds no steady state.
class MusyXSong
{
public:
    MusyXSong(Player& player, const GameProfile& profile) : player_(player), profile_(profile)
    {
        last_ticks_.assign(profile.track_slot_count, 0);
    }

    bool RunFrame()
    {
        int16_t block[2 * kLengthRules.block_frames];
        if (player_.Render(block, kLengthRules.block_frames) != kLengthRules.block_frames)
        {
            return false;
        }

        sounded_ = false;
        for (std::size_t i = 0; i < 2 * kLengthRules.block_frames; i++)
        {
            double& level = levels_[i % 2];
            sounded_ = sounded_ || std::abs(block[i] - level) > xsflib::rip::kSilenceLevel;
            level += (block[i] - level) / kLevelSamples;
        }

        if (sounded_)
        {
            last_sound_ = frame_;
        }

        const bool listed = Word(profile_.song_lists[0]) != 0 || Word(profile_.song_lists[1]) != 0;
        started_ = started_ || listed;
        playing_ = listed;

        // A loop of the song moves its tracks back, all in one frame. A slot's new tick depends on the pass, so the
        // song's loop has one target, 0.
        bool looped = false;
        for (uint32_t k = 0; k < profile_.track_slot_count; k++)
        {
            const uint32_t ticks = Word(profile_.track_slots + k * profile_.track_slot_size);
            looped = looped || ticks < last_ticks_[k];
            last_ticks_[k] = ticks;
        }

        if (looped && on_loop_)
        {
            on_loop_(0, 0);
        }

        ++frame_;

        return true;
    }

    bool Sounded() const
    {
        return sounded_;
    }

    bool Ringing() const
    {
        return last_sound_ && frame_ - *last_sound_ <= kRingingFrames;
    }

    std::size_t VoiceCount() const
    {
        return 0;
    }

    xsflib::rip::VoicePlace Voice(std::size_t) const
    {
        return {};
    }

    bool Active() const
    {
        return playing_;
    }

    bool Settled() const
    {
        return false;
    }

    bool Stuck() const
    {
        return false;
    }

    bool Moving() const
    {
        return false;
    }

    uint64_t ChangedAt() const
    {
        return 0;
    }

    bool Busy() const
    {
        return playing_ || (!started_ && frame_ < kStartFrames);
    }

    bool ChannelsChanging() const
    {
        return false;
    }

    int ActiveChannels() const
    {
        return 0;
    }

    template <typename F>
    void OnLoop(F f)
    {
        on_loop_ = std::move(f);
    }

private:
    // The output's running average takes a sample with this weight's inverse: about 50 ms.
    static constexpr double kLevelSamples = 1600.0;

    // The song rings for 0.5 s after its last sound, and a song that hasn't started in 5 s never will.
    static constexpr uint64_t kRingingFrames =
        kLengthRules.tail_ms * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;
    static constexpr uint64_t kStartFrames = 5000 * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;

    uint32_t Word(uint32_t address) const
    {
        const uint8_t* p = player_.System()->Memory().At(ToPhysical(address), 4);
        return p != nullptr ? xsflib::LoadBe32(p) : 0;
    }

    Player& player_;
    const GameProfile& profile_;
    std::vector<uint32_t> last_ticks_;
    double levels_[2] = {0.0, 0.0};
    std::function<void(int, uint32_t)> on_loop_;
    std::optional<uint64_t> last_sound_;
    uint64_t frame_ = 0;
    bool sounded_ = false;
    bool started_ = false;
    bool playing_ = false;
};

// The length of a MusyX song that `player` has started: two loops and a 10 s fade, or its last sound and a 0.5 s tail.
std::optional<Timing> AnalyzeMusyX(Player& player, const GameProfile& profile)
{
    MusyXSong song(player, profile);
    xsflib::rip::LengthRules rules;
    rules.frame_ms = 1000.0 * kLengthRules.block_frames / kLengthRules.sample_rate;
    rules.max_frames = static_cast<uint64_t>(kLengthRules.limit_ms / rules.frame_ms);
    rules.silent_frames = static_cast<uint64_t>(kLengthRules.silence_ms / rules.frame_ms);
    rules.max_ms = static_cast<double>(kLengthRules.limit_ms);
    rules.fade_ms = static_cast<double>(kLengthRules.loop_fade_ms);
    rules.tail_ms = static_cast<double>(kLengthRules.tail_ms);

    return xsflib::rip::MeasureLength(song, rules);
}

} // namespace

struct LengthAnalyzer::Impl
{
    LoadedSet library;
    GameProfile profile;
};

LengthAnalyzer::LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile)
    : impl_(std::make_unique<Impl>())
{
    PsfFile psf;
    if (auto err = ParsePsf(library, psf))
    {
        throw std::runtime_error("the library can't be read: " + *err);
    }

    ProgramChunks chunks;
    if (auto err = ParseProgram(psf.program, chunks))
    {
        throw std::runtime_error("the library can't be read: " + *err);
    }

    impl_->library.memory = std::move(chunks.memory);
    impl_->library.files = std::move(chunks.files);
    impl_->library.descriptor = ProcessDescriptor::Parse(psf.reserved);
    if (!impl_->library.descriptor)
    {
        throw std::runtime_error("the library has no process descriptor");
    }

    impl_->profile = profile;
}

LengthAnalyzer::~LengthAnalyzer() = default;

std::optional<Timing> LengthAnalyzer::Analyze(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                                              std::vector<jaudio::WaveUse>* played) const
{
    std::set<jaudio::WaveUse> found;
    std::optional<Timing> timing = Play(id, file, waves, played ? &found : nullptr, false);
    if (played)
    {
        played->assign(found.begin(), found.end());
    }

    return timing;
}

std::optional<std::vector<jaudio::WaveUse>> LengthAnalyzer::FindWaves(uint32_t id, const FileChunk* file) const
{
    std::set<jaudio::WaveUse> found;
    if (!Play(id, file, nullptr, &found, true))
    {
        return std::nullopt;
    }

    return std::vector<jaudio::WaveUse>(found.begin(), found.end());
}

std::optional<Timing> LengthAnalyzer::Play(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                                           std::set<jaudio::WaveUse>* found, bool lookups_sound) const
{
    const GameProfile& profile = impl_->profile;
    LoadedSet set = impl_->library;
    std::vector<uint8_t> sound(4);
    xsflib::StoreBe32(sound.data(), id);
    set.memory.push_back({DriverBlock::kAddress + kParamsSoundOffset, sound});
    if (waves)
    {
        set.memory.push_back({DriverBlock::kAddress + kParamsWavesOffset, WaveList(*waves)});
    }

    if (file)
    {
        set.files.push_back(*file);
    }

    Player player;
    if (!player.Load(std::move(set)) || !player.Start())
    {
        return std::nullopt;
    }

    if (profile.library == SoundLibrary::kMusyX)
    {
        return AnalyzeMusyX(player, profile);
    }

    // An AutoBgm piece never repeats. Once its root track idles in its polling routine, the passes of its other tracks
    // would still look like a loop's. Its root track's polls therefore stay moves. They hold back every loop, and the
    // piece plays to the analysis's limit.
    ntdlib::jaudio::GameWatch watch = WatchOf(profile);
    if (IsAutoBgm(profile, id))
    {
        watch.idle_in_calls = false;
    }

    AnalysisMachine machine(player);
    return ntdlib::jaudio::AnalyzeGameSound(machine, DriverBlock::kAddress + kParamsHandleOffset, watch, kLengthRules,
                                            found, lookups_sound, id);
}

} // namespace gcsf::rip
