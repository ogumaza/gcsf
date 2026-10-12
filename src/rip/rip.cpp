// SPDX-License-Identifier: MIT

#include "rip/rip.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
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
#include "rip/driver_mario_golf_blob.h"
#include "rip/driver_twilight_princess_blob.h"
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

const std::vector<GameProfile> kProfiles = {kFourSwordsUsa, kFourSwordsJpn, kTwilightPrincessUsa, kWindWakerUsa,
                                            kMarioGolfUsa};

// The profile's driver parameters, in the order of the driver's parameter block.
std::vector<uint32_t> DriverParameters(const GameProfile& p)
{
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

bool NamesWaves(const GameProfile& profile)
{
    return profile.driver == Driver::kWindWaker || profile.driver == Driver::kTwilightPrincess;
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
    const std::string sequence_archive = profile.sequence_archive;
    const std::string wave_folder = profile.wave_folder;
    game.read_file = [=](const std::string& path, std::vector<uint8_t>& data)
    {
        const bool sound_file = path == sound_archive || path == sequence_archive || path.starts_with(wave_folder);
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

std::vector<jaudio::SoundEntry> ListSounds(const GameProfile& profile, const std::vector<uint8_t>& sound_archive,
                                           const std::vector<uint8_t>& sequence_archive)
{
    if (profile.library != SoundLibrary::kJAudio1)
    {
        return jaudio::ReadSounds(sound_archive, kBaaOptions);
    }

    // A JAudio1 sound table names no sound. A sequence takes its file's name in the sequence archive, without the
    // extension. The archive may be compressed with Yaz0.
    std::vector<uint8_t> decoded;
    std::span<const uint8_t> archive = sequence_archive;
    if (jaudio::IsYaz0(sequence_archive))
    {
        decoded = jaudio::DecodeYaz0(sequence_archive);
        archive = decoded;
    }

    std::map<uint32_t, std::string> file_names;
    for (const jaudio::ArchiveFile& file : jaudio::ReadRarc(archive))
    {
        file_names.emplace(file.id, file.name.substr(0, file.name.rfind('.')));
    }

    std::vector<jaudio::SoundEntry> sounds = jaudio::ReadAafSounds(sound_archive);
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
            continue;
        }

        // An effect is named by its category and its index.
        char label[32];
        std::snprintf(label, sizeof(label), "se%u-%04u", static_cast<unsigned>(jaudio::AafCategory(id)),
                      static_cast<unsigned>(id & jaudio::kAafIndexMask));
        sound.name = label;
        sound.section_name = "SE";
        sound.group_name = std::to_string(jaudio::AafCategory(id));
    }

    return sounds;
}

std::vector<jaudio::WaveSystem> WaveSystems(const GameProfile& profile, const std::vector<uint8_t>& sound_archive)
{
    if (profile.library == SoundLibrary::kJAudio1)
    {
        return jaudio::ReadAafWaveSystems(sound_archive);
    }

    return jaudio::ReadWaveSystems(sound_archive, kBaaOptions);
}

bool IsMusic(const jaudio::SoundEntry& sound)
{
    return xsflib::AsciiLower(sound.section_name) == "bgm";
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

    return {entry.Id(), std::move(label), IsMusic(entry), jaudio::LayoutOf(entry.type).stream};
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

    std::vector<SongEntry> songs;
    const uint32_t tables[2][2] = {{profile.song_table, profile.song_count},
                                   {profile.jingle_table, profile.jingle_count}};
    for (uint32_t table = 0; table < 2; table++)
    {
        for (uint32_t number = 0; number < tables[table][1]; number++)
        {
            const std::span<const uint8_t> entry = at(tables[table][0] + number * 16);
            if (entry.size() < 16)
            {
                throw xsflib::FormatError("main.dol's song table ends early");
            }

            const std::span<const uint8_t> name = at(xsflib::LoadBe32(entry.data()));
            const auto end = std::find(name.begin(), name.end(), uint8_t{0});
            if (end == name.end())
            {
                throw xsflib::FormatError("main.dol's song name runs past its section");
            }

            songs.push_back({table, number, xsflib::LoadBe32(entry.data() + 4), std::string(name.begin(), end)});
        }
    }

    return songs;
}

} // namespace

std::vector<Sound> ListSongs(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        throw xsflib::FormatError("main.dol isn't a DOL");
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

    // A sequence archive compressed with Yaz0 is decoded first.
    std::vector<uint8_t> decoded;
    std::span<const uint8_t> archive = sequence_archive;
    if (jaudio::IsYaz0(sequence_archive))
    {
        decoded = jaudio::DecodeYaz0(sequence_archive);
        archive = decoded;
    }

    std::vector<UnplayedSequence> unplayed;
    for (const jaudio::ArchiveFile& file : jaudio::ReadRarc(archive))
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
        u.sound_archive = sound_archive;
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

    // The disc: its header, the start-up archive, the sequence archive and the wave archives. A JAudio1 library always
    // has the wave archives that the sound library loads at start-up.
    std::vector<uint8_t> sound_archive;
    if (!game.read_file || !game.read_file(profile.sound_archive, sound_archive))
    {
        return fail(std::string("can't read ") + profile.sound_archive);
    }

    std::vector<std::string> paths = {profile.sequence_archive};
    try
    {
        const bool jaudio1 = profile.library == SoundLibrary::kJAudio1;
        std::vector<std::string> names;
        if (wave_archives)
        {
            names = *wave_archives;
            for (const std::string& name :
                 jaudio1 ? jaudio::AafStayingWaveArchiveNames(sound_archive) : std::vector<std::string>{})
            {
                if (std::find(names.begin(), names.end(), name) == names.end())
                {
                    names.push_back(name);
                }
            }
        }
        else
        {
            names = jaudio1 ? jaudio::AafWaveArchiveNames(sound_archive)
                            : jaudio::WaveArchiveNames(sound_archive, kBaaOptions);
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
    watch.jump_track_register = p.jump_track_register;
    watch.jump_target_register = p.jump_target_register;
    watch.conditional_jump = p.conditional_jump;
    watch.conditional_track_register = p.conditional_track_register;
    watch.conditional_arguments_register = p.conditional_arguments_register;
    watch.jaudio1 = p.library == SoundLibrary::kJAudio1;
    watch.parameters_offset = p.parameters_offset;
    watch.parameters_track_offset = p.parameters_track_offset;
    watch.effects_sequence = p.effects_sequence;
    watch.effect_slot_offset = p.effect_slot_offset;
    watch.parent_offset = p.parent_offset;
    watch.children_offset = p.children_offset;

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

    AnalysisMachine machine(player);
    return ntdlib::jaudio::AnalyzeGameSound(machine, DriverBlock::kAddress + kParamsHandleOffset, WatchOf(profile),
                                            kLengthRules, found, lookups_sound, id);
}

} // namespace gcsf::rip
