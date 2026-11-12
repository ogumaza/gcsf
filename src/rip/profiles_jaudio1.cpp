// SPDX-License-Identifier: MIT

// The driver profiles of the games with JAudio1 and of Pikmin, whose library Jac came before JAudio1 (see GameProfile
// in rip/rip.h). The comment of each profile says what the game's code does for sound.

#include <cstdint>

#include "ntdlib/jaudio/jac_effects.h"
#include "ntdlib/jaudio/wind_waker_ports.h"

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{
namespace
{

// The states in which The Wind Waker's sound layer plays a piece of music another way. The addresses are in the USA
// release's main.dol. JAIZelBasic::bgmStart (0x802a34a4) mutes some of the root track's 16 children as it starts 13
// pieces, with bgmMute (0x802abb18). The mask comes from the table m_bgm_mute_state (0x8039b630): the piece's row and
// the column of a state. The IDs from 0x80000105 to 0x80000152 start a piece in another column. A state that plays what
// another plays is left out. 0x80000125 also sets the tempo to 1.3143 times its speed. JA_BGM_SEA and JA_BGM_SEA_STORM
// play every track with the boat's sail up, and leave some out while the sail is down and the boat is still.
// subBgmStart (0x802a47b8) starts JA_BGM_SEA_ENEMY with some tracks muted unless Link is on board, and the minibosses'
// pieces JA_BGM_MBOSS and JA_BGM_MBOSS_S with some tracks muted. bgmNowBattle (0x802a4eb8) starts JA_BGM_BATTLE_NORM in
// the same way. It brings in a layer of the battle music, or of a miniboss's, while an enemy is within 30 units of Link
// (mbossBgmNearByProcess, 0x802a591c). The sound layer brings in another layer while Link's sword is out
// (bgmSetSwordUsing, 0x802a579c, read by bgmNowBattle and mbossBgmMuteProcess at 0x802a5818).
constexpr MusicState kWindWakerMusicStates[] = {
    {0x80000004, 0x80000004 | kEnemyNear, "near"}, // JA_BGM_BATTLE_NORM
    {0x80000004, 0x80000004 | kSwordOut, "sword"},
    {0x80000004, 0x80000004 | kSwordOut | kEnemyNear, "near sword"},
    {0x80000005, 0x80000105, "80000105"},          // JA_BGM_KINDAN_BOSS
    {0x80000014, 0x80000110, "80000110"},          // JA_BGM_DK_BATTLE
    {0x80000015, 0x80000115, "80000115"},          // JA_BGM_MJ_TOWER_BATTLE
    {0x80000019, 0x80000019 | kEnemyNear, "near"}, // JA_BGM_MBOSS
    {0x80000019, 0x80000019 | kSwordOut, "sword"},
    {0x80000019, 0x80000019 | kSwordOut | kEnemyNear, "near sword"},
    {0x8000001a, 0x8000001a | kEnemyNear, "near"}, // JA_BGM_MBOSS_S
    {0x8000001a, 0x8000001a | kSwordOut, "sword"},
    {0x8000001a, 0x8000001a | kSwordOut | kEnemyNear, "near sword"},
    {0x8000001c, 0x8000001c | kOnBoard, "on board"}, // JA_BGM_SEA_ENEMY
    {0x80000023, 0x80000120, "80000120"},            // JA_BGM_BST_BATTLE
    {0x80000023, 0x80000121, "80000121"},
    {0x80000023, 0x80000122, "80000122"},
    {0x80000023, 0x80000123, "80000123"}, // column 3 of the row before
    {0x80000028, 0x80000125, "80000125"}, // JA_BGM_KAMI_TOWER
    {0x80000028, 0x80000126, "80000126"},
    {0x8000002e, 0x8000002e | kSailing, "sailing"}, // JA_BGM_SEA
    {0x8000003c, 0x8000003c | kSailing, "sailing"}, // JA_BGM_SEA_STORM
    {0x80000042, 0x80000130, "80000130"},           // JA_BGM_D_GANON_1
    {0x80000042, 0x80000131, "80000131"},
    {0x80000042, 0x80000132, "80000132"},
    {0x80000042, 0x80000133, "80000133"}, // column 3 of the row after
    {0x80000048, 0x80000140, "80000140"}, // JA_BGM_BIG_POW
    {0x80000049, 0x80000150, "80000150"}, // JA_BGM_PAST_BIG_POW
    {0x8000004a, 0x80000151, "80000151"}, // JA_BGM_PAST_BKM
};

// A room's sound information in Luigi's Mansion (USA): the room's sound_echo_parameter, sound_room_size << 6 and
// sound_room_code << 8. They are fields of the game's table of rooms (roominfo). 0x8002d90c reads the table, and the
// game packs the fields so at 0x8018d48c.
constexpr uint32_t RoomSound(uint32_t echo, uint32_t size, uint32_t code)
{
    return echo | size << 6 | code << 8;
}

// The states in which Luigi's Mansion (USA) plays LuigiSings. The game's start of the piece (0x8018c274) and its frame
// (0x8018bd84) pick the tracks. Children 4 to 7 of the root track hold the tune. One of them plays at a time. Child 4
// plays while Luigi has more than 70 of his 100 points of health, child 5 while he has more than 40, and child 6 below
// that. Child 7 plays alone while the room's lights are on, unless the room's sound_room_size is 2. The other children
// accompany the tune by the room (0x80189488): children 0 to 3 and 11 in a room whose sound_room_code is 6 or 7 and in
// room 35 while it's dark, child 9 in a room whose sound_room_size is 2, and children 8 and 10 in the other rooms.
// Rooms 19, 32 and 65 get no accompaniment. The frame also sets the piece's tempo: 0.6 + 0.4 x the health / 100. With
// the lights on, it scales that by the time between Luigi's steps, to 0.9 while he stands still. A state's rip takes
// the most health of its band and the first room of the room table that gives its tracks. A poison mushroom
// (EnNegativeItem) shrinks Luigi for a time. It puts Luigi in state 56 (0x800d0e4c). That state starts effect 0x847
// and calls 0x80189468 with 1 (0x800b13e0 to 0x800b1424). The call sets the float at 0x804d783c to 2.0. The frame gives
// children 4 to 6 the float through 0x801991b4, and the tune plays an octave higher. Child 7 doesn't take it. Once the
// state's timer runs out, the game calls 0x80189468 with 0 (0x800b4848 to 0x800b48a0). The shrunk states take 100 HP in
// each place but the lit room.
constexpr SingingState kLuigiSingsStates[] = {
    {"hallway 100 HP", 100, false, 6, RoomSound(10, 0, 7)},
    {"hallway 70 HP", 70, false, 6, RoomSound(10, 0, 7)},
    {"hallway 40 HP", 40, false, 6, RoomSound(10, 0, 7)},
    {"room 100 HP", 100, false, 0, RoomSound(10, 1, 0)},
    {"room 70 HP", 70, false, 0, RoomSound(10, 1, 0)},
    {"room 40 HP", 40, false, 0, RoomSound(10, 1, 0)},
    {"outdoors 100 HP", 100, false, 11, RoomSound(0, 2, 1)},
    {"outdoors 70 HP", 70, false, 11, RoomSound(0, 2, 1)},
    {"outdoors 40 HP", 40, false, 11, RoomSound(0, 2, 1)},
    {"unaccompanied 100 HP", 100, false, 19, RoomSound(10, 0, 6)},
    {"unaccompanied 70 HP", 70, false, 19, RoomSound(10, 0, 6)},
    {"unaccompanied 40 HP", 40, false, 19, RoomSound(10, 0, 6)},
    {"lit room", 100, true, 0, RoomSound(10, 1, 0)},
    {"hallway 100 HP shrunk", 100, false, 6, RoomSound(10, 0, 7), true},
    {"room 100 HP shrunk", 100, false, 0, RoomSound(10, 1, 0), true},
    {"outdoors 100 HP shrunk", 100, false, 11, RoomSound(0, 2, 1), true},
    {"unaccompanied 100 HP shrunk", 100, false, 19, RoomSound(10, 0, 6), true},
};

} // namespace

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
            0x80298b74,           // set_port_data
            0x8027e250,           // get_random: JASystem::Player::getRandomS32
            0x802a34a4,           // bgm_start: JAIZelBasic::bgmStart
            0x802a47b8,           // sub_bgm_start: JAIZelBasic::subBgmStart
            0x802a4eb8,           // battle_bgm_start: JAIZelBasic::bgmNowBattle
            0x802a965c,           // set_ship_sail_state
            0x802aba9c,           // set_link_on_board
            0x802a579c,           // set_sword_using: JAIZelBasic::bgmSetSwordUsing
            0x802a5a40,           // change_bgm_status
            0x802a5a78,           // change_sub_bgm_status
            // sub_bgm: JA_BGM_MBOSS (0x19), JA_BGM_MBOSS_S (0x1a), JA_BGM_SEA_ENEMY (0x1c), JA_BGM_P_GANON_1 (0x41) and
            // JA_BGM_P_GANON_2 (0x47)
            0x16000000,
            0,
            0x00000082,
            0,
            0x80000004, // battle_bgm: JA_BGM_BATTLE_NORM
            0x68,       // bgm_handle
            0x22d,      // scene_started
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
    // The sounds that read a port that the game sets, and the values that the rips play them with (ntdlib's
    // WindWakerPortEffects).
    .port_effects = jaudio::WindWakerPortEffects(),
    // The effects that play a key that a random number picks: se3-0049 48 plus a number below 3, se4-0254 48 plus one
    // below 8, se4-0255 36 plus one below 8 and se4-0327 24 plus one below 4. The number is the draw modulo the count
    // (JASystem::Player::getRandomS32, 0x8027e250). The banks give some of those keys a velocity region of volume 0.
    // With the game's sound files, the keys that make a sound are 48, 48 to 50, 36 to 38 and 24 to 25. From the
    // generator's first state, the first draw picks 49, 51, 39 and 27, and all four play nothing. Each key lists the
    // draws before the first draw that picks it.
    .random_effects =
        {
            {0x3831, {{48, 1}, {49, 0}, {50, 4}}},
            {0x48fe, {{48, 8}, {49, 5}, {50, 1}, {51, 0}, {52, 3}, {53, 6}, {54, 13}, {55, 10}}},
            {0x48ff, {{36, 8}, {37, 5}, {38, 1}, {39, 0}, {40, 3}, {41, 6}, {42, 13}, {43, 10}}},
            {0x4947, {{24, 3}, {25, 5}, {26, 1}, {27, 0}}},
        },
    .music_states = kWindWakerMusicStates,
};

// The Wind Waker's Japanese release, Zelda no Densetsu: Kaze no Takuto. The sound library and the functions that the
// driver calls are the same code as the American release's, at other addresses. Only the fields that the linker fills
// in and the line numbers of assertions differ. The sound library's structure offsets don't change. The game's machine
// set-up (mDoMch_Create) gives smaller archive and game heaps. The music sequences are the American ones byte for byte.
// The effects sequence holds the same code for the effects that read a port and for the effects that play a random key.
// Their variants are the American ones. The sound layer (JAIZelBasic) lacks three of the American layer's fields: a
// word at +0x28 and bytes at +0x62 and +0xbf. The fields after them lie 4 or 5 bytes lower. The main music's handle is
// at +0x64 (American +0x68). The byte that the game sets once a scene has started is at +0x229 (American +0x22d). The
// starters, the setters and the table m_bgm_mute_state are the American ones. bgmStart also clears the byte at +0xb7
// (the American +0xbb) as it returns. subBgmStart and seStart set that byte, and the sound layer's constructor clears
// it. The driver calls bgmStart only for the main music. Nothing that the driver starts before it sets the byte.
const GameProfile kWindWakerJpn = {
    .name = "Zelda no Densetsu: Kaze no Takuto",
    .game_id = "GZLJ01",
    .dol_sha1 = "995406d914da8cd1aee4ce58f4c75217c9ec01bc",
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
            0xa3f000,             // archive_heap_size
            0x2ce000,             // game_heap_size
            0x1000,               // command_heap_size
            0xfbe000,             // aram_audio_size: the game's 0xa00000 and most of its 0x5ce000 for graphics
            0x10000,              // aram_graph_size
            8 << 16 | 7 << 8 | 6, // aram_priorities
            0x2000,               // aram_buffer_size
            0x166800,             // audio_heap_size
            0x68,                 // thread_size
            0x8031129c,           // vi_init
            0x8031174c,           // vi_wait_for_retrace
            0x8030577c,           // get_current_thread
            0x80306a94,           // get_thread_priority
            0x80300b40,           // get_arena_lo
            0x80300b38,           // get_arena_hi
            0x802aebd4,           // create_root_heap
            0x802aec4c,           // create_exp_heap
            0x802b090c,           // create_solid_heap
            0x802adee4,           // get_free_size
            0x802ada74,           // become_current_heap
            0x802b1940,           // create_aram
            0x802b3d34,           // set_aram_buffer
            0x802ae2b4,           // alloc
            0x802b14b4,           // thread_ctor
            0x800115a4,           // create_command_heap
            0x80011680,           // create_archive_heap
            0x800113e0,           // create_game_heap
            0x800114c8,           // create_zelda_heap
            0x80017bd0,           // create_disc_thread
            0x803e9d5c,           // audio_heap
            0x800071e0,           // audio_process
            0x803e9d58,           // audio_ready
            0x803eaa18,           // sound_library
            0x8028dc54,           // start_sound
            0x803eaa24,           // wave_systems
            0x803eaa2c,           // wave_status
            0x8028edfc,           // load_wave_group
            0x803eaa9c,           // effects_sequence
            5,                    // sound_state_offset
            0x8028d798,           // set_camera
            0x803e0530,           // default_camera
            0x80296314,           // set_port_data
            0x8027b9f8,           // get_random: JASystem::Player::getRandomS32
            0x802a0c2c,           // bgm_start: JAIZelBasic::bgmStart
            0x802a1f44,           // sub_bgm_start: JAIZelBasic::subBgmStart
            0x802a2644,           // battle_bgm_start: JAIZelBasic::bgmNowBattle
            0x802a6d88,           // set_ship_sail_state
            0x802a91e8,           // set_link_on_board
            0x802a2f28,           // set_sword_using: JAIZelBasic::bgmSetSwordUsing
            0x802a31cc,           // change_bgm_status
            0x802a3204,           // change_sub_bgm_status
            // sub_bgm: JA_BGM_MBOSS (0x19), JA_BGM_MBOSS_S (0x1a), JA_BGM_SEA_ENEMY (0x1c), JA_BGM_P_GANON_1 (0x41) and
            // JA_BGM_P_GANON_2 (0x47)
            0x16000000,
            0,
            0x00000082,
            0,
            0x80000004, // battle_bgm: JA_BGM_BATTLE_NORM
            0x64,       // bgm_handle
            0x229,      // scene_started
        },
    .watch =
        {
            .parse_command = 0x8027da10, // the loop over a track's commands in JASystem::TSeqParser::parseSeq
            .jump_handler = 0x8027c274,  // the sequence parser's command 0xC8, where it takes the jump
            .command_track_register = 31,
            .jump_track_register = 28,
            .jump_target_register = 26,
            .depth_offset = 0xc,
            .wait_offset = 8,
            .note_wait_offset = 0,
            .wave_handle_functions = {0x802835d0, 0x80283eb4}, // TBasicWaveBank's and TSimpleWaveBank's getWaveHandle
            .wave_bank_table = 0x803ea974,                     // JASystem::WaveBankMgr::sWaveBank
            .parameters_offset = 0x3c,
            .parameters_track_offset = 0x1360,
            .effects_sequence = 0x803eaa9c,
            .effect_slot_offset = 4,
            .parent_offset = 0x31c,
            .children_offset = 0x320,
        },
    .port_effects = kWindWakerUsa.port_effects,
    .random_effects = kWindWakerUsa.random_effects,
    .music_states = kWindWakerMusicStates,
};

// Luigi's Mansion (USA). Its sound library is an early JAudio1. The start-up archive is an AAF. The sequences are one
// file, and a table in the AAF divides it. The sound library loads all four wave systems at start-up. The parameters
// follow driver/luigis_mansion.c's Params from frame_retraces to set_shrunk. The table of LuigiSings's states
// (kLuigiSingsStates) follows them.
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
            0x8018c274,           // singing_start
            0x804d91a4,           // music
            0x80188d04,           // set_health
            0x80188ce4,           // set_lights
            0x80188cec,           // set_room_lights
            0x804d91d0,           // room
            0x804d7830,           // room_sound
            0x80189468,           // set_shrunk
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
    .singing_piece = 0x80000007,
    .singing_states = kLuigiSingsStates,
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
    // The game's catalogue of effects, soundTable, and the first action of each type of event, EVENT_OFFSET. ntdlib's
    // lists give the effects that the game's code plays itself and those that the ripper leaves out.
    .jac_effects = {.table = 0x802b33d4,
                    .count = 331,
                    .event_offsets = 0x80225a68,
                    .other_effects = jaudio::kPikminOtherEffects,
                    .left_out = jaudio::kPikminLeftOutEffects,
                    .sounding_event = jaudio::kPikminSoundingEvent,
                    .quiet_event = jaudio::kPikminQuietEvent},
    // The game's set-up of its first scene of play (scene 1) loads groups 1 to 3 of wave system 0, the effects', and
    // group 0 of systems 14, 15 and 2 (Jac_SceneSetup, 0x80019be0).
    .effect_groups = {0 << 16 | 1, 0 << 16 | 2, 0 << 16 | 3, 14 << 16 | 0, 15 << 16 | 0, 2 << 16 | 0},
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
            0x8000c720, // load_wave_scene: WaveScene_Load
            0x803dce00, // scene: current_scene
            0x803e7980, // menu: container. Jac_PlaySystemSe plays effect 7, which closes the menu, only while it's set
            0x80016ba0, // play_system_effect: Jac_PlaySystemSe
            0x800183a0, // play_player_effect: Jac_PlayOrimaSe
            0x800179a0, // create_event: Jac_CreateEvent
            0x80017b20, // play_event_action: Jac_PlayEventAction
            0x80017860, // update_camera: Jac_UpdateCamera
            0x10009,    // system_track: the name of the effects sequence's track for the system's effects
            0x1000a,    // player_track: the name of its track for the player's effects
            0x20000,    // events_track: the name of the events' root track. Its children play the events
            0x80018200, // move_formation: Jac_Orima_Formation
            0x10007,    // formation_track: the name of the effects sequence's track for the formation
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
    // The sound of the formation. Jac_Orima_Formation (0x80018200) takes the player's move as x and y, -127 to 127. It
    // writes x to port 2 of the formation's track and the move's length to port 3. The rip plays a full move straight
    // ahead: x 0 and y 127.
    .port_effects = {{JacEffectId(JacEffect::kFormation, 0), 3, {127}}},
};

// Pikmin 2 (USA). Its sound library is a later JAudio1, under the game's sound layer (PSSystem, PSGame and PSM). The
// start-up archive is an AAF. The game starts its music by file name, through the objects of its sound scenes
// (driver/pikmin2.c). The sound table lists its effects, and PSound.asn names them. Its one wave system has 49 groups,
// one .aw each: wScene_0.aw holds most effects and stays loaded, and a scene loads at most two others. The parameters
// follow driver/pikmin2.c's Params from frame_retraces to big_boss. The table of pieces (ntdlib's kPikmin2Pieces)
// follows them.
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
            0x8009c6a4,           // get_random: JASPlayer::getRandomS32
            0x80516310,           // big_boss
        },
    .pikmin2_pieces = true,
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
    // Three effects draw a random number from JASPlayer::getRandomS32 (0x8009c6a4) and end at once on some draws. From
    // the generator's first state, the first draw ends all three. Each one lists the draws before the first draw on
    // which it plays. Each plays the same sound on every draw that doesn't end it.
    .random_starts =
        {
            {0x5829, 3}, // PSSE_EN_FROG_VOICE
            {0x58fc, 1}, // PSSE_EN_OOPAN_BOUND2
            {0x58fe, 1}, // PSSE_EN_OOPAN_VOICE_DOWN
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

} // namespace gcsf::rip
