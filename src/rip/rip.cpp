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
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "ntdlib/disc/compression.h"
#include "ntdlib/disc/header.h"
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
#include "rip/driver_animal_crossing_blob.h"
#include "rip/driver_blob.h"
#include "rip/driver_collectors_edition_blob.h"
#include "rip/driver_colosseum_bonus_blob.h"
#include "rip/driver_crystal_chronicles_blob.h"
#include "rip/driver_double_dash_blob.h"
#include "rip/driver_jungle_beat_blob.h"
#include "rip/driver_luigis_mansion_blob.h"
#include "rip/driver_mario_golf_blob.h"
#include "rip/driver_musyx_blob.h"
#include "rip/driver_pacman_vs_blob.h"
#include "rip/driver_pikmin2_blob.h"
#include "rip/driver_pikmin_blob.h"
#include "rip/driver_super_mario_sunshine_blob.h"
#include "rip/driver_twilight_princess_blob.h"
#include "rip/driver_warioware_blob.h"
#include "rip/driver_zel_blob.h"
#include "rip/musyx.h"

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
    case Driver::kCrystalChronicles:
        return DriverImage{crystal_chronicles::kDriverBlob, crystal_chronicles::kDriverBase,
                           crystal_chronicles::kDriverEntry, crystal_chronicles::kDriverParams};
    case Driver::kAnimalCrossing:
        return DriverImage{animal_crossing::kDriverBlob, animal_crossing::kDriverBase, animal_crossing::kDriverEntry,
                           animal_crossing::kDriverParams};
    case Driver::kColosseumBonus:
        return DriverImage{colosseum_bonus::kDriverBlob, colosseum_bonus::kDriverBase, colosseum_bonus::kDriverEntry,
                           colosseum_bonus::kDriverParams};
    case Driver::kMusyX:
        return DriverImage{musyx::kDriverBlob, musyx::kDriverBase, musyx::kDriverEntry, musyx::kDriverParams};
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

// Final Fantasy Crystal Chronicles (USA). Its sound library is Square's Red driver (RedSound, built 2003-06-17) on the
// SDK's AX, under a thin layer of the game's code, CSound. The game's system set-up (0x80021E74) sets up the memory
// manager, the sound manager and the file manager, in that order. The game's scripts load a piece's wave bank, then
// the piece, then play it, through CSound's LoadWave, LoadBgm and PlayBgm. The game loads its blocks of effects and
// the wave banks 0 and 500 once, through CSound::LoadBlock (0x800C6284). Those banks take Red's first two slots for
// banks. Any effect's track can pick a wave from them by the slot (command 0x21). The game's scripts load an area's
// wave bank and the area's SEP files through LoadWave and LoadSe. Every effect plays through CSound::PlaySe
// (0x800C5C1C), or through a function that places the effect in 3D. The game's calls of PlaySe pass a pan of 0x40, a
// volume of 127 and no fade-in. The names come from the disc's map (game.MAP), a link map of another build. Each
// address was found in main.dol and checked against its code. The parameters follow driver/crystal_chronicles.c's
// Params from os_sleep_thread.
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
            0x80180840, // OSSleepThread
            0x8001f6e4, // CMemory::Init
            0x8023b848, // the memory manager
            0x800c7970, // CSound::Init
            0x802f1694, // the sound manager
            0x80013b98, // CFile::Init
            0x8021dd00, // the file manager
            0x80013948, // CFile::Open
            0x80013790, // CFile::Read
            0x800135ec, // CFile::SyncCompleted
            0x8001364c, // CFile::Close
            0x801b58f0, // sprintf
            0x801da194, // "dvd/sound/music/music%03d.bgm"
            0x800c5f20, // CSound::LoadWave
            0x800c66b0, // CSound::LoadBgm
            0x800c6630, // CSound::PlayBgm
            0x801da124, // "dvd/sound/se/sep/se%06d.sep"
            0x800c6284, // CSound::LoadBlock
            0x800c6108, // CSound::LoadSe
            0x800c5c1c, // CSound::PlaySe
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
// come from the disc's link map of main.dol (static.map). Each address was checked against main.dol's code. The
// parameters follow driver/animal_crossing.c's Params from frame_retraces.
//
// The sound library plays the music in sequence player 1 or 3 and the effects in player 0. A piece of music that plays
// the hour holds the arrangements for three kinds of weather. The game mutes the channels of the others with masks
// from the tables BGM_MUTE_TABLE_FINE, _SNOW and _SAKURA. When it rains, the game plays another piece in place of the
// hour's (see ListSongs).
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
            2,          // a frame is two retraces
            3,          // JKRAram's ARInit takes a list of 3 blocks
            0x800c3140, // nintendo_hi_0, the sound of the start-up screen
            0x66a0,     // its size, as the game's sound_initial gives it
            0x80211f90, // AG + 0x3788: the sequence players
            0x160,      // the size of a player
            0x80217bc0, // sou_now_bgm_handle: the player that plays the music
            0x80218361, // sou_bgm_call_buffer: the request to the sound frame
            0x800788a4, // OSInit
            0x80079074, // OSInitAlarm
            0x800850f4, // DVDInit
            0x800882c8, // VIInit
            0x80088740, // VIWaitForRetrace
            0x8008c3cc, // ARInit
            0x8008cff4, // ARQInit
            0x80010c8c, // Na_InitAudio
            0x80010d1c, // Na_GameFrame
            0x80015548, // Na_CheckNeosBoot
            0x80011158, // Na_Tenki
            0x80011170, // Na_BgmStart
            128,        // the first music disc's piece (Sou_Na_BgmStart)
            182,        // the last
            9,          // a hi-fi stereo
            0x8001405c, // Na_MDPlayerPos
            0x80015578, // Na_Museum
            0,          // the sequence player of the effects (the game's SE_GROUP)
            0x38,       // a player's channels
            0x800eb670, // sou_trg_se: the slots of the effects that play
            6,          // the slots
            0x20,       // a slot's size
            0x8000c784, // Sou_TrgStart
            0x800a9838, // SEQ_TABLE
            92,         // the first parts of Kapp'n's songs (sing_1st_part_bgm_no, in the boat's code)
            111,        // the last
            112,        // their second parts (sing_2nd_part_bgm_no)
            121,        // the last
            126,        // a piece whose sequence (247) ends at once
            0x158,      // a sequence player's ports (Nas_GroupSeq)
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
// version and assertion strings, and the sound library's by the program's calls. Each address was checked against
// the program's code. The parameters follow driver/colosseum_bonus.c's Params from vi_init.
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
            0x80041244, // VIInit
            0x800416f4, // VIWaitForRetrace
            0x80042fbc, // AIInit
            0x800436d8, // ARInit
            0x80045208, // ARQInit
            0x80038314, // OSGetArenaLo
            0x8003830c, // OSGetArenaHi
            0x8002b718, // DVDOpen
            0x8002bb18, // DVDReadPrio
            0x8009a24c, // the sound library's allocator
            0x800976d4, // the sound library's set-up, with the program's values:
            0x800000,   // 8 MiB of ARAM,
            0x40,       // 64 entries for the effects that play,
            0x20,       // and 32 tracks
            0x80096f6c, // the scripts' file, once in memory
            0x800ac804, // "pm6fgm.sem"
            0x8009aa6c, // a bank in ARAM
            0x513100,   // its size, as the program gives it
            0x8009a8ec, // the bank's samples, from the disc
            0x800ac810, // "pm6fgm.ssm"
            0x80096f28, // the function through which the program plays an effect
            0x800976fc, // the count of the effects that play
            0x800f4fc8, // the head of their list
        },
    .library_name = "ColosseumBonus",
    .tgc = "tgc/pokedownload.tgc",
    .program_id = "POKE01",
};

// Metroid Prime (USA, disc version 2). Its sound library is MusyX 2.0, under Retro Studios' audio system. Its music
// streams from the disc, apart from the four songs in MidiData.pak. The parameters follow driver/musyx.c's Params from
// os_sleep_thread.
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
        {
            0x80386124, // OSSleepThread
            0x8037f594, // OSGetArenaLo
            0x8037f58c, // OSGetArenaHi
            0x802d6d70, // the start-up of the OS and the disc
            0,
            0,
            0,
            0x803722b4, // DVDOpen
            0x803725c4, // DVDReadAsyncPrio
            0x80374b04, // DVDGetCommandBlockStatus
            0x8037237c, // DVDClose
            0x8036e720, // ARInit
            0x805a9cfc, // the ARAM allocation stack
            2,          // its entries
            0x80370230, // ARQInit
            0x8036e004, // AIInit
            0x803b4f20, // sndSetHooks
            0x803b1bd8, // sndInit
            1,          // sndInit takes the number of studios
            48,         // voices
            48,         // music
            48,         // sfx
            1,          // studios
            1,          // SND_FLAGS_DEFAULT_STUDIO_DPL2
            0x5fc000,   // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x8039d460, // sndOutputMode
            2,          // SND_OUTPUTMODE_SURROUND
            0x803ab444, // sndPushGroup
            0x803abbf8, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x8039d124, // sndSeqVolume
            0x8039d2a8, // sndFXStartEx
            // no aux effects
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0x8039d36c, // sndVolume
        },
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
// Namco's sound layer. The parameters follow driver/musyx.c's Params from os_sleep_thread.
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
        {
            0x800fce04, // OSSleepThread
            0x800f6164, // OSGetArenaLo
            0x800f615c, // OSGetArenaHi
            0x80100314, // DVDInit
            0,
            0,
            0,
            0x800ffd68, // DVDOpen
            0x80100078, // DVDReadAsyncPrio
            0x8010245c, // DVDGetCommandBlockStatus
            0x800ffe30, // DVDClose
            0x801078b4, // ARInit
            0x802cadd8, // the ARAM allocation stack
            0x40,       // its entries
            0x801093c4, // ARQInit
            0x801072b0, // AIInit
            0x8014fb94, // sndSetHooks
            0x8014c2e4, // sndInit
            1,          // sndInit takes the number of studios
            64,         // voices
            42,         // music
            22,         // sfx
            1,          // studios
            0,          // SND_FLAGS_DEFAULT
            0x77f000,   // the ARAM's size for MusyX: up to 0x783000
            // nothing after sndInit
            0,
            0,
            0x80139000, // sndOutputMode
            2,          // SND_OUTPUTMODE_SURROUND
            0x8014649c, // sndPushGroup
            0x80146c8c, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x80138d24, // sndSeqVolume
            0x80138e48, // sndFXStartEx
            0x801390ec, // sndSetAuxProcessingCallbacks
            0x80150e34, // sndAuxCallbackReverbSTD, for aux A
            0x80150e34, // and for aux B
            0x802cc464, // aux A's reverb
            0x802cc310, // aux B's reverb
            0x80150ee4, // sndAuxCallbackPrepareReverbSTD, for aux A
            0x80150ee4, // and for aux B
            0xff,       // no MIDI channel controls aux A
            0xff,       // or aux B
        },
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

// Star Fox Adventures (USA, disc version 1). Its sound library is MusyX, under Rare's sound layer. The parameters
// follow driver/musyx.c's Params from os_sleep_thread.
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
        {
            0x802471c4, // OSSleepThread
            0x80241df0, // OSGetArenaLo
            0x80241de8, // OSGetArenaHi
            0x80249958, // DVDInit
            0,
            0,
            0,
            0x80249300, // DVDOpen
            0x80249610, // DVDReadAsyncPrio
            0x8024ba84, // DVDGetCommandBlockStatus
            0x802493c8, // DVDClose
            0x80250838, // ARInit
            0x803369f4, // the ARAM allocation stack
            10,         // its entries
            0x80251460, // ARQInit
            0x8025024c, // AIInit
            0x8028469c, // sndSetHooks
            0x802817a8, // sndInit
            1,          // sndInit takes the number of studios
            48,         // voices
            48,         // music
            24,         // sfx
            1,          // studios
            1,          // SND_FLAGS_DEFAULT_STUDIO_DPL2
            0x1000000,  // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x802731c8, // sndOutputMode
            1,          // SND_OUTPUTMODE_STEREO
            0x8027be90, // sndPushGroup
            0x8027c140, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x80272e84, // sndSeqVolume
            0,          // sndFXStartEx: no effects yet
            0x802732c0, // sndSetAuxProcessingCallbacks
            0x80285324, // sndAuxCallbackReverbSTD, for aux A
            0,          // nothing for aux B
            0x803368a0, // aux A's reverb
            0,
            0x80285380, // sndAuxCallbackPrepareReverbSTD, for aux A
            0,
            0xff, // no MIDI channel controls aux A
            0xff, // or aux B
        },
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

// Star Wars Rogue Squadron II: Rogue Leader (USA). Its sound library is MusyX. The parameters follow driver/musyx.c's
// Params from os_sleep_thread.
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
        {
            0x8002f010, // OSSleepThread
            0x8002908c, // OSGetArenaLo
            0x80029084, // OSGetArenaHi
            0,          // the start-up of the user's code initialises the disc
            0,
            0,
            0,
            0x80038e10, // DVDOpen
            0x80039120, // DVDReadAsyncPrio
            0x8003af74, // DVDGetCommandBlockStatus
            0x80038ed8, // DVDClose
            0x8004014c, // ARInit
            0x80095ca0, // the ARAM allocation stack
            2,          // its entries
            0x80041364, // ARQInit
            0x8004193c, // AIInit
            0x80026174, // sndSetHooks
            0x8002300c, // sndInit
            1,          // sndInit takes the number of studios
            56,         // voices
            40,         // music
            32,         // sfx
            1,          // studios
            3,          // SND_FLAGS_DEFAULT_STUDIO_DPL2 and SND_FLAGS_EMITTERGROUPSUSEMAXVOICES
            0x9fc000,   // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x80011ff0, // sndOutputMode
            2,          // SND_OUTPUTMODE_SURROUND
            0x8001cc50, // sndPushGroup
            0x8001d040, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x80011b10, // sndSeqVolume
            0,          // sndFXStartEx: no effects yet
            0x80012070, // sndSetAuxProcessingCallbacks
            0x80026ea4, // sndAuxCallbackReverbHI, for aux A
            0,          // nothing for aux B
            0x800ae1e0, // aux A's reverb
            0,
            0x80026f00, // sndAuxCallbackPrepareReverbHI, for aux A
            0,
            0xff, // no MIDI channel controls aux A
            0xff, // or aux B
        },
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

// Pokémon Colosseum (USA). Its sound library is MusyX, under the game's sound layer. The parameters follow
// driver/musyx.c's Params from os_sleep_thread.
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
        {
            0x800a238c, // OSSleepThread
            0x8009afb8, // OSGetArenaLo
            0x8009afb0, // OSGetArenaHi
            0,          // OSInit initialises the disc
            0,
            0,
            0,
            0x800a501c, // DVDOpen
            0x800a532c, // DVDReadAsyncPrio
            0x800a7728, // DVDGetCommandBlockStatus
            0x800a50e4, // DVDClose
            0x800aca80, // ARInit
            0x80478dc0, // the ARAM allocation stack
            2,          // its entries
            0x800ae5c0, // ARQInit
            0x800ac440, // AIInit
            0x801631ac, // sndSetHooks
            0x8015fe88, // sndInit
            1,          // sndInit takes the number of studios
            64,         // voices
            48,         // music
            16,         // sfx
            1,          // studios
            0,          // SND_FLAGS_DEFAULT
            0x9fc000,   // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x8014d9bc, // sndOutputMode
            1,          // SND_OUTPUTMODE_STEREO
            0x80159ef0, // sndPushGroup
            0x8015a368, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x8014d6d8, // sndSeqVolume
            0,          // sndFXStartEx: no effects yet
            0x8014daa8, // sndSetAuxProcessingCallbacks
            0x8016442c, // sndAuxCallbackReverbHI, for aux A
            0,          // nothing for aux B
            0x80452500, // aux A's reverb
            0,
            0x801644e0, // sndAuxCallbackPrepareReverbHI, for aux A
            0,
            0xff, // no MIDI channel controls aux A
            0xff, // or aux B
        },
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
// under Capcom's sound layer. The parameters follow driver/musyx.c's Params from os_sleep_thread.
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
        {
            0x8019f9d4, // OSSleepThread
            0x80198dcc, // OSGetArenaLo
            0x80198dc4, // OSGetArenaHi
            0x801a5ae0, // DVDInit
            0,
            0,
            0,
            0x801a5540, // DVDOpen
            0x801a58b0, // DVDReadAsyncPrio
            0x801a7dfc, // DVDGetCommandBlockStatus
            0x801a5608, // DVDClose
            0x801ad1cc, // ARInit
            0x8031b140, // the ARAM allocation stack
            0x18,       // its entries
            0x801aecdc, // ARQInit
            0x801acaec, // AIInit
            0x801e6598, // sndSetHooks
            0x801e32d8, // sndInit
            1,          // sndInit takes the number of studios
            64,         // voices
            32,         // music
            32,         // sfx
            1,          // studios
            0,          // SND_FLAGS_DEFAULT
            0xa00000,   // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x801cf3ec, // sndOutputMode
            1,          // SND_OUTPUTMODE_STEREO
            0x801dcc8c, // sndPushGroup
            0x801dd440, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x801cf0dc, // sndSeqVolume
            0x801cf200, // sndFXStartEx
            0x801cf4d8, // sndSetAuxProcessingCallbacks
            0x801e8040, // sndAuxCallbackReverbHI, for aux A
            0x801e8040, // and for aux B
            0x8031b1a0, // aux A's reverb
            0x8031b380, // aux B's reverb
            0x801e809c, // sndAuxCallbackPrepareReverbHI, for aux A
            0x801e809c, // and for aux B
            0xff,       // no MIDI channel controls aux A
            0xff,       // or aux B
        },
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

// Metroid Prime 2: Echoes (USA). Its sound library is a later build of MusyX 2.0, under Retro Studios' audio system.
// It streams its music. Its MusyX groups are all effects groups. The parameters follow driver/musyx.c's Params from
// os_sleep_thread.
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
        {
            0x80375eac, // OSSleepThread
            0x8036ebc4, // OSGetArenaLo
            0x8036ebbc, // OSGetArenaHi
            0x8028bf68, // the start-up of the OS and the disc
            0,
            0,
            0,
            0x8036017c, // DVDOpen
            0x8036048c, // DVDReadAsyncPrio
            0x80362a20, // DVDGetCommandBlockStatus
            0x80360244, // DVDClose
            0x8035533c, // ARInit
            0x803c5ab8, // the ARAM allocation stack
            3,          // its entries
            0x80356e4c, // ARQInit
            0x80381b4c, // AIInit
            0x803a1d30, // sndSetHooks
            0x8039e168, // sndInit
            1,          // sndInit takes the number of studios
            48,         // voices
            48,         // music
            48,         // sfx
            3,          // studios
            1,          // SND_FLAGS_DEFAULT_STUDIO_DPL2
            0x400000,   // the ARAM's size for MusyX in a rip (see above)
            // nothing after sndInit
            0,
            0,
            0x80389a58, // sndOutputMode
            2,          // SND_OUTPUTMODE_SURROUND
            0x803974dc, // sndPushGroup
            0x80397dd4, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x803895a4, // sndSeqVolume
            0x803897cc, // sndFXStartEx
            // no aux effects
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0x80389964, // sndVolume
        },
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
// streams its music. The parameters follow driver/musyx.c's Params from os_sleep_thread.
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
        {
            0x8029d5b0, // OSSleepThread
            0x8029673c, // OSGetArenaLo
            0x80296734, // OSGetArenaHi
            0x802a0500, // DVDInit
            0,
            0,
            0,
            0x802a00b0, // DVDOpen
            0x802a03c0, // DVDReadPrio, in place of DVDReadAsyncPrio (see above)
            0x802a2710, // DVDGetCommandBlockStatus
            0x802a0178, // DVDClose
            0x802a8d3c, // ARInit
            0x8041eb24, // the ARAM allocation stack
            8,          // its entries
            0x802aa87c, // ARQInit
            0x802a868c, // AIInit
            0x802915c0, // sndSetHooks
            0x8028dacc, // sndInit
            1,          // sndInit takes the number of studios
            64,         // voices
            12,         // music
            44,         // sfx
            1,          // studios
            1,          // SND_FLAGS_DEFAULT_STUDIO_DPL2
            0xbfc000,   // the ARAM's size for MusyX
            // nothing after sndInit
            0,
            0,
            0x8027a300, // sndOutputMode
            1,          // SND_OUTPUTMODE_STEREO
            0x802877f0, // sndPushGroup
            0x80287fe0, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x80279eac, // sndSeqVolume
            0x8027a0d4, // sndFXStartEx
            0x8027a3ec, // sndSetAuxProcessingCallbacks
            0x80292d78, // sndAuxCallbackReverbHI, for aux A
            0x80294058, // sndAuxCallbackChorus, for aux B
            0x803e194c, // aux A's reverb
            0x803e175c, // aux B's chorus
            0x80292dd4, // sndAuxCallbackPrepareReverbHI, for aux A
            0x80294314, // sndAuxCallbackPrepareChorus, for aux B
            0xff,       // no MIDI channel controls aux A
            0xff,       // or aux B
            0x8027a2a0, // sndVolume
            0x80279fd4, // sndFXCtrl
            0x8027a034, // sndFXCtrl14
        },
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

// Star Wars Rogue Squadron III: Rebel Strike (USA). Its sound library is MusyX. The parameters follow driver/musyx.c's
// Params from os_sleep_thread.
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
        {
            0x80026a88, // OSSleepThread
            0x800216a4, // OSGetArenaLo
            0x8002169c, // OSGetArenaHi
            0x80027dd4, // DVDInit
            0,
            0,
            0,
            0x80027650, // DVDOpen
            0x80027ab4, // DVDReadAsyncPrio
            0x80029ed8, // DVDGetCommandBlockStatus
            0x80027718, // DVDClose
            0x80030180, // ARInit
            0,          // no ARAM allocation stack: the loader's start-up calls ARInit
            0,
            0x80031c90, // ARQInit
            0x80032270, // AIInit
            0x7f65a060, // sndSetHooks
            0x7f65655c, // sndInit
            1,          // sndInit takes the number of studios
            56,         // voices
            20,         // music
            50,         // sfx
            1,          // studios
            3,          // SND_FLAGS_DEFAULT_STUDIO_DPL2 and SND_FLAGS_EMITTERGROUPSUSEMAXVOICES
            0x4fc000,   // the ARAM's size for MusyX: 0x500000 less ARAM's base
            // nothing after sndInit
            0,
            0,
            0x7f644b28, // sndOutputMode
            2,          // SND_OUTPUTMODE_SURROUND
            0x7f653608, // sndPushGroup
            0x7f653df8, // sndSeqPlayEx
            1,          // sndSeqPlayEx takes a studio
            0x7f6444b0, // sndSeqVolume
            0x7f6446d8, // sndFXStartEx
            0,          // no aux effects
            0,
            0,
            0,
            0,
            0,
            0,
            0xff,
            0xff,
            0x7f644ac8, // sndVolume
        },
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

const std::vector<GameProfile> kProfiles = {
    kFourSwordsUsa,        kFourSwordsJpn,     kTwilightPrincessUsa, kWindWakerUsa,          kMarioGolfUsa,
    kMarioPowerTennisUsa,  kWarioWareUsa,      kLuigisMansionUsa,    kSuperMarioSunshineUsa, kPikminUsa,
    kJungleBeatUsa,        kDoubleDashUsa,     kPikmin2Usa,          kPacManVsUsa,           kCollectorsEditionUsa,
    kCrystalChroniclesUsa, kAnimalCrossingUsa, kColosseumBonusUsa,   kMetroidPrimeUsa,       kTalesOfSymphoniaUsa,
    kStarFoxAdventuresUsa, kRogueLeaderUsa,    kColosseumUsa,        kResidentEvilZeroUsa,   kMetroidPrime2Usa,
    kPaperMarioUsa,        kRebelStrikeUsa};

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

// A TGC's header: its magic, and where its parts lie in it. The program's disc header comes first, then bi2.bin and
// the apploader. The file system table gives each file's offset as if the files began at kTgcTableFiles. They begin at
// kTgcFiles in the TGC. The values were read from the Pokémon Colosseum Bonus Disc's TGC. Its file system table then
// finds each file's data.
constexpr uint32_t kTgcMagic = 0xae0f38a2;
constexpr uint32_t kTgcHeader = 0x08;     // the program's disc header
constexpr uint32_t kTgcFst = 0x10;        // the file system table's offset, then its size
constexpr uint32_t kTgcDol = 0x1c;        // main.dol's offset, then its size
constexpr uint32_t kTgcFiles = 0x24;      // the files' offset in the TGC
constexpr uint32_t kTgcTableFiles = 0x34; // and in the file system table

// bi2.bin and the apploader follow the disc header. The apploader's header gives its code's size at 0x14 and its
// trailer's at 0x18.
constexpr uint32_t kBi2Size = 0x2000;
constexpr uint32_t kApploaderHeaderSize = 0x20;

// The program in a TGC, as LoadGame would give it from the program's disc (see OpenProgram). `source` names the
// image that holds the TGC. A file system table entry is 12 bytes: a word with a folder's flag in the top byte and the
// name's offset below, then a file's offset and size, or a folder's parent and the entry after its last. The first
// entry is the root folder. The names follow the entries. Throws xsflib::FormatError for a damaged TGC.
disc::GameFiles ReadTgc(std::shared_ptr<const std::vector<uint8_t>> tgc, const std::string& source)
{
    const xsflib::Reader r(*tgc, true);
    if (r.U32(0) != kTgcMagic)
    {
        throw xsflib::FormatError("it isn't a TGC");
    }

    const auto part = [&r](uint32_t offset, uint32_t size)
    {
        const std::span<const uint8_t> bytes = r.Sub(offset, size);
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };

    disc::GameFiles game;
    game.source = "TGC in " + source;
    const uint32_t header = r.U32(kTgcHeader);
    game.boot = part(header, disc::kHeaderSize);
    game.bi2 = part(header + disc::kHeaderSize, kBi2Size);
    const uint32_t apploader = header + disc::kHeaderSize + kBi2Size;
    game.apploader = part(apploader, kApploaderHeaderSize + r.U32(apploader + 0x14) + r.U32(apploader + 0x18));
    game.dol = part(r.U32(kTgcDol), r.U32(kTgcDol + 4));
    game.fst = part(r.U32(kTgcFst), r.U32(kTgcFst + 4));
    game.region = xsflib::LoadBe32(game.bi2.data() + 0x18);

    // The files, by their paths. A folder's entries run up to its last entry.
    const xsflib::Reader fst(game.fst, true);
    const uint32_t entries = fst.U32(8);
    if (entries == 0 || uint64_t{entries} * 12 > game.fst.size())
    {
        throw xsflib::FormatError("its file system table is damaged");
    }

    const uint32_t files = r.U32(kTgcFiles);
    const uint32_t table_files = r.U32(kTgcTableFiles);
    std::map<std::string, std::pair<uint32_t, uint32_t>> places;
    std::vector<std::pair<uint32_t, std::string>> folders = {{entries, ""}};
    for (uint32_t i = 1; i < entries; i++)
    {
        while (i >= folders.back().first && folders.size() > 1)
        {
            folders.pop_back();
        }

        const uint32_t word = fst.U32(i * 12);
        const std::string name = fst.CString(entries * 12 + (word & 0xffffff), 255);
        const std::string path = folders.back().second + name;
        if (word >> 24)
        {
            folders.emplace_back(fst.U32(i * 12 + 8), path + "/");
            continue;
        }

        const uint32_t offset = fst.U32(i * 12 + 4);
        const uint32_t size = fst.U32(i * 12 + 8);
        if (offset < table_files || uint64_t{offset} - table_files + files + size > tgc->size())
        {
            throw xsflib::FormatError(path + " lies outside the TGC");
        }

        places[path] = {offset - table_files + files, size};
        game.files.push_back({path, size, i});
    }

    std::sort(game.files.begin(), game.files.end(),
              [](const disc::GameFile& a, const disc::GameFile& b) { return a.path < b.path; });
    game.read_file = [tgc, places](const std::string& path, std::vector<uint8_t>& data)
    {
        const auto place = places.find(path);
        if (place == places.end())
        {
            return false;
        }

        data.assign(tgc->begin() + place->second.first, tgc->begin() + place->second.first + place->second.second);
        return true;
    };

    return game;
}

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

std::optional<std::string> OpenProgram(disc::GameFiles& game)
{
    for (const GameProfile& profile : kProfiles)
    {
        if (!profile.tgc || game.GameId() != profile.game_id ||
            std::none_of(game.files.begin(), game.files.end(),
                         [&profile](const disc::GameFile& file) { return file.path == profile.tgc; }))
        {
            continue;
        }

        auto tgc = std::make_shared<std::vector<uint8_t>>();
        if (!game.read_file || !game.read_file(profile.tgc, *tgc))
        {
            return std::string("can't read ") + profile.tgc;
        }

        try
        {
            game = ReadTgc(tgc, game.source);
        }
        catch (const xsflib::FormatError& e)
        {
            return std::string(profile.tgc) + ": " + e.what();
        }

        return std::nullopt;
    }

    return std::nullopt;
}

bool ListsSongs(const GameProfile& profile)
{
    return profile.library == SoundLibrary::kMusyX || profile.library == SoundLibrary::kJac ||
           profile.library == SoundLibrary::kRed || profile.library == SoundLibrary::kNintendo64 ||
           profile.library == SoundLibrary::kHal;
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

namespace
{

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

// A Red game's names of its files: a prefix, the number in `digits` digits or more, and the extension. The game writes
// them with sprintf.
struct RedName
{
    std::string_view prefix;
    std::size_t digits;
    std::string_view extension;
};

constexpr RedName kRedPiece = {"music", 3, ".bgm"};
constexpr RedName kRedBank = {"wave", 4, ".wd"};
constexpr RedName kRedBlock = {"se", 3, ".seb"};
constexpr RedName kRedEffect = {"se", 6, ".sep"};

// A Red effect's sound ID is this plus the game's number of the effect (see ListSongs). A piece's is its number.
constexpr uint32_t kRedEffectId = 0x1000000;

// The game numbers a block's effect by the block's number times this, plus the effect's place in the block.
constexpr uint32_t kRedBlockStride = 1000;

// A Red game's files in `folder` that have the form of `name`, by their numbers. The game's disc library matches paths
// in any case. The match therefore ignores case. A file whose number has other digits than the game's sprintf writes
// is left out. A further leading zero is one example. The game never reads such a file.
std::map<uint32_t, std::string> RedFiles(const disc::GameFiles& game, std::string_view folder, const RedName& name)
{
    constexpr std::size_t kMaxDigits = 8;
    std::map<uint32_t, std::string> found;
    for (const disc::GameFile& file : game.files)
    {
        const std::string path = xsflib::AsciiLower(file.path);
        if (!path.starts_with(folder) || !path.ends_with(name.extension))
        {
            continue;
        }

        const std::string_view rest = std::string_view(path).substr(folder.size());
        if (!rest.starts_with(name.prefix) || rest.size() < name.prefix.size() + name.extension.size())
        {
            continue;
        }

        const std::string_view digits =
            rest.substr(name.prefix.size(), rest.size() - name.prefix.size() - name.extension.size());
        const auto is_digit = [](char c)
        {
            return c >= '0' && c <= '9';
        };
        if (digits.empty() || digits.size() > kMaxDigits || !std::all_of(digits.begin(), digits.end(), is_digit))
        {
            continue;
        }

        const uint32_t number = static_cast<uint32_t>(std::stoul(std::string(digits)));
        std::string written = std::to_string(number);
        written.insert(0, name.digits > written.size() ? name.digits - written.size() : 0, '0');
        if (written == digits)
        {
            found.emplace(number, file.path);
        }
    }

    return found;
}

// The name of a Red game's file without its folder and extension.
std::string RedLabel(const std::string& path, const RedName& name)
{
    std::string label = path.substr(path.rfind('/') + 1);
    label.resize(label.size() - name.extension.size());
    return label;
}

// The wave bank of a Red sound: a little-endian half at `offset` in its information.
uint32_t RedInfoBank(std::span<const uint8_t> info, std::size_t offset)
{
    return info[offset] | static_cast<uint32_t>(info[offset + 1]) << 8;
}

// An effect of a Red game's block: its place in the block and its wave bank.
struct RedBlockEffect
{
    uint32_t place;
    uint32_t bank;
};

// The effects of a Red game's block, by place. A block's file starts with "SeBlock" and a zero byte. A big-endian half
// at 10 gives the number of places. A table of big-endian words follows at 16, one for each place. A free place's word
// is -1. Another place's word gives the offset of its effect's information from the table's end in its low 31 bits.
// The information's second and third bytes give its wave bank's number, the low byte first. Returns nothing if the
// file isn't a block.
std::optional<std::vector<RedBlockEffect>> RedBlockEffects(std::span<const uint8_t> block)
{
    constexpr std::size_t kTable = 16;
    constexpr std::size_t kBank = 1;
    constexpr uint32_t kFree = 0xffffffff;
    constexpr uint32_t kOffsetMask = 0x7fffffff;
    if (block.size() < kTable ||
        !std::equal(block.begin(), block.begin() + 8, std::string_view("SeBlock\0", 8).begin()))
    {
        return std::nullopt;
    }

    const std::size_t places = xsflib::LoadBe16(block.data() + 10);
    const std::size_t end = kTable + 4 * places;
    if (block.size() < end)
    {
        return std::nullopt;
    }

    std::vector<RedBlockEffect> effects;
    for (std::size_t place = 0; place < places; place++)
    {
        const uint32_t entry = xsflib::LoadBe32(block.data() + kTable + 4 * place);
        if (entry == kFree)
        {
            continue;
        }

        const std::size_t info = end + (entry & kOffsetMask);
        if (info + kBank + 2 > block.size())
        {
            return std::nullopt;
        }

        effects.push_back({static_cast<uint32_t>(place), RedInfoBank(block.subspan(info), kBank)});
    }

    return effects;
}

// A Red game's blocks of effects that the game loads, by number.
std::map<uint32_t, std::string> RedBlocks(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> blocks = RedFiles(game, profile.block_folder, kRedBlock);
    std::erase_if(blocks, [&profile](const auto& block) { return block.first >= profile.red_blocks; });
    return blocks;
}

// A Red game's effects in SEP files that the game can play, by number.
std::map<uint32_t, std::string> RedSeps(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> seps = RedFiles(game, profile.effect_folder, kRedEffect);
    std::erase_if(seps, [](const auto& sep) { return sep.first < kRedFirstSep; });
    return seps;
}

// A Red game's pieces of music and effects (see ListSongs).
std::vector<Sound> RedSounds(const disc::GameFiles& game, const GameProfile& profile)
{
    std::vector<Sound> sounds;
    for (const auto& [number, path] : RedFiles(game, profile.music_folder, kRedPiece))
    {
        sounds.push_back({number, RedLabel(path, kRedPiece), true});
    }

    if (!profile.effect_folder)
    {
        return sounds;
    }

    for (const auto& [number, path] : RedBlocks(game, profile))
    {
        std::vector<uint8_t> bytes;
        if (!game.read_file || !game.read_file(path, bytes))
        {
            throw xsflib::FormatError("can't read " + path);
        }

        const std::optional<std::vector<RedBlockEffect>> effects = RedBlockEffects(bytes);
        if (!effects)
        {
            throw xsflib::FormatError(path + " isn't a block of effects");
        }

        for (const RedBlockEffect& effect : *effects)
        {
            std::string place = std::to_string(effect.place);
            place.insert(0, place.size() < 3 ? 3 - place.size() : 0, '0');
            sounds.push_back({kRedEffectId + number * kRedBlockStride + effect.place,
                              RedLabel(path, kRedBlock) + "-" + place, false});
        }
    }

    for (const auto& [number, path] : RedSeps(game, profile))
    {
        sounds.push_back({kRedEffectId + number, RedLabel(path, kRedEffect), false});
    }

    return sounds;
}

// The weathers for which the game arranges a piece of music that plays the hour: its numbers for them, and the names
// that its tables of masks give them (BGM_MUTE_TABLE_FINE, _SNOW and _SAKURA). The game plays another piece in the
// rain, its weather 1. The game's falling leaves count as its weather 3.
struct N64Weather
{
    uint8_t number;
    const char* name;
};

constexpr N64Weather kN64Weathers[] = {{0, "fine"}, {2, "snow"}, {3, "sakura"}};

// The game's numbers of its pieces of music that play the hour.
constexpr uint32_t kN64FirstHour = 1;
constexpr uint32_t kN64LastHour = 24;

// The top byte of an effect's sound ID with the Nintendo 64's sound library. The driver takes the low 16 bits as the
// game's number of the effect.
constexpr uint32_t kN64EffectKind = 1;

// The most effects in a bank of the Nintendo 64's sound library. The game's table of the effects' priorities has an
// entry for each place in a bank (TRGPRIO, 128 bytes).
constexpr uint32_t kN64BankEffects = 128;

// The museum's piece of music, and its rooms by the game's numbers for them. Each room mutes some of the piece's
// channels (BGM_MUTE_TABLE_MUSEUM). The game plays the piece only in the museum.
constexpr uint32_t kN64Museum = 88;
constexpr const char* kN64MuseumRooms[] = {"lobby", "paintings", "fish", "insects", "fossils"};

// The game's numbers that its start of a piece of music (Sou_BgmStart) doesn't play as a piece: 0 stops the music, and
// 83 to 87 play the effects of the start-up screen. Numbers 112 to 121 are the second parts of Kapp'n's songs. The
// first part's script starts its second part. The game's driver plays a second part as a piece.
bool N64PlaysPiece(uint32_t number)
{
    return number != 0 && !(number >= 83 && number <= 87);
}

// A sequence's number in the game's table of sequences (GameProfile::sequence_table), following a sequence of size 0 to
// the sequence that it plays. Throws xsflib::FormatError for a number outside the table, or for a chain that doesn't
// end.
uint32_t N64Sequence(const ntdlib::Dol& dol, const GameProfile& profile, uint32_t number)
{
    constexpr uint32_t kEntries = 0x10;
    constexpr uint32_t kEntrySize = 0x10;
    constexpr int kMaxChain = 8;
    const std::span<const uint8_t> table = DolBytes(dol, profile.sequence_table);
    const uint32_t count = table.size() >= 2 ? xsflib::LoadBe16(table.data()) : 0;
    for (int chain = 0; chain < kMaxChain; chain++)
    {
        const uint32_t entry = kEntries + number * kEntrySize;
        if (number >= count || entry + kEntrySize > table.size())
        {
            break;
        }

        if (xsflib::LoadBe32(table.data() + entry + 4) != 0)
        {
            return number;
        }

        number = xsflib::LoadBe32(table.data() + entry);
    }

    throw xsflib::FormatError("main.dol's table of sequences lacks sequence " + std::to_string(number));
}

// A game's pieces of music with the Nintendo 64's sound library (see ListSongs). Throws xsflib::FormatError if a table
// lies outside the game's code.
std::vector<Sound> N64Pieces(const ntdlib::Dol& dol, const GameProfile& profile)
{
    constexpr uint32_t kPieces = 256;
    const std::span<const uint8_t> table = DolBytes(dol, profile.music_table);
    if (table.size() < kPieces)
    {
        throw xsflib::FormatError("main.dol's table of music ends early");
    }

    std::vector<Sound> sounds;
    std::set<uint32_t> sequences;
    for (uint32_t number = 0; number < kPieces; number++)
    {
        const uint32_t sequence = N64Sequence(dol, profile, table[number]);
        std::string label = std::to_string(number);
        label.insert(0, label.size() < 3 ? 3 - label.size() : 0, '0');
        label.insert(0, "bgm");
        if (number >= kN64FirstHour && number <= kN64LastHour)
        {
            for (const N64Weather& weather : kN64Weathers)
            {
                sounds.push_back({uint32_t{weather.number} << 8 | number, label + "-" + weather.name, true});
            }
        }
        else if (number == kN64Museum)
        {
            for (uint32_t room = 1; room <= std::size(kN64MuseumRooms); room++)
            {
                sounds.push_back({room << 16 | number, label + "-" + kN64MuseumRooms[room - 1], true});
            }
        }
        else if (N64PlaysPiece(number) && sequences.insert(sequence).second)
        {
            sounds.push_back({number, std::move(label), true});
        }
    }

    return sounds;
}

// A game's effects with the Nintendo 64's sound library (see GameProfile::n64_effects). A bank's table ends where
// another bank's table or an effect's script starts, or after 128 effects. An effect is named after its bank and its
// place, for example `se2-017`. The game's effect 0 is none: the game's slot of an effect holds 0 when it's free.
// Throws xsflib::FormatError if the tables lie outside the sound data.
std::vector<Sound> N64Effects(const disc::GameFiles& game, const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<uint8_t> rom;
    if (!game.read_file || !game.read_file(profile.sound_archive, rom))
    {
        throw xsflib::FormatError(std::string("can't read ") + profile.sound_archive);
    }

    const std::span<const uint8_t> table = DolBytes(dol, profile.sequence_table);
    const uint32_t entry = 0x10 + N64Sequence(dol, profile, profile.n64_effects) * 0x10;
    const uint64_t offset = xsflib::LoadBe32(table.data() + entry);
    const uint64_t size = xsflib::LoadBe32(table.data() + entry + 4);
    if (offset + size > rom.size() || profile.n64_effect_banks + 2 > size)
    {
        throw xsflib::FormatError("the effects sequence lies outside the sound data");
    }

    const std::span<const uint8_t> sequence(rom.data() + offset, size);
    const auto half = [&sequence](uint32_t at)
    {
        return uint32_t{xsflib::LoadBe16(sequence.data() + at)};
    };

    // The table of banks ends where the first bank's table starts.
    std::vector<uint32_t> banks;
    uint32_t end = static_cast<uint32_t>(size);
    for (uint32_t at = profile.n64_effect_banks; at + 2 <= end && banks.size() < 16; at += 2)
    {
        const uint32_t bank = half(at);
        if (bank <= profile.n64_effect_banks || bank >= size)
        {
            throw xsflib::FormatError("the effects sequence's table of banks is damaged");
        }

        banks.push_back(bank);
        end = std::min(end, bank);
    }

    std::vector<Sound> sounds;
    for (uint32_t number = 0; number < banks.size(); number++)
    {
        const uint32_t start = banks[number];
        uint32_t stop = static_cast<uint32_t>(size);
        for (const uint32_t other : banks)
        {
            if (other > start)
            {
                stop = std::min(stop, other);
            }
        }

        for (uint32_t place = 0; place < kN64BankEffects && start + 2 * place + 2 <= stop; place++)
        {
            const uint32_t script = half(start + 2 * place);
            if (script > start)
            {
                stop = std::min(stop, script);
            }

            if (start + 2 * place + 2 > stop)
            {
                break;
            }

            if (number == 0 && place == 0)
            {
                continue;
            }

            std::string label = std::to_string(place);
            label.insert(0, label.size() < 3 ? 3 - label.size() : 0, '0');
            sounds.push_back(
                {kN64EffectKind << 24 | number << 8 | place, "se" + std::to_string(number) + "-" + label, false});
        }
    }

    return sounds;
}

} // namespace

// The names that a HAL game's C header defines for its effects' IDs (see GameProfile::sound_names), by ID. A line
// "#define NAME ((GROUP<<16) + NUMBER)" names an effect. The header's comments are Shift-JIS. Its names are ASCII.
std::map<uint32_t, std::string> HalNames(const disc::GameFiles& game, const GameProfile& profile)
{
    std::map<uint32_t, std::string> names;
    std::vector<uint8_t> header;
    if (!profile.sound_names || !game.read_file || !game.read_file(profile.sound_names, header))
    {
        return names;
    }

    const std::string text(header.begin(), header.end());
    std::size_t line = 0;
    while (line < text.size())
    {
        const std::size_t end = std::min(text.find('\n', line), text.size());
        unsigned group = 0;
        unsigned number = 0;
        char name[64] = {};
        const std::string row = text.substr(line, end - line);
        if (std::sscanf(row.c_str(), "#define %63s ((%u<<16) + %u)", name, &group, &number) == 3 && group <= 0xffff &&
            number <= 0xffff)
        {
            names.emplace(group << 16 | number, name);
        }

        line = end + 1;
    }

    return names;
}

// A HAL game's effects (see GameProfile::sound_archive): every effect of the file of scripts, group by group. An
// effect's label is its name in the game's header, or else "se", its group, a hyphen and its place in the group in
// three digits.
std::vector<Sound> HalEffects(const disc::GameFiles& game, const GameProfile& profile)
{
    std::vector<uint8_t> scripts;
    if (!game.read_file || !game.read_file(profile.sound_archive, scripts))
    {
        throw xsflib::FormatError(std::string("can't read ") + profile.sound_archive);
    }

    const xsflib::Reader r(scripts, true);
    uint32_t at = 0;
    for (int table = 0; table < 2; table++)
    {
        at += 4 + 4 * r.U32(at);
    }

    const uint32_t groups = r.U32(at);
    std::vector<uint32_t> firsts;
    for (uint32_t group = 0; group < groups; group++)
    {
        firsts.push_back(r.U32(at + 4 + 4 * group));
    }

    const uint32_t effects = r.U32(at + 4 + 4 * groups);
    const std::map<uint32_t, std::string> names = HalNames(game, profile);
    std::vector<Sound> sounds;
    for (uint32_t group = 0; group < groups; group++)
    {
        const uint32_t end = group + 1 < groups ? firsts[group + 1] : effects;
        if (end < firsts[group] || end > effects || end - firsts[group] > 0x10000)
        {
            throw xsflib::FormatError(std::string(profile.sound_archive) + "'s table of groups is damaged");
        }

        for (uint32_t place = 0; place < end - firsts[group]; place++)
        {
            const uint32_t id = group << 16 | place;
            const auto name = names.find(id);
            std::string label = std::to_string(place);
            label.insert(0, label.size() < 3 ? 3 - label.size() : 0, '0');
            sounds.push_back(
                {id, name != names.end() ? name->second : "se" + std::to_string(group) + "-" + label, false});
        }
    }

    return sounds;
}

std::vector<Sound> ListSongs(const disc::GameFiles& game, const GameProfile& profile)
{
    if (profile.library == SoundLibrary::kRed)
    {
        return RedSounds(game, profile);
    }

    if (profile.library == SoundLibrary::kHal)
    {
        return HalEffects(game, profile);
    }

    if (profile.driver == Driver::kMusyX)
    {
        const MusyXCatalogue catalogue = GatherMusyX(game, profile);
        std::vector<Sound> sounds;
        for (std::size_t i = 0; i < catalogue.entries.size(); i++)
        {
            const MusyXCatalogue::Entry& entry = catalogue.entries[i];
            const bool song = entry.kind == MusyXCatalogue::Kind::kSong;
            const uint32_t key = uint32_t{entry.song_group} << 16 | entry.id;
            const std::vector<uint32_t>& streamed = profile.musyx_streamed_songs;
            const bool stream = song && std::find(streamed.begin(), streamed.end(), key) != streamed.end();
            sounds.push_back({static_cast<uint32_t>(i), entry.label, song, stream});
        }

        return sounds;
    }

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
    }

    if (profile.library == SoundLibrary::kNintendo64)
    {
        std::vector<Sound> sounds = N64Pieces(*dol, profile);
        if (profile.n64_effects)
        {
            std::vector<Sound> effects = N64Effects(game, *dol, profile);
            sounds.insert(sounds.end(), std::make_move_iterator(effects.begin()),
                          std::make_move_iterator(effects.end()));
        }

        return sounds;
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

// The part of a game's code and data that main.dol holds packed at `address` (see GameProfile::packed_module),
// unpacked. Throws xsflib::FormatError if it's damaged.
std::vector<uint8_t> UnpackModule(const ntdlib::Dol& dol, uint32_t address)
{
    const std::span<const uint8_t> packed = DolBytes(dol, address);
    if (packed.size() < 8 || xsflib::LoadBe32(packed.data()) > packed.size() - 8)
    {
        throw xsflib::FormatError("main.dol's packed code runs past its section");
    }

    std::vector<uint8_t> module(xsflib::LoadBe32(packed.data() + 4));
    try
    {
        if (ntdlib::disc::Inflate(packed.subspan(8, xsflib::LoadBe32(packed.data())), module) == module.size())
        {
            return module;
        }
    }
    catch (const std::exception&)
    {
    }

    throw xsflib::FormatError("main.dol's packed code is damaged");
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

    // The game's code and data, section by section. A game that maps part of them through the page table keeps that
    // part packed (GameProfile::packed_module). Its sections go where the profile's patches map them with a BAT. They
    // replace main.dol's sections that they overlap.
    std::vector<uint8_t> module_bytes;
    std::optional<ntdlib::Dol> module;
    if (profile.packed_module)
    {
        try
        {
            module_bytes = UnpackModule(*dol, profile.packed_module);
        }
        catch (const xsflib::FormatError& e)
        {
            return fail(e.what());
        }

        module = ntdlib::Dol::Parse(module_bytes);
        if (!module)
        {
            return fail("main.dol's packed code isn't a DOL");
        }
    }

    const auto overlaps_module = [&](const ntdlib::Dol::Section& section)
    {
        if (!module)
        {
            return false;
        }

        const uint64_t start = section.address, end = start + section.data.size();
        for (const ntdlib::Dol::Section& part : module->Sections())
        {
            const uint64_t part_start = uint64_t{part.address} + profile.module_offset;
            if (start < part_start + part.data.size() && part_start < end)
            {
                return true;
            }
        }

        return false;
    };

    ProgramBuilder program;
    for (const ntdlib::Dol::Section& section : dol->Sections())
    {
        if (!overlaps_module(section))
        {
            program.AddMemory(section.address, section.data.data(), section.data.size());
        }
    }

    if (module)
    {
        for (const ntdlib::Dol::Section& section : module->Sections())
        {
            program.AddMemory(section.address + profile.module_offset, section.data.data(), section.data.size());
        }
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

    // A game for driver/musyx.c: the disc's header, the catalogue, and the files of its groups and songs.
    if (profile.driver == Driver::kMusyX)
    {
        try
        {
            const MusyXCatalogue catalogue = GatherMusyX(game, profile);
            program.AddFile("sys/boot.bin", game.boot);
            program.AddFile(kMusyXCatalogue, CatalogueFile(catalogue));
            for (std::size_t i = 0; i < catalogue.sets.size(); i++)
            {
                const MusyXCatalogue::FileSet& set = catalogue.sets[i];
                const uint32_t number = static_cast<uint32_t>(i);
                program.AddFile(MusyXPath(number, ".proj"), set.project);
                program.AddFile(MusyXPath(number, ".pool"), set.pool);
                program.AddFile(MusyXPath(number, ".sdir"), set.sdir);
                program.AddFile(MusyXPath(number, ".samp"), set.samples);
            }

            for (std::size_t i = 0; i < catalogue.songs.size(); i++)
            {
                program.AddFile(MusyXPath(static_cast<uint32_t>(i), ".sng"), catalogue.songs[i]);
            }
        }
        catch (const xsflib::FormatError& e)
        {
            return fail(e.what());
        }

        return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
    }

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

    // A Red game: the disc's header, every piece of music, the blocks of effects that the game loads, the SEP files
    // that it can play, and the wave banks that they name. A piece's file starts with "BGM ", then the piece's number
    // and its bank's number, big-endian halves at 4 and 6. A SEP file starts with "SeSep". Its bank's number is a
    // little-endian half at 0x11. RedBlockEffects reads a block.
    if (profile.library == SoundLibrary::kRed)
    {
        constexpr std::size_t kBankOffset = 6;
        constexpr std::size_t kSepBankOffset = 0x11;
        const std::map<uint32_t, std::string> banks = RedFiles(game, profile.wave_folder, kRedBank);
        std::set<uint32_t> named;
        std::optional<std::string> error;
        const auto add = [&](const std::string& path, std::vector<uint8_t>& bytes)
        {
            if (!game.read_file || !game.read_file(path, bytes))
            {
                error = "can't read " + path;
                return false;
            }

            program.AddFile(path, bytes);
            return true;
        };
        const auto name = [&](const std::string& path, uint32_t bank)
        {
            if (!banks.contains(bank))
            {
                error = path + " names wave bank " + std::to_string(bank) + ". The disc doesn't have that bank";
                return false;
            }

            named.insert(bank);
            return true;
        };

        program.AddFile("sys/boot.bin", game.boot);
        for (const auto& [number, path] : RedFiles(game, profile.music_folder, kRedPiece))
        {
            std::vector<uint8_t> bytes;
            if (!add(path, bytes))
            {
                return fail(*error);
            }

            if (bytes.size() < kBankOffset + 2 || !std::equal(bytes.begin(), bytes.begin() + 4, "BGM "))
            {
                return fail(path + " isn't a piece of music");
            }

            if (!name(path, xsflib::LoadBe16(bytes.data() + kBankOffset)))
            {
                return fail(*error);
            }
        }

        if (profile.effect_folder)
        {
            for (const auto& [number, path] : RedBlocks(game, profile))
            {
                std::vector<uint8_t> bytes;
                if (!add(path, bytes))
                {
                    return fail(*error);
                }

                const std::optional<std::vector<RedBlockEffect>> effects = RedBlockEffects(bytes);
                if (!effects)
                {
                    return fail(path + " isn't a block of effects");
                }

                for (const RedBlockEffect& effect : *effects)
                {
                    if (!name(path, effect.bank))
                    {
                        return fail(*error);
                    }
                }
            }

            for (const auto& [number, path] : RedSeps(game, profile))
            {
                std::vector<uint8_t> bytes;
                if (!add(path, bytes))
                {
                    return fail(*error);
                }

                if (bytes.size() < kSepBankOffset + 2 || !std::equal(bytes.begin(), bytes.begin() + 5, "SeSep"))
                {
                    return fail(path + " isn't an effect");
                }

                if (!name(path, RedInfoBank(bytes, kSepBankOffset)))
                {
                    return fail(*error);
                }
            }
        }

        for (const uint32_t bank : named)
        {
            std::vector<uint8_t> bytes;
            if (!game.read_file(banks.at(bank), bytes))
            {
                return fail("can't read " + banks.at(bank));
            }

            program.AddFile(banks.at(bank), bytes);
        }

        return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
    }

    // A game with HAL Laboratory's sound library: the disc's header, the effects' scripts and their samples.
    if (profile.library == SoundLibrary::kHal)
    {
        program.AddFile("sys/boot.bin", game.boot);
        std::vector<std::string> paths = {profile.sound_archive};
        paths.insert(paths.end(), profile.other_sound_files.begin(), profile.other_sound_files.end());
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

    // A game with the Nintendo 64's sound library: the disc's header and the file of sound data.
    if (profile.library == SoundLibrary::kNintendo64)
    {
        std::vector<uint8_t> data;
        if (!game.read_file || !game.read_file(profile.sound_archive, data))
        {
            return fail(std::string("can't read ") + profile.sound_archive);
        }

        program.AddFile("sys/boot.bin", game.boot);
        program.AddFile(profile.sound_archive, data);

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

// A song in game mode, as xsflib's MeasureLength plays it (see xsflib/rip/length.h). The adapter follows the song
// through its sound library's state. A derived class reads that state: whether the song plays, and whether it has
// gone back to the start of its loop. A frame is one block of the analysis's output (kLengthRules.block_frames). The
// song also counts as playing before it first plays. Its loop has one target, 0. MusyX's and Red's output keeps a
// constant level on each channel once the song has stopped. Sound is therefore a sample that differs by more than
// xsflib's kSilenceLevel from its channel's recent level. That level is a running average over about 50 ms. An output
// that rests at 0 (`rests_at_zero`) counts a sample louder than kSilenceLevel as sound, as the other analyses do. The
// song rings while it has sounded in the last 0.5 s. The analysis therefore keeps the end of a release. The adapter
// follows no voices and finds no steady state.
class LibrarySong
{
public:
    explicit LibrarySong(Player& player, bool rests_at_zero = false) : player_(player), rests_at_zero_(rests_at_zero)
    {
    }

    virtual ~LibrarySong() = default;

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
            sounded_ = sounded_ || std::abs(block[i] - (rests_at_zero_ ? 0.0 : level)) > xsflib::rip::kSilenceLevel;
            level += (block[i] - level) / kLevelSamples;
        }

        if (sounded_)
        {
            last_sound_ = frame_;
        }

        playing_ = Playing();
        started_ = started_ || playing_;
        if (Looped() && on_loop_)
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

    // Whether the song's other tracks let its fade start (see xsflib's MeasureLength). Its loops count as track 0's.
    bool OthersLooped(int)
    {
        return OthersDone();
    }

protected:
    // Whether the song plays in the sound library.
    virtual bool Playing() = 0;

    // Whether the song has gone back to the start of its loop since the last frame.
    virtual bool Looped() = 0;

    // Whether the tracks that loop apart from the song's loops have each looped twice, or have stopped. A song whose
    // tracks all loop together has none.
    virtual bool OthersDone()
    {
        return true;
    }

    // The word at `address` in the game's memory, or 0 where no memory is.
    uint32_t Word(uint32_t address) const
    {
        const uint8_t* p = player_.System()->Memory().At(ToPhysical(address), 4);
        return p != nullptr ? xsflib::LoadBe32(p) : 0;
    }

private:
    // The output's running average takes a sample with this weight's inverse: about 50 ms.
    static constexpr double kLevelSamples = 1600.0;

    // The song rings for 0.5 s after its last sound, and a song that hasn't started in 5 s never will.
    static constexpr uint64_t kRingingFrames =
        kLengthRules.tail_ms * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;
    static constexpr uint64_t kStartFrames = 5000 * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;

    Player& player_;
    bool rests_at_zero_;
    double levels_[2] = {0.0, 0.0};
    std::function<void(int, uint32_t)> on_loop_;
    std::optional<uint64_t> last_sound_;
    uint64_t frame_ = 0;
    bool sounded_ = false;
    bool started_ = false;
    bool playing_ = false;
};

// A MusyX song (see LibrarySong). It plays while it's in one of MusyX's lists of songs. A jingle's thread waits half a
// second before it starts the jingle. Each section of the song counts its loops (see MusyXSections). The song has
// looped once more when each section that loops has.
class MusyXSong : public LibrarySong
{
public:
    MusyXSong(Player& player, const GameProfile& profile, const MusyXSections& sections)
        : LibrarySong(player), profile_(profile), sections_(sections)
    {
        last_ticks_.assign(profile.track_slot_count, 0);
    }

private:
    bool Playing() override
    {
        return Word(profile_.song_lists[0]) != 0 || Word(profile_.song_lists[1]) != 0;
    }

    bool Looped() override
    {
        CountSectionLoops();

        // The song's count is the least of the counts of its sections that loop.
        uint32_t least = UINT32_MAX;
        for (std::size_t s = 0; s < MusyXSections::kSections; s++)
        {
            if (sections_.looping >> s & 1)
            {
                least = std::min(least, section_loops_[s]);
            }
        }

        if (sections_.looping == 0 || least <= song_loops_)
        {
            return false;
        }

        song_loops_ = least;

        return true;
    }

    // Brings each section's count of its loops up to date. MusyX keeps the counts where the profile gives them.
    // Otherwise a section loops in a frame in which the slots of its tracks fall. A loop moves the section's tracks
    // back, all in one frame. A slot's new tick depends on the pass. A section whose tracks reach their loop entries in
    // the frame of the loop shows no fall.
    void CountSectionLoops()
    {
        if (profile_.section_loops != 0)
        {
            for (uint32_t s = 0; s < MusyXSections::kSections; s++)
            {
                section_loops_[s] = Word(profile_.section_loops + s * profile_.section_loop_size) >> 16;
            }

            return;
        }

        uint32_t fell = 0;
        for (uint32_t k = 0; k < profile_.track_slot_count && k < MusyXSections::kTracks; k++)
        {
            const uint32_t ticks = Word(profile_.track_slots + k * profile_.track_slot_size);
            if (ticks < last_ticks_[k] && sections_.of_track[k] < MusyXSections::kSections)
            {
                fell |= 1u << sections_.of_track[k];
            }
            last_ticks_[k] = ticks;
        }

        for (std::size_t s = 0; s < MusyXSections::kSections; s++)
        {
            section_loops_[s] += fell >> s & 1;
        }
    }

    const GameProfile& profile_;
    const MusyXSections sections_;
    std::vector<uint32_t> last_ticks_;
    std::array<uint32_t, MusyXSections::kSections> section_loops_ = {};
    uint32_t song_loops_ = 0;
};

// The entry of sound `id` in the catalogue in `library`, or null if the library has no catalogue or the catalogue no
// such sound.
const uint8_t* CatalogueEntry(const LoadedSet& library, uint32_t id)
{
    constexpr uint32_t kSounds = 8, kSoundsAt = 12, kSound = 20;
    const std::vector<uint8_t>* catalogue = library.File(kMusyXCatalogue);
    if (catalogue == nullptr || catalogue->size() < kSoundsAt + 4 ||
        id >= xsflib::LoadBe32(catalogue->data() + kSounds))
    {
        return nullptr;
    }

    const uint64_t entry = uint64_t{xsflib::LoadBe32(catalogue->data() + kSoundsAt)} + uint64_t{id} * kSound;

    return entry + kSound <= catalogue->size() ? catalogue->data() + entry : nullptr;
}

// The sections of the song that sound `id` plays, from the catalogue in `library` (see MusyXSections). A sound without
// a song, or a library without a catalogue, gets the defaults.
MusyXSections SoundSections(const LoadedSet& library, uint32_t id)
{
    constexpr uint32_t kSong = 8;
    const uint8_t* entry = CatalogueEntry(library, id);
    if (entry == nullptr)
    {
        return {};
    }

    const std::vector<uint8_t>* file = library.File(MusyXPath(xsflib::LoadBe16(entry + kSong), ".sng"));

    return file != nullptr ? SectionsOf(*file) : MusyXSections{};
}

// Whether sound `id` of the catalogue in `library` is an effect.
bool IsMusyXEffect(const LoadedSet& library, uint32_t id)
{
    const uint8_t* entry = CatalogueEntry(library, id);

    return entry != nullptr && entry[0] == static_cast<uint8_t>(MusyXCatalogue::Kind::kEffect);
}

// A MusyX effect (see LibrarySong and MusyXVoices). The effect plays while one of MusyX's voices is busy: while its
// synthesizer voice is in use, or while its DSP voice plays. A rip sends an effect no events, such as a key-off. A
// voice whose macro has ended, or waits for an event, therefore changes no more. The voice has settled. The sound
// changes when a voice starts or stops, moves to another step of its macro, or starts to wait. A voice's macro loops
// when a LOOP step without a count (a count of 0xFFFF) jumps back. The first voice that loops gives the effect's loops.
// Another voice that is still busy holds the fade back until it has looped twice too, or has settled.
//
// An effect whose voices have all settled holds. xsflib's MeasureLength loops it at the later of two points: 10 s after
// its sound last changed, and the second time since then that each playing DSP voice has passed the start of its
// sample's loop. An effect whose voices have all settled while no DSP voice plays has ended.
class MusyXEffect : public LibrarySong
{
public:
    MusyXEffect(Player& player, const GameProfile& profile)
        : LibrarySong(player),
          voices_(profile.musyx_voices),
          cpu_(player.System()->Cpu()),
          now_(voices_.count),
          loops_(voices_.count, 0)
    {
        cpu_.Watch({voices_.loop_handler}, [this](uint32_t) { StepLoops(); });
    }

    MusyXEffect(const MusyXEffect&) = delete;
    MusyXEffect& operator=(const MusyXEffect&) = delete;

    ~MusyXEffect() override
    {
        cpu_.Watch({}, nullptr);
    }

    // MeasureLength reads these in place of LibrarySong's.
    std::size_t VoiceCount() const
    {
        return voices_.count;
    }

    xsflib::rip::VoicePlace Voice(std::size_t i) const
    {
        const VoiceState& v = now_[i];

        return {.playing = v.state != 0, .loops = v.loop != 0, .position = v.position};
    }

    bool Settled() const
    {
        return settled_ && frame_ - changed_frame_ >= kHoldFrames;
    }

    bool Stuck() const
    {
        return settled_;
    }

    uint64_t ChangedAt() const
    {
        return changes_;
    }

    int ActiveChannels() const
    {
        return sampling_;
    }

private:
    // A voice's state in a frame.
    struct VoiceState
    {
        uint32_t id = kFree;
        uint32_t macro = 0;
        uint32_t step = 0;
        uint64_t wait = 0;
        uint8_t state = 0;
        int64_t position = 0;
        uint32_t loop = 0;

        bool Busy() const
        {
            return id != kFree || state != 0;
        }

        // Whether the voice's macro has ended or waits for an event.
        bool Settled() const
        {
            return id == kFree || macro == 0 || wait == kForever;
        }

        // Whether the voice's sound has changed since `last`. The position of its sample moves on.
        bool ChangedFrom(const VoiceState& last) const
        {
            return id != last.id || macro != last.macro || step != last.step || wait != last.wait ||
                   (state != 0) != (last.state != 0);
        }
    };

    static constexpr uint32_t kFree = 0xffffffff;
    static constexpr uint64_t kForever = UINT64_MAX;

    // A held effect loops 10 s after its sound last changed at the earliest.
    static constexpr uint64_t kHoldFrames = 10 * kLengthRules.sample_rate / kLengthRules.block_frames;

    // The byte at `address` in the game's memory, or 0 where no memory is.
    uint8_t Byte(uint32_t address) const
    {
        return static_cast<uint8_t>(Word(address & ~3u) >> (8 * (3 - (address & 3))));
    }

    // Reads the voices. LibrarySong calls this once a frame.
    bool Playing() override
    {
        frame_ = frames_++;
        const uint32_t synth = Word(voices_.synth);
        const uint32_t dsp = Word(voices_.dsp);
        if (synth == 0 || dsp == 0)
        {
            return false;
        }

        bool busy = false;
        bool changed = false;
        settled_ = true;
        sampling_ = 0;
        for (uint32_t i = 0; i < voices_.count; i++)
        {
            const uint32_t s = synth + i * voices_.synth_size;
            const uint32_t d = dsp + i * voices_.dsp_size;
            VoiceState v;
            v.id = Word(s + voices_.id_offset);
            v.macro = Word(s + voices_.macro_offset);
            v.step = Word(s + voices_.step_offset);
            v.wait = uint64_t{Word(s + voices_.wait_offset)} << 32 | Word(s + voices_.wait_offset + 4);
            v.state = Byte(d + voices_.state_offset);
            v.position = Word(d + voices_.position_offset);
            v.loop = Word(d + voices_.loop_offset);

            changed = changed || v.ChangedFrom(now_[i]);
            busy = busy || v.Busy();
            settled_ = settled_ && v.Settled();
            sampling_ += v.state != 0 ? 1 : 0;
            now_[i] = v;
        }

        if (changed)
        {
            ++changes_;
            changed_frame_ = frame_;
        }

        return busy;
    }

    // MusyX runs a LOOP step of the macro of the voice in r3. The step is in r4: two words. The step's count is the
    // high half of the second word. Bit 16 of the first word makes the count random.
    void StepLoops()
    {
        const uint32_t synth = Word(voices_.synth);
        const uint32_t voice = cpu_.state_.gpr[3];
        const uint32_t step = cpu_.state_.gpr[4];
        if (synth == 0 || voice < synth || voices_.synth_size == 0)
        {
            return;
        }

        const uint32_t i = (voice - synth) / voices_.synth_size;
        if (i >= voices_.count || Word(step + 4) >> 16 != 0xffff || (Word(step) >> 16 & 1) != 0)
        {
            return;
        }

        ++loops_[i];
        if (!first_)
        {
            first_ = i;
        }
        if (i == *first_)
        {
            looped_ = true;
        }
    }

    bool Looped() override
    {
        return std::exchange(looped_, false);
    }

    bool OthersDone() override
    {
        for (uint32_t i = 0; i < voices_.count; i++)
        {
            const VoiceState& v = now_[i];
            if (i != first_ && v.Busy() && !v.Settled() && loops_[i] < xsflib::rip::kLengthLoops)
            {
                return false;
            }
        }

        return true;
    }

    const MusyXVoices voices_;
    ppc::Cpu& cpu_;
    std::vector<VoiceState> now_;   // each voice's state in this frame
    std::vector<int> loops_;        // each voice's loops so far
    std::optional<uint32_t> first_; // the first voice that looped
    bool looped_ = false;           // whether that voice has looped since the last frame
    bool settled_ = false;          // whether every voice has settled
    int sampling_ = 0;              // the DSP voices that play
    uint64_t changes_ = 0;          // the changes of the sound so far
    uint64_t frames_ = 0;           // the frames so far
    uint64_t frame_ = 0;            // this frame
    uint64_t changed_frame_ = 0;    // the frame of the sound's last change
};

// A piece of a Red game (see LibrarySong). It plays while one of Red's music controls plays a piece. It loops in a
// frame in which the CPU reaches the profile's `red_loop`. There, Red takes the piece's tracks back to their state at
// the start of the loop.
class RedPiece : public LibrarySong
{
public:
    RedPiece(Player& player, const GameProfile& profile)
        : LibrarySong(player), profile_(profile), cpu_(player.System()->Cpu())
    {
        cpu_.Watch({profile.red_loop}, [this](uint32_t) { looped_ = true; });
    }

    RedPiece(const RedPiece&) = delete;
    RedPiece& operator=(const RedPiece&) = delete;

    ~RedPiece() override
    {
        cpu_.Watch({}, nullptr);
    }

private:
    bool Playing() override
    {
        const uint32_t controls = Word(profile_.red_controls);
        if (controls == 0)
        {
            return false;
        }

        for (uint32_t i = 0; i < profile_.red_music_controls; i++)
        {
            const uint32_t piece = Word(controls + i * profile_.red_control_size + profile_.red_music_offset);
            if (static_cast<int32_t>(piece) >= 0)
            {
                return true;
            }
        }

        return false;
    }

    bool Looped() override
    {
        return std::exchange(looped_, false);
    }

    const GameProfile& profile_;
    ppc::Cpu& cpu_;
    bool looped_ = false;
};

// An effect of a Red game (see LibrarySong). It plays while one of the tracks of Red's effect control is busy. Each
// track loops on its own. The profile's `red_track_loop` takes a track back to the start of a loop that never ends.
// The first track that loops gives the effect's loops. Another track that is still busy holds the fade back until it
// has looped twice too. A track that has stopped holds nothing back.
class RedEffect : public LibrarySong
{
public:
    RedEffect(Player& player, const GameProfile& profile)
        : LibrarySong(player), profile_(profile), cpu_(player.System()->Cpu()), loops_(profile.red_effect_tracks, 0)
    {
        cpu_.Watch({profile.red_track_loop}, [this](uint32_t) { TrackLooped(); });
    }

    RedEffect(const RedEffect&) = delete;
    RedEffect& operator=(const RedEffect&) = delete;

    ~RedEffect() override
    {
        cpu_.Watch({}, nullptr);
    }

private:
    // The effect control, or 0 before Red has set up its controls.
    uint32_t Control() const
    {
        const uint32_t controls = Word(profile_.red_controls);
        return controls != 0 ? controls + profile_.red_effect_control * profile_.red_control_size : 0;
    }

    // The effect control's first track, or 0.
    uint32_t Tracks() const
    {
        const uint32_t control = Control();
        return control != 0 ? Word(control) : 0;
    }

    // Whether track `i` is busy.
    bool Busy(uint32_t i) const
    {
        return Word(Tracks() + i * profile_.red_track_size) != 0;
    }

    bool Playing() override
    {
        if (Tracks() == 0)
        {
            return false;
        }

        for (uint32_t i = 0; i < profile_.red_effect_tracks; i++)
        {
            if (Busy(i))
            {
                return true;
            }
        }

        return false;
    }

    // A track has gone back to the start of its loop. r3 holds its control and r5 the track.
    void TrackLooped()
    {
        const uint32_t control = Control();
        const uint32_t tracks = Tracks();
        const uint32_t track = cpu_.state_.gpr[5];
        if (tracks == 0 || cpu_.state_.gpr[3] != control || track < tracks)
        {
            return;
        }

        const uint32_t i = (track - tracks) / profile_.red_track_size;
        if (i >= profile_.red_effect_tracks)
        {
            return;
        }

        ++loops_[i];
        if (!first_)
        {
            first_ = i;
        }
        if (i == *first_)
        {
            looped_ = true;
        }
    }

    bool Looped() override
    {
        return std::exchange(looped_, false);
    }

    bool OthersDone() override
    {
        for (uint32_t i = 0; i < profile_.red_effect_tracks; i++)
        {
            if (i != first_ && loops_[i] < xsflib::rip::kLengthLoops && Busy(i))
            {
                return false;
            }
        }

        return true;
    }

    const GameProfile& profile_;
    ppc::Cpu& cpu_;
    std::vector<int> loops_;        // each track's loops so far
    std::optional<uint32_t> first_; // the first track that looped
    bool looped_ = false;           // whether that track has looped since the last frame
};

// An effect of a HAL game (see LibrarySong). It plays while the driver's handle holds the effect's entry in the sound
// library's table. The driver clears the handle once the sound library has freed the entry. The sound library frees
// it once the effect's script has ended and its voices have stopped. The output rests at 0. The analysis follows no
// loops of the scripts. The Bonus Disc's effects have none.
class HalEffect : public LibrarySong
{
public:
    explicit HalEffect(Player& player) : LibrarySong(player, true)
    {
    }

private:
    bool Playing() override
    {
        return Word(DriverBlock::kAddress + kParamsHandleOffset) != 0;
    }

    bool Looped() override
    {
        return false;
    }
};

// A script of the Nintendo 64's sound library: a sequence player's, a channel's or a layer's. Their order is that of
// GameProfile::n64_command_reads and n64_flow_returns.
enum class N64Script : uint8_t
{
    kPlayer,
    kChannel,
    kLayer,
};

// Where Animal Crossing's build of the Nintendo 64's sound library keeps what the length analysis reads. A script's
// state holds its program counter at +0, the depth of its calls and loops at +0x18, and its value at +0x19. A sequence
// player holds its script's state at +0x78, its sequence's data at +0x18 and the ticks left to wait at +0x10. A channel
// holds its script's state at +0x64, its player at +0x50 and the ticks left to wait at +0x22. A layer holds its
// script's state at +0x64, its channel at +0x60 and the ticks left to wait at +0x1a.
constexpr uint32_t kN64Pc = 0x00;
constexpr uint32_t kN64Depth = 0x18;
constexpr uint32_t kN64Value = 0x19;
constexpr uint32_t kN64PlayerScript = 0x78;
constexpr uint32_t kN64PlayerData = 0x18;
constexpr uint32_t kN64PlayerWait = 0x10;
constexpr uint32_t kN64ChannelScript = 0x64;
constexpr uint32_t kN64ChannelPlayer = 0x50;
constexpr uint32_t kN64ChannelWait = 0x22;
constexpr uint32_t kN64LayerScript = 0x64;
constexpr uint32_t kN64LayerChannel = 0x60;
constexpr uint32_t kN64LayerWait = 0x1a;

// The commands of the Nintendo 64's sound library that end a script at depth 0, or return from a call. From 0xf2 to
// 0xfe, the commands jump, call, loop and wait in all three kinds of script.
constexpr uint8_t kN64End = 0xff;
constexpr uint8_t kN64FirstFlow = 0xf2;

// The codes that the analysis passes to ntdlib's LoopFinder in place of a command: a command that changes the sound,
// one that doesn't, and the end of a script.
constexpr uint8_t kN64Changes = 0x00;
constexpr uint8_t kN64Still = 0xfe;

// Whether a command of a script changes the sound: a note, a channel or layer started or stopped, or a parameter of the
// sound. Waits, jumps, calls, loops and the commands on ports and values don't. A layer's commands are notes up to
// 0xbf. 0xc0 waits.
bool N64ChangesSound(N64Script script, uint8_t command)
{
    if (command >= kN64FirstFlow)
    {
        return false;
    }

    switch (script)
    {
    case N64Script::kPlayer:
        // 0x00 to 0x3f test the channels, and 0x50 to 0x8f use the ports and loads. 0x40 to 0x4f stop channels, and
        // 0x90 to 0xaf start them. 0xb0 to 0xc1 load sequences, call the game and set nothing. 0xc2 jumps through a
        // table, 0xc5 counts, and 0xc7 to 0xcf work on the value. 0xc3 mutes channels, and 0xc4 and 0xc6 start and
        // stop sequences. From 0xd0, the commands set the player's parameters.
        if (command < 0x40 || (command >= 0x50 && command < 0x90) || (command >= 0xb0 && command < 0xc2))
        {
            return false;
        }

        return command != 0xc2 && command != 0xc5 && !(command >= 0xc7 && command <= 0xcf) && command != 0xef;
    case N64Script::kChannel:
        // 0x00 to 0x1f wait and load voices. 0x20 to 0x2f start channels. 0x30 to 0x77 use the ports and the value.
        // 0x78 to 0x7b start layers, and 0x80 to 0x83 test them. 0x88 to 0x9b start and stop layers. 0xa0 to 0xbe
        // work on the value and the channel's state, apart from the filters, the random variation and the sample's
        // start (0xa4, 0xb0, 0xb1, 0xb3, 0xb9 to 0xbb and 0xbd). From 0xc1, the commands set the channel's instrument
        // and parameters, apart from the jumps through tables (0xc2, 0xc5, 0xe4) and the work on the value (0xc7 to
        // 0xcf but 0xca and 0xcd).
        if (command < 0x20 || (command >= 0x30 && command < 0x78) || (command >= 0x80 && command < 0x88))
        {
            return false;
        }

        if (command < 0xa0)
        {
            return true;
        }

        if (command < 0xc1)
        {
            return command == 0xa4 || command == 0xb0 || command == 0xb1 || command == 0xb3 ||
                   (command >= 0xb9 && command <= 0xbb) || command == 0xbd;
        }

        return command != 0xc2 && command != 0xc5 && command != 0xe4 &&
               !(command >= 0xc7 && command <= 0xcf && command != 0xca && command != 0xcd);
    case N64Script::kLayer:
        return command != 0xc0;
    }

    return true;
}

// The length of a sound of the Nintendo 64's sound library, which the driver has started (see LengthAnalyzer::Analyze).
// For a piece of music, the driver's handle holds the piece's sequence player. The analysis follows the player's
// script, its channels' and their layers' through ntdlib's LoopFinder, as it follows a JAudio sound's tracks
// (ntdlib::jaudio::AnalyzeGameSound). A script is a track. The player's script is the root. For an effect, the handle
// holds the channel of the effects sequence that plays the effect. The channel's script is the root, and its layers
// follow. A track's position is an offset in the sequence's data. The sound ends when the driver clears the handle.
std::optional<Timing> AnalyzeN64Sound(Player& player, const GameProfile& profile, uint32_t handle_address, bool effect)
{
    using ntdlib::jaudio::FramesToMs;
    using ntdlib::jaudio::LoopFinder;
    using ntdlib::jaudio::TrackEvent;
    constexpr const ntdlib::jaudio::GameLengthRules& rules = kLengthRules;
    const auto ms = [](uint64_t frames)
    {
        return FramesToMs(frames, rules.sample_rate);
    };

    AnalysisMachine machine(player);
    ppc::Cpu& cpu = machine.Cpu();
    std::vector<TrackEvent> events;
    std::map<uint32_t, N64Script> scripts; // each track's kind, by its script's state
    uint32_t root = 0; // the sound's sequence player or channel, once the driver has started the sound
    uint32_t data = 0; // the sequence's data
    uint64_t frames = 0;

    // The script that a call of the library's functions came from, by its return address.
    const auto script_of = [](uint32_t lr, const std::array<uint32_t, 3>& returns) -> std::optional<N64Script>
    {
        const auto it = std::find(returns.begin(), returns.end(), lr);
        return it != returns.end() ? std::optional(static_cast<N64Script>(it - returns.begin())) : std::nullopt;
    };

    // The channel that a script's state belongs to, or 0 for a sequence player's.
    const auto channel_of = [&machine](N64Script script, uint32_t state) -> uint32_t
    {
        switch (script)
        {
        case N64Script::kPlayer:
            return 0;
        case N64Script::kChannel:
            return state - kN64ChannelScript;
        case N64Script::kLayer:
            return machine.Word(state - kN64LayerScript + kN64LayerChannel);
        }

        return 0;
    };

    // Whether a script belongs to the sound: to its sequence player, or to its channel.
    const auto belongs = [&](N64Script script, uint32_t state)
    {
        const uint32_t channel = channel_of(script, state);
        if (effect)
        {
            return channel != 0 && channel == root;
        }

        const uint32_t owner = channel ? machine.Word(channel + kN64ChannelPlayer) : state - kN64PlayerScript;
        return owner == root;
    };

    // Each command's start, from the reads of commands, and each jump that the flow function takes. A jump's position
    // is after its arguments. A relative jump counts from there.
    const auto on_watch = [&](uint32_t pc)
    {
        const auto& s = cpu.state_;
        if (!root)
        {
            return;
        }

        if (pc == profile.n64_read_byte)
        {
            const std::optional<N64Script> script = script_of(s.lr, profile.n64_command_reads);
            const uint32_t state = s.gpr[3];
            if (!script || !belongs(*script, state))
            {
                return;
            }

            const uint32_t position = machine.Word(state + kN64Pc);
            const uint8_t command = machine.Byte(position).value_or(0);
            const uint8_t depth = machine.Byte(state + kN64Depth).value_or(0);
            const uint8_t code = command == kN64End                  ? (depth == 0 ? kN64End : kN64Still)
                                 : N64ChangesSound(*script, command) ? kN64Changes
                                                                     : kN64Still;
            scripts.emplace(state, *script);
            events.push_back({TrackEvent::Kind::kCommand, state, position - data, 0, depth, code, frames});
            return;
        }

        const std::optional<N64Script> script = script_of(s.lr, profile.n64_flow_returns);
        const uint32_t state = s.gpr[4];
        if (!script || !belongs(*script, state))
        {
            return;
        }

        const uint32_t position = machine.Word(state + kN64Pc);
        const auto value = static_cast<int8_t>(machine.Byte(state + kN64Value).value_or(0));
        const uint32_t argument = s.gpr[6];
        const uint32_t absolute = data + (argument & 0xffff);
        const uint32_t relative = position + static_cast<uint32_t>(static_cast<int8_t>(argument & 0xff));
        std::optional<uint32_t> target;
        switch (s.gpr[5] & 0xff)
        {
        case 0xfb: // jump
            target = absolute;
            break;
        case 0xfa: // jump if the value is 0
            target = value == 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf9: // jump if it's negative
            target = value < 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf5: // jump if it isn't
            target = value >= 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf4: // relative jump
            target = relative;
            break;
        case 0xf3: // relative jump if the value is 0
            target = value == 0 ? std::optional(relative) : std::nullopt;
            break;
        case 0xf2: // relative jump if it's negative
            target = value < 0 ? std::optional(relative) : std::nullopt;
            break;
        default:
            break;
        }

        if (target)
        {
            const uint8_t depth = machine.Byte(state + kN64Depth).value_or(0);
            events.push_back({TrackEvent::Kind::kJump, state, position - data, *target - data, depth,
                              static_cast<uint8_t>(s.gpr[5]), frames});
        }
    };

    cpu.Watch({profile.n64_read_byte, profile.n64_flow}, on_watch);
    struct Unwatch
    {
        ppc::Cpu& cpu;
        ~Unwatch()
        {
            cpu.Watch({}, nullptr);
        }
    } unwatch{cpu};

    // What a track waits for: the ticks left to wait. No script of the library waits for its notes.
    const auto read_wait = [&](uint32_t track) -> ntdlib::jaudio::TrackWait
    {
        const auto it = scripts.find(track);
        if (it == scripts.end())
        {
            return {0, false};
        }

        switch (it->second)
        {
        case N64Script::kPlayer:
            return {static_cast<int32_t>(machine.Word(track - kN64PlayerScript + kN64PlayerWait) >> 16), false};
        case N64Script::kChannel:
            return {static_cast<int32_t>(machine.Word(track - kN64ChannelScript + kN64ChannelWait) >> 16), false};
        case N64Script::kLayer:
            return {static_cast<int16_t>(machine.Word(track - kN64LayerScript + kN64LayerWait) >> 16), false};
        }

        return {0, false};
    };

    // Play until the sound ends, loops twice, holds, falls silent for good or reaches the limit.
    std::vector<int16_t> block(rules.block_frames * 2);
    std::optional<LoopFinder> finder;
    std::optional<uint64_t> first_sound;
    uint64_t last_sound = 0;
    std::optional<uint64_t> released;
    const auto ended = [&]() -> Timing
    {
        if (!first_sound)
        {
            return {rules.tail_ms, 0, true};
        }

        return {ms(last_sound + 1) + rules.tail_ms, 0, false};
    };

    const auto changes = [](uint8_t code)
    {
        return code != kN64Still;
    };

    while (ms(frames) < rules.limit_ms)
    {
        events.clear();
        if (machine.Render(block.data(), rules.block_frames) != rules.block_frames)
        {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < rules.block_frames; i++)
        {
            if (xsflib::rip::Audible(std::span<const int16_t>(&block[2 * i], 2)))
            {
                last_sound = frames + i;
                first_sound = first_sound.value_or(last_sound);
            }
        }

        const uint32_t handle = machine.Word(handle_address);
        if (handle && !finder)
        {
            root = handle;
            const uint32_t track = root + (effect ? kN64ChannelScript : kN64PlayerScript);
            data = machine.Word((effect ? machine.Word(root + kN64ChannelPlayer) : root) + kN64PlayerData);
            scripts.emplace(track, effect ? N64Script::kChannel : N64Script::kPlayer);
            finder.emplace(track, rules.sample_rate, +changes);
        }

        // The tracks' loops. Loops before the first sound don't count.
        const ntdlib::jaudio::Sounded sounded = {first_sound.has_value(), last_sound};
        for (const TrackEvent& event : events)
        {
            const LoopFinder::Verdict verdict = finder ? finder->Take(event, sounded) : LoopFinder::Verdict::kNone;
            if (verdict == LoopFinder::Verdict::kLoops)
            {
                return Timing{ms(event.frame), rules.loop_fade_ms, false};
            }

            if (verdict == LoopFinder::Verdict::kEnded)
            {
                return ended();
            }
        }

        frames += rules.block_frames;
        if (finder)
        {
            finder->TakeWaits(read_wait);
        }

        // The driver clears the handle when the player stops. The notes can still sound after that: the output runs
        // behind the sequence, and a release fades on.
        if (!handle && finder && !released)
        {
            released = frames;
        }

        if (released && (ms(frames - std::max(*released, last_sound)) >= rules.tail_ms ||
                         ms(frames - *released) >= ntdlib::jaudio::kStillMs))
        {
            return ended();
        }

        // A sound that holds loops where it holds, if it still sounds.
        if (first_sound && finder && finder->Holds(frames))
        {
            if (ms(frames - last_sound) <= rules.holding_ms)
            {
                return Timing{ms(frames), rules.loop_fade_ms, false};
            }

            return ended();
        }

        if (first_sound && ms(frames - last_sound) >= rules.silence_ms)
        {
            return ended();
        }
    }

    if (!first_sound)
    {
        return ended();
    }

    return Timing{rules.limit_ms, rules.loop_fade_ms, false};
}

// The rules of a song's length: two loops and a 10 s fade, or its last sound and a 0.5 s tail.
xsflib::rip::LengthRules SongRules()
{
    xsflib::rip::LengthRules rules;
    rules.frame_ms = 1000.0 * kLengthRules.block_frames / kLengthRules.sample_rate;
    rules.max_frames = static_cast<uint64_t>(kLengthRules.limit_ms / rules.frame_ms);
    rules.silent_frames = static_cast<uint64_t>(kLengthRules.silence_ms / rules.frame_ms);
    rules.max_ms = static_cast<double>(kLengthRules.limit_ms);
    rules.fade_ms = static_cast<double>(kLengthRules.loop_fade_ms);
    rules.tail_ms = static_cast<double>(kLengthRules.tail_ms);

    return rules;
}

// The length of a song that `song` follows (see SongRules).
std::optional<Timing> MeasureSong(LibrarySong& song)
{
    return xsflib::rip::MeasureLength(song, SongRules());
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

    if (profile.library == SoundLibrary::kMusyX && IsMusyXEffect(impl_->library, id))
    {
        if (profile.musyx_voices.count == 0)
        {
            return std::nullopt;
        }

        MusyXEffect effect(player, profile);
        return xsflib::rip::MeasureLength(effect, SongRules());
    }

    if (profile.library == SoundLibrary::kMusyX)
    {
        MusyXSong song(player, profile, SoundSections(impl_->library, id));
        return MeasureSong(song);
    }

    if (profile.library == SoundLibrary::kRed && id >= kRedEffectId)
    {
        RedEffect effect(player, profile);
        return MeasureSong(effect);
    }

    if (profile.library == SoundLibrary::kRed)
    {
        RedPiece piece(player, profile);
        return MeasureSong(piece);
    }

    if (profile.library == SoundLibrary::kNintendo64)
    {
        return AnalyzeN64Sound(player, profile, DriverBlock::kAddress + kParamsHandleOffset,
                               id >> 24 == kN64EffectKind);
    }

    if (profile.library == SoundLibrary::kHal)
    {
        HalEffect effect(player);
        return MeasureSong(effect);
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
