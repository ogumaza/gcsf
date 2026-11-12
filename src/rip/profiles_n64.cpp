// SPDX-License-Identifier: MIT

// The driver profiles of the games with the Nintendo 64's sound library: Animal Crossing and its Japanese releases
// (see GameProfile in rip/rip.h). The comment of each profile says what the game's code does for sound.

#include <cstdint>
#include <vector>

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{
namespace
{

// The parameters of the driver of Animal Crossing, Doubutsu no Mori+ and Doubutsu no Mori e+
// (driver/animal_crossing.c's Params from frame_retraces on): the sizes and values that the game's start-up gives, the
// addresses of the game's functions and variables, and the game's numbers of the music discs' pieces and of Kapp'n's
// songs. The comments in the driver's Params describe each field.
struct AnimalCrossingParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

    uint32_t frame_retraces = 0, aram_block_count = 0, start_up_sound = 0, start_up_sound_size = 0;
    uint32_t players = 0, player_size = 0, music_player = 0, music_request = 0;
    uint32_t os_init = 0, os_init_alarm = 0, dvd_init = 0, vi_init = 0, vi_wait_for_retrace = 0, ar_init = 0,
             arq_init = 0;
    uint32_t init_audio = 0, game_frame = 0, check_boot = 0, set_weather = 0, start_music = 0;
    uint32_t first_disc = 0, last_disc = 0, disc_stereo = 0, place_stereo = 0;
    uint32_t set_room = 0;
    uint32_t effects_player = 0, player_channels = 0, effect_slots = 0, effect_slot_count = 0, effect_slot_size = 0,
             start_effect = 0;
    uint32_t music_table = 0, first_song = 0, last_song = 0, first_second_part = 0, last_second_part = 0,
             empty_piece = 0, player_ports = 0;
    uint32_t aram_size = 0, wide_numbers = 0, set_island = 0;
};

std::vector<uint32_t> AnimalCrossingParams::Words() const
{
    return {frame_retraces,
            aram_block_count,
            start_up_sound,
            start_up_sound_size,
            players,
            player_size,
            music_player,
            music_request,
            os_init,
            os_init_alarm,
            dvd_init,
            vi_init,
            vi_wait_for_retrace,
            ar_init,
            arq_init,
            init_audio,
            game_frame,
            check_boot,
            set_weather,
            start_music,
            first_disc,
            last_disc,
            disc_stereo,
            place_stereo,
            set_room,
            effects_player,
            player_channels,
            effect_slots,
            effect_slot_count,
            effect_slot_size,
            start_effect,
            music_table,
            first_song,
            last_song,
            first_second_part,
            last_second_part,
            empty_piece,
            player_ports,
            aram_size,
            wide_numbers,
            set_island};
}

} // namespace

// Animal Crossing (USA). Its sound library is Nintendo's sound library for the Nintendo 64, ported to the GameCube. The
// port runs the Nintendo 64's sequence player and synthesis on the CPU, inside an early JAudio1 that mixes their output
// with the DSP's. The game calls it through C functions (Na_*). The sequences, the banks and the samples are in one
// file, `audiorom.img`. The sound library reads all of it into ARAM as it starts. The game's main (0x800060F8) sets up
// the OS, the disc, JSystem and the sound library, and waits until the sound library is up. Then it links the game's
// module. The module calls Na_GameFrame once a frame, and starts its music with Na_BgmStart. The names and addresses
// come from the disc's link map of main.dol (static.map).
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
        AnimalCrossingParams{
            .frame_retraces = 2,           // a frame is two retraces
            .aram_block_count = 3,         // JKRAram's ARInit takes a list of 3 blocks
            .start_up_sound = 0x800c3140,  // nintendo_hi_0
            .start_up_sound_size = 0x66a0, // as the game's sound_initial gives it
            .players = 0x80211f90,         // AG + 0x3788
            .player_size = 0x160,
            .music_player = 0x80217bc0,        // sou_now_bgm_handle
            .music_request = 0x80218361,       // sou_bgm_call_buffer
            .os_init = 0x800788a4,             // OSInit
            .os_init_alarm = 0x80079074,       // OSInitAlarm
            .dvd_init = 0x800850f4,            // DVDInit
            .vi_init = 0x800882c8,             // VIInit
            .vi_wait_for_retrace = 0x80088740, // VIWaitForRetrace
            .ar_init = 0x8008c3cc,             // ARInit
            .arq_init = 0x8008cff4,            // ARQInit
            .init_audio = 0x80010c8c,          // Na_InitAudio
            .game_frame = 0x80010d1c,          // Na_GameFrame
            .check_boot = 0x80015548,          // Na_CheckNeosBoot
            .set_weather = 0x80011158,         // Na_Tenki
            .start_music = 0x80011170,         // Na_BgmStart
            .first_disc = 128,                 // from Sou_Na_BgmStart
            .last_disc = 182,
            .disc_stereo = 9,           // a hi-fi stereo
            .place_stereo = 0x8001405c, // Na_MDPlayerPos
            .set_room = 0x80015578,     // Na_Museum
            .effects_player = 0,        // the game's SE_GROUP
            .player_channels = 0x38,
            .effect_slots = 0x800eb670, // sou_trg_se
            .effect_slot_count = 6,
            .effect_slot_size = 0x20,
            .start_effect = 0x8000c784, // Sou_TrgStart
            .music_table = 0x800a9838,  // SEQ_TABLE
            .first_song = 92,           // the boat's sing_1st_part_bgm_no
            .last_song = 111,
            .first_second_part = 112, // the boat's sing_2nd_part_bgm_no
            .last_second_part = 121,
            .empty_piece = 126,    // its sequence (247) ends at once
            .player_ports = 0x158, // as Nas_GroupSeq reads them
        }
            .Words(),
    .library_name = "AnimalCrossing",
    // __OK: blr. The sound library calls it once the game's sound data is in ARAM. It plays the start-up screen's sound
    // on the early JAudio1's sequence player: about 7 s of a voice and a chime. The game plays no music over that
    // sound.
    // Sou_TrgStart: mr r0, r3 in place of each add of a random number. Sou_TrgStart adds Nap_GetRandom() & 1 to the
    // numbers 0x61, 0x63, 0x65, 0x67, 0x11e and 0x4028. Sou_TrgStart adds Nap_GetRandom() & 3 to 17 numbers: 0x3b,
    // 0x13f, 0x43f and every fourth number from 0x501 to 0x53d except 0x52d and 0x535. The game then plays the effect
    // of the sum. Nap_GetRandom mixes in the time base. Without the adds, a rip plays the effect that its number names.
    .patches = {{0x8000bb84, 0x4e800020}, {0x8000c930, 0x7c601b78}, {0x8000c94c, 0x7c601b78}},
    .music_table = 0x800a9838,      // SEQ_TABLE
    .sequence_table = 0x800cca40,   // AudioseqHeaderStart
    .n64_effect_numbers = {83, 87}, // Sou_BgmStart plays them through Na_SysTrgStart
    .n64_effects = 242,             // the sequence that the game's Sou_InitAudio starts in SE_GROUP
    .n64_effect_banks = 0x188,      // read by the effects channels' scripts
    .n64_players = 0x80211f90,
    .n64_player_count = 5,
    .n64_player_size = 0x160,
    .n64_read_byte = 0x80029a60,                               // Nas_ReadByteData
    .n64_command_reads = {0x8002bbfc, 0x8002ad2c, 0x80029dd0}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x80028ec0,                                    // Common_Com
    .n64_flow_returns = {0x8002bc30, 0x8002adc0, 0x80029e14},
};

// Doubutsu no Mori+ (Japan), disc version 1. Animal Crossing is the game's American release. The sound library is an
// earlier build of the one in Animal Crossing. The functions that the driver calls take the same arguments. The
// structures that the length analysis reads have the same layouts. The names and addresses come from the disc's link
// map of main.dol (static.map). The releases differ in their sounds. Sou_BgmStart plays numbers 83 to 87 as pieces of
// music. Animal Crossing plays them as effects. The effects play in sequence 236. Kapp'n's songs have other notes.
const GameProfile kDoubutsuNoMoriPlusJpn = {
    .name = "Doubutsu no Mori+",
    .game_id = "GAFJ01",
    .dol_sha1 = "25a5ada7a025c092307e61b4420953f36633500d",
    .main = 0x80005dec,
    .driver = Driver::kAnimalCrossing,
    .library = SoundLibrary::kNintendo64,
    .sound_archive = "audiorom.img",
    .params =
        AnimalCrossingParams{
            .frame_retraces = 2,
            .aram_block_count = 3,
            .start_up_sound = 0x800bb900,  // nintendo_hi_0
            .start_up_sound_size = 0x9900, // as the game's main gives it
            .players = 0x80211998,         // AG + 0x3780
            .player_size = 0x160,
            .music_player = 0x802175b0,        // sou_now_bgm_handle
            .music_request = 0x80217c09,       // sou_bgm_call_buffer
            .os_init = 0x8007a58c,             // OSInit
            .os_init_alarm = 0x8007ace4,       // OSInitAlarm
            .dvd_init = 0x80086e14,            // DVDInit
            .vi_init = 0x80089e94,             // VIInit
            .vi_wait_for_retrace = 0x8008a30c, // VIWaitForRetrace
            .ar_init = 0x8008e0ac,             // ARInit
            .arq_init = 0x8008f2c4,            // ARQInit
            .init_audio = 0x8000e31c,          // Na_InitAudio
            .game_frame = 0x8000e428,          // Na_GameFrame
            .check_boot = 0x80011b24,          // Na_CheckNeosBoot
            .set_weather = 0x8000eab0,         // Na_Tenki
            .start_music = 0x8000eac8,         // Na_BgmStart
            .first_disc = 128,                 // from Sou_SpecialRoutine06
            .last_disc = 182,
            .disc_stereo = 9,
            .place_stereo = 0x80010588, // Na_MDPlayerPos
            .set_room = 0x80011b44,     // Na_Museum
            .effects_player = 0,
            .player_channels = 0x38,
            .effect_slots = 0x800e48d0, // sou_trg_se
            .effect_slot_count = 6,
            .effect_slot_size = 0x20,
            .start_effect = 0x8000bfa4, // Sou_TrgStart
            .music_table = 0x800a63e8,  // SEQ_TABLE
            .first_song = 92,
            .last_song = 111,
            .first_second_part = 112,
            .last_second_part = 121,
            .empty_piece = 126, // its sequence (241) ends at once
            .player_ports = 0x158,
        }
            .Words(),
    .library_name = "DoubutsuNoMoriPlus",
    .patches = {{0x8000b7f0, 0x4e800020},  // __OK: blr (see Animal Crossing's profile)
                {0x8000c110, 0x7f83e378},  // Sou_TrgStart: mr r3, r28 in place of the add of Nap_GetRandom() & 1
                {0x8000c120, 0x7f83e378}}, // Sou_TrgStart: mr r3, r28 in place of the add of Nap_GetRandom() & 3
    .music_table = 0x800a63e8,             // SEQ_TABLE
    .sequence_table = 0x800c5200,          // AudioseqHeaderStart
    .n64_effects = 236,
    .n64_effect_banks = 0x188,
    .n64_players = 0x80211998,
    .n64_player_count = 5,
    .n64_player_size = 0x160,
    .n64_read_byte = 0x80026240,                               // Nas_ReadByteData
    .n64_command_reads = {0x80028474, 0x80027588, 0x800265d0}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x80025740,                                    // Common_Com
    .n64_flow_returns = {0x800284a8, 0x80027618, 0x80026614},
};

// Doubutsu no Mori+ (Japan), disc version 0. The disc's link map of main.dol (static.map) gives each address. Of the
// 805 functions of the game's sound code, 794 are disc version 1's code at other addresses. The driver calls two of the
// others. Na_GameFrame also counts down a byte that the game sets elsewhere. Na_MDPlayerPos lacks disc version 1's
// test of a flag. The sequences are disc version 1's, but for the release of one envelope in the effects sequence.
// Sequence 240 differs too. No piece plays it.
const GameProfile kDoubutsuNoMoriPlusJpnV0 = {
    .name = "Doubutsu no Mori+",
    .game_id = "GAFJ01",
    .dol_sha1 = "afb54e84f33c77165b3b7672ab098b88bbdefd09",
    .main = 0x80005dec,
    .driver = Driver::kAnimalCrossing,
    .library = SoundLibrary::kNintendo64,
    .sound_archive = "audiorom.img",
    .params =
        AnimalCrossingParams{
            .frame_retraces = 2,
            .aram_block_count = 3,
            .start_up_sound = 0x800bb5e0,  // nintendo_hi_0
            .start_up_sound_size = 0x9900, // as the game's main gives it
            .players = 0x80211458,         // AG + 0x3780
            .player_size = 0x160,
            .music_player = 0x80217070,        // sou_now_bgm_handle
            .music_request = 0x802176c9,       // sou_bgm_call_buffer
            .os_init = 0x8007a2f0,             // OSInit
            .os_init_alarm = 0x8007aa48,       // OSInitAlarm
            .dvd_init = 0x80086b78,            // DVDInit
            .vi_init = 0x80089bf8,             // VIInit
            .vi_wait_for_retrace = 0x8008a070, // VIWaitForRetrace
            .ar_init = 0x8008de10,             // ARInit
            .arq_init = 0x8008f028,            // ARQInit
            .init_audio = 0x8000e300,          // Na_InitAudio
            .game_frame = 0x8000e40c,          // Na_GameFrame
            .check_boot = 0x80011a78,          // Na_CheckNeosBoot
            .set_weather = 0x8000eab0,         // Na_Tenki
            .start_music = 0x8000eac8,         // Na_BgmStart
            .first_disc = 128,                 // from Sou_SpecialRoutine06
            .last_disc = 182,
            .disc_stereo = 9,
            .place_stereo = 0x8001057c, // Na_MDPlayerPos
            .set_room = 0x80011a98,     // Na_Museum
            .effects_player = 0,
            .player_channels = 0x38,
            .effect_slots = 0x800e4390, // sou_trg_se
            .effect_slot_count = 6,
            .effect_slot_size = 0x20,
            .start_effect = 0x8000bf84, // Sou_TrgStart
            .music_table = 0x800a60c8,  // SEQ_TABLE
            .first_song = 92,
            .last_song = 111,
            .first_second_part = 112,
            .last_second_part = 121,
            .empty_piece = 126, // its sequence (241) ends at once
            .player_ports = 0x158,
        }
            .Words(),
    .library_name = "DoubutsuNoMoriPlus",
    .patches = {{0x8000b7d0, 0x4e800020},  // __OK: blr (see Animal Crossing's profile)
                {0x8000c0f0, 0x7f83e378},  // Sou_TrgStart: mr r3, r28 in place of the add of Nap_GetRandom() & 1
                {0x8000c100, 0x7f83e378}}, // Sou_TrgStart: mr r3, r28 in place of the add of Nap_GetRandom() & 3
    .music_table = 0x800a60c8,             // SEQ_TABLE
    .sequence_table = 0x800c4ee0,          // AudioseqHeaderStart
    .n64_effects = 236,
    .n64_effect_banks = 0x188,
    .n64_players = 0x80211458,
    .n64_player_count = 5,
    .n64_player_size = 0x160,
    .n64_read_byte = 0x80026080,                               // Nas_ReadByteData
    .n64_command_reads = {0x800282b4, 0x800273c8, 0x80026410}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x80025580,                                    // Common_Com
    .n64_flow_returns = {0x800282e8, 0x80027458, 0x80026454},
};

// Doubutsu no Mori e+ (Japan), disc version 1. The sound library is a later build of the one in Animal Crossing. The
// build numbers the pieces and their sequences with 16 bits. SEQ_TABLE holds 384 entries of 16 bits, and the table of
// sequences holds 353 sequences. Na_InitAudio takes the start-up screen's sound, the sound's size and the ARAM for the
// sound library. The game's main works out that size from the size of audiorom.img (Na_GetAudioARAMSize). A sequence
// player takes 4 more bytes. Its fields from +4 on lie up to 4 bytes further on. The names and addresses come from the
// disc's link map of main.dol (static.map).
//
// The music discs' pieces are 208 to 367 (Sou_Na_BgmStart). The island's piece, 376, plays a copy of piece 91's
// sequence. The game mutes six of its channels (BGM_MUTE_PRIVATE_ISLAND) unless the islander is there
// (Na_PrivateIslandStatus). The empty piece is 90. Its sequence (87) has no notes and ends after a tick. The only
// sequence that ends at once is 351. A port can't hold that number.
const GameProfile kDoubutsuNoMoriEPlusJpn = {
    .name = "Doubutsu no Mori e+",
    .game_id = "GAEJ01",
    .dol_sha1 = "7574bec2692efc921c0975c52af41038f3fb7093",
    .main = 0x80006428,
    .driver = Driver::kAnimalCrossing,
    .library = SoundLibrary::kNintendo64,
    .sound_archive = "audiorom.img",
    .params =
        AnimalCrossingParams{
            .frame_retraces = 2,
            .aram_block_count = 3,
            .start_up_sound = 0x800e89e0,  // nintendo_hi_0
            .start_up_sound_size = 0x66a0, // as the game's sound_initial gives it
            .players = 0x8028bb18,         // AG + 0x3800
            .player_size = 0x164,
            .music_player = 0x80291762,        // sou_now_bgm_handle
            .music_request = 0x80292069,       // sou_bgm_call_buffer
            .os_init = 0x8009e514,             // OSInit
            .os_init_alarm = 0x8009ed4c,       // OSInitAlarm
            .dvd_init = 0x800aa430,            // DVDInit
            .vi_init = 0x800ada3c,             // VIInit
            .vi_wait_for_retrace = 0x800adeec, // VIWaitForRetrace
            .ar_init = 0x800b2070,             // ARInit
            .arq_init = 0x800b3bb0,            // ARQInit
            .init_audio = 0x8001544c,          // Na_InitAudio
            .game_frame = 0x800154e4,          // Na_GameFrame
            .check_boot = 0x80019300,          // Na_CheckNeosBoot
            .set_weather = 0x80015944,         // Na_Tenki
            .start_music = 0x8001595c,         // Na_BgmStart
            .first_disc = 208,                 // from Sou_Na_BgmStart
            .last_disc = 367,
            .disc_stereo = 9,
            .place_stereo = 0x80017e14, // Na_MDPlayerPos
            .set_room = 0x80019330,     // Na_Museum
            .effects_player = 0,
            .player_channels = 0x3c,
            .effect_slots = 0x80112310, // sou_trg_se
            .effect_slot_count = 6,
            .effect_slot_size = 0x20,
            .start_effect = 0x80010eac, // Sou_TrgStart
            .music_table = 0x800d1940,  // SEQ_TABLE
            .first_song = 92,
            .last_song = 111,
            .first_second_part = 112,
            .last_second_part = 121,
            .empty_piece = 90, // its sequence (87) ends after a tick, without a sound
            .player_ports = 0x15c,
            .aram_size = 0x81b640, // as the game's main works it out
            .wide_numbers = 1,
            .set_island = 0x80019488, // Na_PrivateIslandStatus
        }
            .Words(),
    .library_name = "DoubutsuNoMoriEPlus",
    // This release's Sou_TrgStart adds Nap_GetRandom() & 1 to 0x63, 0x65, 0x67, 0x11e, 0x148, 0x475 and 0x4028.
    .patches = {{0x80010260, 0x4e800020},  // __OK: blr (see Animal Crossing's profile)
                {0x80011064, 0x7c601b78},  // Sou_TrgStart: mr r0, r3 in place of the add of Nap_GetRandom() & 1
                {0x80011080, 0x7c601b78}}, // Sou_TrgStart: mr r0, r3 in place of the add of Nap_GetRandom() & 3
    .music_table = 0x800d1940,             // SEQ_TABLE
    .sequence_table = 0x800f22e0,          // AudioseqHeaderStart
    .n64_pieces = 384,
    .n64_wide_numbers = true,
    .n64_island_piece = 376,
    .n64_effect_numbers = {83, 87}, // Sou_BgmStart plays them through Na_SysTrgStart
    .n64_effects = 346,
    .n64_effect_banks = 0x188,
    .n64_players = 0x8028bb18,
    .n64_player_count = 5,
    .n64_player_size = 0x164,
    .n64_read_byte = 0x8002da40,                               // Nas_ReadByteData
    .n64_command_reads = {0x8002fbdc, 0x8002ed0c, 0x8002ddb0}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x8002cea0,                                    // Common_Com
    .n64_flow_returns = {0x8002fc10, 0x8002eda0, 0x8002ddf4},
    .n64_player_fields = {0x7c, 0x1c, 0x12},
};

// Doubutsu no Mori e+ (Japan), disc version 0. The disc's link map of main.dol (static.map) gives each address. The
// disc holds disc version 1's audiorom.img. The game's sound driver (game64.c) and its start-up code are disc version
// 1's code at other addresses, with the same constants. Their data differs only in two words of the start-up voice's
// wave table. Of the 873 functions of the sound library, 771 are disc version 1's code. Nas_GroupSeq and Nas_SubSeq
// are among the others. They differ only in their registers and in the order of a few instructions. Their reads of a
// command and their returns from Common_Com lie at disc version 1's offsets.
const GameProfile kDoubutsuNoMoriEPlusJpnV0 = {
    .name = "Doubutsu no Mori e+",
    .game_id = "GAEJ01",
    .dol_sha1 = "25d65de224306e90b12e89613c5658e4214c63b2",
    .main = 0x800064e8,
    .driver = Driver::kAnimalCrossing,
    .library = SoundLibrary::kNintendo64,
    .sound_archive = "audiorom.img",
    .params =
        AnimalCrossingParams{
            .frame_retraces = 2,
            .aram_block_count = 3,
            .start_up_sound = 0x800e6e00,  // nintendo_hi_0
            .start_up_sound_size = 0x66a0, // as the game's sound_initial gives it
            .players = 0x8028aee0,         // AG + 0x3800
            .player_size = 0x164,
            .music_player = 0x80290b22,        // sou_now_bgm_handle
            .music_request = 0x802913e9,       // sou_bgm_call_buffer
            .os_init = 0x8009ddf0,             // OSInit
            .os_init_alarm = 0x8009e628,       // OSInitAlarm
            .dvd_init = 0x800a9c34,            // DVDInit
            .vi_init = 0x800ad208,             // VIInit
            .vi_wait_for_retrace = 0x800ad6b8, // VIWaitForRetrace
            .ar_init = 0x800b196c,             // ARInit
            .arq_init = 0x800b347c,            // ARQInit
            .init_audio = 0x8001556c,          // Na_InitAudio
            .game_frame = 0x80015604,          // Na_GameFrame
            .check_boot = 0x80019420,          // Na_CheckNeosBoot
            .set_weather = 0x80015a64,         // Na_Tenki
            .start_music = 0x80015a7c,         // Na_BgmStart
            .first_disc = 208,                 // from Sou_Na_BgmStart
            .last_disc = 367,
            .disc_stereo = 9,
            .place_stereo = 0x80017f34, // Na_MDPlayerPos
            .set_room = 0x80019450,     // Na_Museum
            .effects_player = 0,
            .player_channels = 0x3c,
            .effect_slots = 0x80110550, // sou_trg_se
            .effect_slot_count = 6,
            .effect_slot_size = 0x20,
            .start_effect = 0x80010fcc, // Sou_TrgStart
            .music_table = 0x800d04c0,  // SEQ_TABLE
            .first_song = 92,
            .last_song = 111,
            .first_second_part = 112,
            .last_second_part = 121,
            .empty_piece = 90, // its sequence (87) ends after a tick, without a sound
            .player_ports = 0x15c,
            .aram_size = 0x81b640, // as the game's main works it out
            .wide_numbers = 1,
            .set_island = 0x800195a8, // Na_PrivateIslandStatus
        }
            .Words(),
    .library_name = "DoubutsuNoMoriEPlus",
    .patches = {{0x80010380, 0x4e800020},  // __OK: blr (see Animal Crossing's profile)
                {0x80011184, 0x7c601b78},  // Sou_TrgStart: mr r0, r3 in place of the add of Nap_GetRandom() & 1
                {0x800111a0, 0x7c601b78}}, // Sou_TrgStart: mr r0, r3 in place of the add of Nap_GetRandom() & 3
    .music_table = 0x800d04c0,             // SEQ_TABLE
    .sequence_table = 0x800f0700,          // AudioseqHeaderStart
    .n64_pieces = 384,
    .n64_wide_numbers = true,
    .n64_island_piece = 376,
    .n64_effect_numbers = {83, 87}, // Sou_BgmStart plays them through Na_SysTrgStart
    .n64_effects = 346,
    .n64_effect_banks = 0x188,
    .n64_players = 0x8028aee0,
    .n64_player_count = 5,
    .n64_player_size = 0x164,
    .n64_read_byte = 0x8002db80,                               // Nas_ReadByteData
    .n64_command_reads = {0x8002fd1c, 0x8002ee4c, 0x8002def0}, // in Nas_GroupSeq, Nas_SubSeq and __Command_Seq
    .n64_flow = 0x8002cfe0,                                    // Common_Com
    .n64_flow_returns = {0x8002fd50, 0x8002eee0, 0x8002df34},
    .n64_player_fields = {0x7c, 0x1c, 0x12},
};

} // namespace gcsf::rip
