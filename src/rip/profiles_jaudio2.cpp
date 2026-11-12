// SPDX-License-Identifier: MIT

// The driver profiles of the games with JAudio2 (see GameProfile in rip/rip.h). The comment of each profile says
// what the game's code does for sound.

#include <cstdint>
#include <vector>

#include "ntdlib/flipper/z2_params.h"
#include "ntdlib/jaudio/jungle_beat_states.h"

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{
namespace
{

// The parameters of Four Swords Adventures' driver (driver/four_swords.c's Params from frame_retraces on): the sizes
// that the game's start-up gives, the addresses of the game's functions and variables, and the piece whose tracks the
// game's music start mutes. That start mutes some children of the piece's track 0. `muted_sound` names the piece, or 0
// for none. Bit n of `muted_children` mutes child n. `track_mute` is JASTrack::mute. `second_sound` is the ID that
// plays the piece with its second set of tracks, or 0 for none. `second_children` gives that set's muted children.
struct FourSwordsParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

    uint32_t frame_retraces = 0;
    uint32_t max_heaps = 0, system_heap_size = 0, aram_audio_size = 0, aram_graph_size = 0, aram_priorities = 0,
             audio_heap_size = 0, disc_heap_size = 0, audio_object_size = 0;
    uint32_t vi_init = 0, vi_wait_for_retrace = 0, get_current_thread = 0, get_thread_priority = 0;
    uint32_t create_root_heap = 0, create_exp_heap = 0, create_solid_heap = 0, create_aram = 0, alloc = 0,
             alloc_from = 0, thread_ctor = 0, become_current_heap = 0, system_heap = 0;
    uint32_t game_globals = 0, audio_object_ctor = 0, create_disc_thread = 0, audio_setup = 0, audio_frame = 0;
    uint32_t sound_manager_offset = 0, section_offset = 0, load_wave_bank = 0, section_data_offset = 0,
             wave_banks_offset = 0, wave_arc_state_offset = 0;
    uint32_t track_mute = 0, muted_sound = 0, muted_children = 0, second_sound = 0, second_children = 0;
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
            muted_children,
            second_sound,
            second_children};
}

// The ID that plays Four Swords Adventures' JA_BGM_SELECT (0x0100001a) with its second set of tracks. The game's IDs
// leave bit 31 clear. On the file select and the choice of mode, the piece plays only children 2 and 7 of its track 0.
// On the screen where the players join and on the map where they pick a stage, it plays every child but 2.
constexpr uint32_t kSelectSecondSet = 0x8100001a;

} // namespace

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
            // The function at 0x80458408 switches the piece between that set and one that mutes only child 2.
            .track_mute = 0x80037f1c,
            .muted_sound = 0x0100001a,
            .muted_children = 0xffff & ~(1u << 2 | 1u << 7),
            .second_sound = kSelectSecondSet,
            .second_children = 1u << 2,
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
    .second_set_piece = 0x0100001a,
    .second_set_id = kSelectSecondSet,
    .second_set_name = "map",
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
            // The function at 0x80079998 switches the piece between that set and one that mutes only child 2.
            .track_mute = 0x80037ff4,
            .muted_sound = 0x0100001a,
            .muted_children = 0xffff & ~(1u << 2 | 1u << 7),
            .second_sound = kSelectSecondSet,
            .second_children = 1u << 2,
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
    .second_set_piece = 0x0100001a,
    .second_set_id = kSelectSecondSet,
    .second_set_name = "map",
};

// Four Swords Adventures' European release. The sound library and the functions that the driver calls are the American
// code at other addresses. Beyond the fields that the linker fills in, two functions that a rip reaches differ. One is
// the audio set-up (see below). The other is the disc library's stateReady, which leaves out a store of -1 that the
// American build makes. The game's main makes one more heap than the American main: 0x142800 bytes from the game heap,
// for the languages' files. It keeps that heap at +0xC of the game's variables, and the audio object at +0x10. The
// driver keeps the American places, with the audio object at +0xC. Of the code that the driver reaches, only the audio
// set-up reads the object from there (0x8043A47C). The rip patches that load to read +0xC, as the American set-up does.
// The sound files are the American ones byte for byte.
const GameProfile kFourSwordsEur = {
    .name = "The Legend of Zelda: Four Swords Adventures",
    .game_id = "G4SP01",
    .dol_sha1 = "66dbc77f258a64d8bc8d7b0041c619ed70d0c00b",
    .main = 0x80138bb8,
    .sound_archive = "GC4Sword_pal/AudioRes/JaiInit.baa",
    .sequence_archive = "GC4Sword_pal/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "GC4Sword_pal/AudioRes/Waves/",
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
            .vi_init = 0x8004c91c,
            .vi_wait_for_retrace = 0x8004cdcc,
            .get_current_thread = 0x80045ea4,
            .get_thread_priority = 0x80046fbc,
            .create_root_heap = 0x80081fe0,
            .create_exp_heap = 0x80082060,
            .create_solid_heap = 0x80085738,
            .create_aram = 0x80079c34,
            .alloc = 0x80084efc,
            .alloc_from = 0x80084f98,
            .thread_ctor = 0x80086158,
            .become_current_heap = 0x800843b8,
            .system_heap = 0x80557bf8,
            .game_globals = 0x8054540c,
            .audio_object_ctor = 0x8047484c,
            .create_disc_thread = 0x803f9348,
            .audio_setup = 0x8043a3b4,
            .audio_frame = 0x80473e40,
            .sound_manager_offset = 0x30,
            .section_offset = 0x44,
            .load_wave_bank = 0x8003e0e4,
            .section_data_offset = 0x30,
            .wave_banks_offset = 0xec,
            .wave_arc_state_offset = 0x4c,
            // The game's music start (0x80473e40, 0x804737bc) mutes every child of JA_BGM_SELECT's track 0 but 2 and 7.
            // The function at 0x80472bd4 switches the piece between that set and one that mutes only child 2. Its four
            // callers pass the American flags.
            .track_mute = 0x8003807c,
            .muted_sound = 0x0100001a,
            .muted_children = 0xffff & ~(1u << 2 | 1u << 7),
            .second_sound = kSelectSecondSet,
            .second_children = 1u << 2,
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x800352e4, // JASSeqParser::parse
            .jump_handler = 0x80034028,  // the sequence parser's command 0xC7
            .sequence_vtable = 0x804ad9bc,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x804ad928,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
        },
    .second_set_piece = 0x0100001a,
    .second_set_id = kSelectSecondSet,
    .second_set_name = "map",
    .patches = {{0x8043a47c, 0x8064000c}}, // the audio set-up: lwz r3, 0xc(r4), the audio object
};

// Twilight Princess (USA). Its sound layer, Z2, sits on JAudio2. The game loads its wave groups as its scenes require
// them. The driver (driver/twilight_princess.c) runs ntdlib's start-up of Z2. Its parameters are ntdlib's z2::Profile.
// An effect's handle points at the JAISound inside its JAISe, 0x10 bytes in: its vtable is the JAISe's + 8, and its
// root track is 0xb8 bytes further. As the game starts five pieces, Z2SeqMgr::bgmStart (0x802af010) mutes some
// children of their track 0. The driver mutes the same children through the music manager (ntdlib's
// kTwilightPrincessStarts). The scenes' starts, changeBgmStatus (0x802aff8c), changeSubBgmStatus (0x802b1df4) and the
// battle music's functions give 17 pieces more states (ntdlib's kTwilightPrincessStates). The driver sets their volumes
// the same way and writes their status through Z2SoundStarter::setPortData.
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
            .music_manager = 0x80450b84,        // JASGlobalInstance<Z2SeqMgr>::sInstance
            .set_child_volume = 0x802b3fec,     // Z2SeqMgr::setChildTrackVolume
            .set_port_data = 0x802aaedc,        // Z2SoundStarter::setPortData
            .get_port_data = 0x802aaf74,        // Z2SoundStarter::getPortData
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
    .muted_starts = ntdlib::jaudio::kTwilightPrincessStarts,
    .piece_states = ntdlib::jaudio::kTwilightPrincessStates,
};

// Twilight Princess's European release. The sound library, Z2 and the functions that the driver calls are the American
// code at other addresses. Only the fields that the linker fills in differ. The structure offsets don't change. The
// machine set-up (mDoMch_Create) differs only in its choice of video mode. It gives the American heaps. The sound files
// are the American ones byte for byte.
const GameProfile kTwilightPrincessEur = {
    .name = "The Legend of Zelda: Twilight Princess",
    .game_id = "GZ2P01",
    .dol_sha1 = "8d06f67da2185bf10d1978125c46fd36de723219",
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
            .vi_init = 0x8034d380,
            .vi_wait_for_retrace = 0x8034d830,
            .get_current_thread = 0x80341af0,
            .get_thread_priority = 0x80342cc8,
            .create_root_heap = 0x802cfbb4,     // JKRExpHeap::createRoot
            .create_exp_heap = 0x802cfc2c,      // JKRExpHeap::create
            .create_solid_heap = 0x802d1824,    // JKRSolidHeap::create
            .create_aram = 0x802d2da4,          // JKRAram::create
            .set_aram_buffer = 0x802d4e34,      // JKRAramStream::setTransBuffer
            .create_thread_switch = 0x802d2814, // JKRThreadSwitch::createManager
            .create_command_heap = 0x8000ed3c,  // mDoExt_createCommandHeap
            .create_archive_heap = 0x8000ed6c,  // mDoExt_createArchiveHeap
            .archive_heap_parent = 0,           // the root heap
            .create_assert_heap = 0x8000ec74,   // mDoExt_createAssertHeap
            .create_zelda_heap = 0x8000ed0c,    // mDoExt_createZeldaHeap
            .get_free_size = 0x802cf52c,        // JKRHeap::getFreeSize
            .become_current_heap = 0x802cf238,  // JKRHeap::becomeCurrentHeap
            .system_heap = 0x80453338,          // JKRHeap::sSystemHeap
            .alloc = 0x802cfa4c,                // operator new
            .thread_ctor = 0x802d24b8,          // JKRThread's constructor for an OSThread
            .create_disc_thread = 0x8001580c,   // mDoDvdThd::create
            .audio_heap = 0x80452b7c,           // g_mDoAud_audioHeap
            .audio_setup = 0x80006fc0,          // mDoAud_Create
            .init_flag = 0x80452b78,            // mDoAud_zelAudio_c::mInitFlag
            .audio_frame = 0x802ce704,          // Z2AudioMgr::gframeProcess
            .load_se_wave = 0x802bb504,         // Z2SceneMgr::loadSeWave
            .load_bgm_wave = 0x802bb570,        // Z2SceneMgr::loadBgmWave
            .wave_status = 0x802bb498,          // Z2SceneMgr::getWaveLoadStatus
            .audio_manager = 0x803ddeec,        // the audio manager, g_mDoAud_zelAudio
            .start_sound = 0x802ce91c,          // Z2AudioMgr::startSound
            .music_manager = 0x80452b44,        // JASGlobalInstance<Z2SeqMgr>::sInstance
            .set_child_volume = 0x802b4dec,     // Z2SeqMgr::setChildTrackVolume
            .set_port_data = 0x802abcdc,        // Z2SoundStarter::setPortData
            .get_port_data = 0x802abd74,        // Z2SoundStarter::getPortData
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x802966d4,    // JASSeqParser::parse
            .jump_handler = 0x80295128,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8029516c, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x803cb894,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x803cb800,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
            // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
            .wave_handle_functions = {0x802996dc, 0x80299c60},
            .wave_bank_table = 0x80452c80,  // the section heap's instance
            .wave_bank_table_offset = 0xfc, // its table of wave banks
            .wave_bank_count_offset = 0xf8, // the table's count
        },
    .muted_starts = ntdlib::jaudio::kTwilightPrincessStarts,
    .piece_states = ntdlib::jaudio::kTwilightPrincessStates,
};

// Twilight Princess's Japanese release, Zelda no Densetsu: Twilight Princess. The sound library and the functions that
// the driver calls are the American code at other addresses. Only the fields that the linker fills in differ. The
// structure offsets don't change. Of Z2, only Z2SoundObjMgr::searchEnemy differs. Its loop over the enemies near Link
// never runs in a rip. The machine set-up (mDoMch_Create) gives an archive heap of 0x8E6000 bytes (American 0x8DF400).
// The sound files are the American ones byte for byte.
const GameProfile kTwilightPrincessJpn = {
    .name = "Zelda no Densetsu: Twilight Princess",
    .game_id = "GZ2J01",
    .dol_sha1 = "847261078fc157620aed5f2d8ee84d3aa9193021",
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
            .archive_heap_size = 0x8e6000,
            .zelda_heap_margin = 0x10000,
            .audio_heap_size = 0x14d800,
            .vi_init = 0x8034e9b0,
            .vi_wait_for_retrace = 0x8034ee60,
            .get_current_thread = 0x80343120,
            .get_thread_priority = 0x803442f8,
            .create_root_heap = 0x802d1250,     // JKRExpHeap::createRoot
            .create_exp_heap = 0x802d12c8,      // JKRExpHeap::create
            .create_solid_heap = 0x802d2ec0,    // JKRSolidHeap::create
            .create_aram = 0x802d4440,          // JKRAram::create
            .set_aram_buffer = 0x802d64d0,      // JKRAramStream::setTransBuffer
            .create_thread_switch = 0x802d3eb0, // JKRThreadSwitch::createManager
            .create_command_heap = 0x8000ed88,  // mDoExt_createCommandHeap
            .create_archive_heap = 0x8000edb8,  // mDoExt_createArchiveHeap
            .archive_heap_parent = 0,           // the root heap
            .create_assert_heap = 0x8000ecc0,   // mDoExt_createAssertHeap
            .create_zelda_heap = 0x8000ed58,    // mDoExt_createZeldaHeap
            .get_free_size = 0x802d0bc8,        // JKRHeap::getFreeSize
            .become_current_heap = 0x802d08d4,  // JKRHeap::becomeCurrentHeap
            .system_heap = 0x8044b4b0,          // JKRHeap::sSystemHeap
            .alloc = 0x802d10e8,                // operator new
            .thread_ctor = 0x802d3b54,          // JKRThread's constructor for an OSThread
            .create_disc_thread = 0x80015858,   // mDoDvdThd::create
            .audio_heap = 0x8044acfc,           // g_mDoAud_audioHeap
            .audio_setup = 0x80006fc0,          // mDoAud_Create
            .init_flag = 0x8044acf8,            // mDoAud_zelAudio_c::mInitFlag
            .audio_frame = 0x802cfda0,          // Z2AudioMgr::gframeProcess
            .load_se_wave = 0x802bcb44,         // Z2SceneMgr::loadSeWave
            .load_bgm_wave = 0x802bcbb0,        // Z2SceneMgr::loadBgmWave
            .wave_status = 0x802bcad8,          // Z2SceneMgr::getWaveLoadStatus
            .audio_manager = 0x803d608c,        // the audio manager, g_mDoAud_zelAudio
            .start_sound = 0x802cffb8,          // Z2AudioMgr::startSound
            .music_manager = 0x8044acc4,        // JASGlobalInstance<Z2SeqMgr>::sInstance
            .set_child_volume = 0x802b642c,     // Z2SeqMgr::setChildTrackVolume
            .set_port_data = 0x802ad31c,        // Z2SoundStarter::setPortData
            .get_port_data = 0x802ad3b4,        // Z2SoundStarter::getPortData
        }
            .Words(),
    .watch =
        {
            .parse_command = 0x80297d04,    // JASSeqParser::parse
            .jump_handler = 0x80296758,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8029679c, // its command 0xC8, where it takes the jump
            .sequence_vtable = 0x803c3a34,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x803c39a0,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
            // JASBasicWaveBank's and JASSimpleWaveBank's getWaveHandle
            .wave_handle_functions = {0x8029ad0c, 0x8029b290},
            .wave_bank_table = 0x8044ae00,  // the section heap's instance
            .wave_bank_table_offset = 0xfc, // its table of wave banks
            .wave_bank_count_offset = 0xf8, // the table's count
        },
    .muted_starts = ntdlib::jaudio::kTwilightPrincessStarts,
    .piece_states = ntdlib::jaudio::kTwilightPrincessStates,
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
    .other_files = {"AudioRes/Info/JaiChord.arc", "AudioRes/Info/ChordSeDataBase.csd"},
    .other_files_first = true,
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
            0xd08,                // chord_offset: the audio manager's chord effects
            164,                  // chord_records: the database's count says 218
            0x8024c4c8,           // start_chord_effect: startChordSe
            0xd0c,                // stage_offset: the audio manager's stage object
            0x8025049c,           // start_stage_music
            0x80253684,           // set_track_states: the BGM player's function for a code for each track
            0x810,                // rhythm_offset: the audio manager's rhythm
            0x80248784,           // rhythm_frame: the rhythm's frame process
            0x50042,              // swapped_effect: two stages play other effects with its records
            0x50063,              // swaps: such an effect
            0x2c,                 //   and its stage
            0x50062,              // the other such effect
            0x35,                 //   and its stage
            0,                    // music: written by the driver
            0x70,                 // combo_effect: JA_SE_EAT_BIND_COMBO
            0x8023f98c,           // start_with_count: the audio manager's start of a sound with a count
            0x802405ac,           // write_port: writes a port of a sound that plays
            // counts and ports: effects whose sequences add a value at port 0xb to a key, each with the value that
            // the game's code gives it first. The game starts the first two with a count, and start_with_count writes
            // the count there. A game object writes the third's value after the start (0x8024658c, from 0x80244aec).
            // JA_SE_PAUSE_MENU_CLAP_PARAM: the pause menu's setting of the microphone + 1, after a step of the setting
            // (0x801c89e4). The setting starts from the microphone's threshold (0x801c87f4): 6 from a new save's 75
            // (0x8021ddf4). The menu checks a step down first (0x801c8de4), to 5.
            0x8e,
            6,
            // JA_SE_BEAT_POINT_ADD: the points added + 60 (0x802400c8), while they stay below a limit (0x80240090).
            // Both start at 0 (0x80240064). No code sets the limit. The game therefore never reaches the start.
            0x37,
            60,
            // JA_SE_OBJ_SOUND_BLOCK_DISAPPEAR: the entry of the table at 0x803f09a0 (8, 4, 1, 0) for the block's step
            // (0x80244aec). The step starts at 0 (0x802438cc) and goes on by 1 after each block (0x80243998). It goes
            // back to 0 after 90 frames without one (0x80243940).
            0x4000f,
            8,
            // The states that the game sets for its music during play (ntdlib's kJungleBeatStates).
            0x802540d8, // combo_state: sets the state of the first table of the BGM player for a combo count
            0x80251018, // combo_threshold: gives the count in the row of the stage for a layer of a combo
            30,         // combo_stage: MorningField, whose combos reach their layers at counts of 10, 45 and 80
            0x80253da8, // flags_state: sets the state of the first table of the BGM player for the flags of the player
            0x164,      // flags_offset
        },
    .watch =
        {
            .parse_command = 0x8000bb9c,    // JASSeqParser::parse
            .jump_handler = 0x8000a628,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8000a66c, // its command 0xC8, where it takes the jump
            .idle_in_calls = true,          // an effect's root track waits in a call until its child tracks end
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
    .background_sound = 52,       // the driver's word `music`
    .track_parent_offset = 0x12c, // JASTrack::connectChild (0x80007cec) stores the parent there
    .layer_states = jaudio::kJungleBeatStates,
};

// Mario Kart: Double Dash!! (USA). Its sound library is the earliest known build of JAudio2, under the game's audio
// object. The start-up archive's sound table has the first layouts. The archive holds the music's sequences as blocks.
// The game has no sequence archive. Its three wave banks hold 6.5 MiB of waves. The game keeps them loaded. Its voices
// come from three more start-up archives in the first one's "baac" block. The game loads one of them at a time
// (0x801148fc): the title's and the character select's (0), the races' (1) or the awards' (2). The driver loads the
// one whose bank a voice plays. The parameters follow driver/double_dash.c's Params from frame_retraces.
const GameProfile kDoubleDashUsa = {
    .name = "Mario Kart: Double Dash!!",
    .game_id = "GM4E01",
    .dol_sha1 = "e389e50357d4a5076ab3d38672204658f411123d",
    .main = 0x801cc6a8,
    .driver = Driver::kDoubleDash,
    .sound_archive = "AudioRes/GCKart.baa",
    .wave_folder = "AudioRes/Waves/",
    .first_layouts = true,
    .baac_archives = true,
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
            0x801148fc,           // load_voices: the audio object's load of an archive of the baac block
        },
    .watch =
        {
            .parse_command = 0x8009d090,    // JASSeqParser::parse
            .jump_handler = 0x8009bd84,     // the sequence parser's command 0xC7
            .conditional_jump = 0x8009be80, // its command 0xC8, where it takes the jump
            // The game and the driver start a level sound again each frame. 16 of the level sounds end each pass. The
            // next start runs the finished track again from its first command.
            .restarts_as_passes = true,
            .sequence_vtable = 0x80341754,
            .sequence_track_offset = 0xa8,
            .effect_vtable = 0x803416c0,
            .effect_track_offset = 0xb8,
            .wait_offset = 0x40,
            .note_wait_offset = 0x51,
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

} // namespace gcsf::rip
