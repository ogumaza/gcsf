// SPDX-License-Identifier: MIT

// The driver profiles of the games with MusyX that driver/musyx.c and its variants play (see GameProfile in
// rip/rip.h). The comment of each profile says what the game's code does for sound.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{
namespace
{

// The parameters of the MusyX driver (driver/musyx.c's Params from os_sleep_thread on): the addresses of the game's
// functions for the OS, the disc, the ARAM, the audio interface and MusyX, MusyX's settings, and studio 0's aux
// effects, A then B. A function that the game doesn't call is 0. driver/musyx.c's Params says what each field means.
struct MusyXParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

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
    uint32_t snd_volume = 0, snd_fx_ctrl = 0, snd_fx_ctrl14 = 0, group_volume = 0;
    uint32_t snd_master_volume = 0, master_volume = 0, master_music = 0, master_fx = 0;
    uint32_t song_fade_in = 0, volume_group = 0xff;
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
            snd_fx_ctrl14,
            group_volume,
            snd_master_volume,
            master_volume,
            master_music,
            master_fx,
            song_fade_in,
            volume_group};
}

// The places of some of the parameters among the words (kMusyXAuxCallbacks). Words() lists the fields in their order.
static_assert(offsetof(MusyXParams, voices) == kMusyXVoices * 4);
static_assert(offsetof(MusyXParams, studios) == kMusyXStudios * 4);
static_assert(offsetof(MusyXParams, aux_callback) == kMusyXAuxCallbacks * 4);
static_assert(offsetof(MusyXParams, aux_user) == kMusyXAuxBlocks * 4);
static_assert(offsetof(MusyXParams, aux_prepare) == kMusyXAuxPreparations * 4);
static_assert(offsetof(MusyXParams, group_volume) == kMusyXGroupVolume * 4);
static_assert(offsetof(MusyXParams, snd_master_volume) == kMusyXMasterVolume * 4);
static_assert(offsetof(MusyXParams, song_fade_in) == kMusyXSongFadeIn * 4);

// The parameters of driver/musyx_song_controls.c: driver/musyx.c's, then sndSeqCntrl, OSCreateAlarm and OSSetAlarm,
// and the time from a song's start to the settings of its controllers in ticks of the time base.
struct MusyXSongControlParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

    MusyXParams musyx;
    uint32_t snd_seq_cntrl = 0, os_create_alarm = 0, os_set_alarm = 0, song_control_delay = 0;
};

std::vector<uint32_t> MusyXSongControlParams::Words() const
{
    std::vector<uint32_t> words = musyx.Words();
    words.insert(words.end(), {snd_seq_cntrl, os_create_alarm, os_set_alarm, song_control_delay});

    return words;
}

// The parameters of driver/musyx_upload.c: driver/musyx.c's (MusyXParams), then sndSetSampleDataUploadCallback and
// aramSyncTransferQueue. driver/musyx.c's Params says what each field means.
struct MusyXUploadParams
{
    // The words in the order of the driver's parameter block.
    std::vector<uint32_t> Words() const;

    MusyXParams musyx;
    uint32_t snd_set_upload_callback = 0, aram_sync = 0;
};

std::vector<uint32_t> MusyXUploadParams::Words() const
{
    std::vector<uint32_t> words = musyx.Words();
    words.insert(words.end(), {snd_set_upload_callback, aram_sync});

    return words;
}

} // namespace

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
// then takes the one of 3.6 s, as at start-up. At 0x800A2374 the set-up calls sndVolume(127, 0, 0xFF). That call gives
// the volume groups of the songs and the effects their full volume.
//
// The game's sound layer starts an effect through 0x800A1978 or 0x800A13D8. Each calls sndFXStartEx with studio 0.
// Every call of 0x800A1978 passes a volume of 127 and a pan of 64. The voices in battle go through 0x800F036C and
// 0x800A13D8, with a volume and a pan from their caller. The game can later set an effect's pan from the place of the
// sound's source, through controller 10 (0x800A17B4). A rip plays every effect at 127 in the centre. Every row of the
// groups' tables of effects gives those values too.
//
// The length analysis reads the first of MusyX's song instances, at 0x802FB7E8. Its 16 sections start 0x1508 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34. The analysis reads MusyX's 64 voices for an effect.
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
            .snd_volume = 0x80138f0c,
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
    .musyx_voices = {.count = 64,
                     .synth = 0x8035ac60,
                     .synth_size = 0x458,
                     .dsp = 0x8035ad48,
                     .dsp_size = 0xf8,
                     .state_offset = 0xf0,
                     .loop_handler = 0x8013e8a4},
};

// Star Fox Adventures (USA, disc version 1). Its sound library is MusyX, under Rare's sound layer.
//
// The game's start-up initialises the disc. Its sound set-up (0x80009BF8) initialises the ARAM with a stack of 10
// entries at 0x803369F4. It then starts the audio interface, sets MusyX's memory hooks and calls sndInit: 48 voices, 48
// for music, 24 for effects, one studio, the flag that encodes the default studio for Dolby Pro Logic II, and 16 MiB of
// ARAM. It then sets the same numbers of voices again. The output is in stereo unless the console is set to mono. The
// set-up then gives studio 0's aux A a standard reverb of 3 s and calls sndVolume(127, 0, 0xFF) at 0x80009D84. That
// call gives the volume groups of the songs and the effects their full volume. A call of sndMasterVolume(127, 100, 1,
// 1) follows. MusyX's master volumes start at 127. The call leaves them there.
//
// The game starts every song through 0x8000B390, from a trigger in Music.bin. The function starts the song through
// sndSeqPlayEx at a volume of 0 (0x8000B470 to 0x8000B49C). It then calls sndSeqVolume(the trigger's volume, 500, the
// song, 0) at 0x8000B4B4. MusyX raises the song's volume over 500 ms. A rip does the same (song_fade_in). The trigger's
// speed of 256 is MusyX's normal speed (seqStartPlay, 0x8026D054 and 0x8026D0E4).
//
// The game starts an effect through 0x8000C4D8. That function calls sndFXStartEx with the volume of the effect's row in
// Sfx.bin, a pan of 0x40 and studio 0. In some places of the game's world, 0x8000BB64 sets a level for the effects'
// sends to aux A. 0x8000C4D8 then sends each effect there with controller 0x5B. Everywhere else, and in a rip, the
// level is 0 and the function sets no controller.
//
// The length analysis reads the first of MusyX's song instances, at 0x803B15B0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34. The analysis reads MusyX's 48 voices for an effect.
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
            .snd_fx_start_ex = 0x8027300c,
            .snd_set_aux_processing_callbacks = 0x802732c0,
            .aux_callback = {0x80285324, 0}, // sndAuxCallbackReverbSTD for aux A, and nothing for aux B
            .aux_user = {0x803368a0, 0},     // aux A's reverb
            .aux_prepare = {0x80285380, 0},  // sndAuxCallbackPrepareReverbSTD for aux A
            .aux_midi = {0xff, 0xff},        // no MIDI channel controls aux A or aux B
            .snd_volume = 0x802730d4,
            .song_fade_in = 500, // every song (0x8000B4B4)
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
    .musyx_voices = {.count = 48,
                     .synth = 0x803deee8,
                     .synth_size = 0x404,
                     .dsp = 0x803defc4,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x80275dd0},
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
// The set-up then calls sndVolume(127, 0, 0xFF) (0x7FD49310 in the game). That call gives every volume group its full
// volume. The game's table of sounds (0x7FDDBF30 in the game) holds 522 rows of 12 bytes. A row gives an effect's ID
// in a big-endian half and the effect's volume in the byte at 3. Two floats follow. An ID of 0xFFFF marks an empty
// row. Several rows can give one effect, at different volumes. The function at 0x7FD5123C plays a row through
// sndFXStartEx, with a pan from its caller and studio 0. Three functions (0x7FD4BF18, 0x7FD4BF80 and 0x7FD4BFE8) play
// rows 489 to 491 at the row's volume times a setting, with a pan of 64. The function at 0x7FD51294 starts a row's
// effect through 0x80011D18 instead, with values for controllers 0x83 and 0x0B. A rip plays an effect at the most
// common volume of its rows, in the centre, with no controllers. An effect that no row names takes the volume and the
// pan of its row in its group's table of effects.
//
// The length analysis reads the first of MusyX's song instances, at 0x800694F0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34. The analysis reads MusyX's 56 voices for an effect.
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
            .snd_fx_start_ex = 0x80011c98,
            .snd_set_aux_processing_callbacks = 0x80012070,
            .aux_callback = {0x80026ea4, 0}, // sndAuxCallbackReverbHI for aux A, and nothing for aux B
            .aux_user = {0x800ae1e0, 0},     // aux A's reverb
            .aux_prepare = {0x80026f00, 0},  // sndAuxCallbackPrepareReverbHI for aux A
            .aux_midi = {0xff, 0xff},        // no MIDI channel controls aux A or aux B
            .snd_volume = 0x80011f90,
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
    .musyx_voices = {.count = 56,
                     .synth = 0x80096498,
                     .synth_size = 0x404,
                     .dsp = 0x80096574,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x80016468},
    .musyx_effects = 0x7fddbf30,
    .musyx_effect_count = 522,
};

// Pokémon Colosseum (USA). Its sound library is MusyX, under the game's sound layer.
//
// OSInit initialises the disc. The game's main initialises the ARAM with a stack of 2 entries at 0x80478DC0 and starts
// the audio interface. Its sound set-up (0x80166E88) sets MusyX's memory hooks and calls sndInit: 64 voices, 48 for
// music, 16 for effects, one studio, and 0x9FC000 bytes of ARAM. It then writes the settings of reverb preset 0 into
// aux A's high-quality reverb, prepares the reverb and gives it to studio 0's aux A. The output is in stereo unless the
// console is set to mono. The game's module common_rel holds its table of sounds (see MusyXFiles::kColosseum). The
// game's play function (0x80166AB8) sets a song's reverb preset, then plays the song at volume 0x7F. No call in the
// game's code gives a song another volume. The set-up calls sndVolume(100, 0, SND_FX_VOLGROUPS) (0x80167008). That
// call sets fader 31 of MusyX's faders (0x80434E64, 0x30 bytes each) to 100/127. MusyX puts every effect in that
// volume group. An effect's rip writes the fader's volume and its target.
//
// The length analysis reads the first of MusyX's song instances, at 0x804285D0. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34. For an effect, it reads MusyX's voices.
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
            .snd_fx_start_ex = 0x8014d7fc,
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
    // The effects' fader at 100/127 in floats: its volume, then its target.
    .musyx_effect_writes = {{0x80435434, 0x3f499326}, {0x80435438, 0x3f499326}},
    .musyx_file_table = 0x1133d8,
    .musyx_file_count = 79,
    .musyx_reverb_presets = 0x145044,
    // The settings come from each sound's preset.
    .reverbs = {{{.block = 0x80452500, .high = true}}},
    .song_lists = {0x8047af14, 0x8047af10},
    .section_loops = 0x80429aec,
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 64,
                     .synth = 0x8047af48,
                     .synth_size = 0x404,
                     .dsp = 0x8047b024,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x801529a4},
};

// Resident Evil (USA, disc 1). The game's sound library is an early build of MusyX under Capcom's sound driver
// (0x800671B8 takes the driver's commands). The game's sound set-up (0x80063990) takes the driver's command 2
// (0x80066458): 16 blocks of ARAM, 8 MiB of ARAM for MusyX, 48 voices, 32 for music and 32 for effects, with one
// studio. The game allocates the ARAM's blocks from its heap. A rip puts them in the driver's state at 0x80225080. No
// code of a rip uses that state. The output is in stereo unless the console is set to mono (0x80063888). Command 45
// (0x800662C4) gives studio 0's aux A MusyX's high-quality reverb of 4.2 s and aux B its standard reverb of 0.9 s. It
// ends with sndVolume(100, 0, 0xFF). A song's start gives the song's group its full volume. That call therefore reaches
// only effects. The game's options start at 100 (0x8001ADAC). The music's option gives sndMasterVolume(110, time, 1, 0)
// (0x800620B4, 0x80066064).
//
// A room's table names up to four songs that play at once (0x80061B1C). The code of a room or an event changes the
// levels of channels through 0x80061E6C, above all in the ambience of songs 0x6E to 0x7C. A rip plays each song alone.
// The rips of songs 0x74 and 0x7A that ntdlib's ReadCatalogue lists as mixes play the levels that rooms give their
// channels. driver/musyx_song_controls.c sets them through sndSeqCntrl (0x80155BB8), as the game's driver does. An OS
// alarm (OSCreateAlarm at 0x801117AC, OSSetAlarm at 0x80111A08) sets them 250 ms after the song's start: after the
// song's first pattern, whose controllers end at tick 64 (83 ms at sg7a's 120 beats a minute, 104 ms at sg74's 96), and
// before its first note, at tick 496 (646 ms and 807 ms). Song 0x7A starts with all seven channels at level 0. The rip
// of the file's mix makes no sound, and the ripper leaves it out.
//
// The length analysis reads the first of MusyX's song instances, at 0x80238868. Resident Evil Zero's analysis does the
// same. It reads MusyX's 48 voices for an effect.
const GameProfile kResidentEvilUsa = {
    .name = "Resident Evil",
    .game_id = "GBIE08",
    .dol_sha1 = "7e514a94826c672605a4f484367ec511fd27933f",
    .main = 0x8001aeac,
    .driver = Driver::kMusyXSongControls,
    .library = SoundLibrary::kMusyX,
    .params =
        MusyXSongControlParams{
            .musyx =
                MusyXParams{
                    .os_sleep_thread = 0x80116de0,
                    .os_get_arena_lo = 0x801121b0,
                    .os_get_arena_hi = 0x801121a8,
                    .startup = {0x80119a84}, // DVDInit
                    .dvd_open = 0x80119428,
                    .dvd_read_async_prio = 0x80119738,
                    .dvd_get_command_block_status = 0x8011bddc,
                    .dvd_close = 0x801194f0,
                    .ar_init = 0x80120f20,
                    .aram_blocks = 0x80225080,
                    .aram_block_count = 16,
                    .arq_init = 0x80121b54,
                    .ai_init = 0x80120858,
                    .snd_set_hooks = 0x8016937c,
                    .snd_init = 0x801664fc,
                    .init_form = 1, // sndInit takes the number of studios
                    .voices = 48,
                    .music = 32,
                    .sfx = 32,
                    .studios = 1,
                    .init_flags = 0, // SND_FLAGS_DEFAULT
                    .aram_size = 0x800000,
                    .snd_output_mode = 0x80156118,
                    .output_mode = 1, // SND_OUTPUTMODE_STEREO
                    .snd_push_group = 0x80160944,
                    .snd_seq_play = 0x80160d34, // sndSeqPlayEx
                    .seq_play_form = 1,         // sndSeqPlayEx takes a studio
                    .snd_seq_volume = 0x80155a48,
                    .snd_fx_start_ex = 0x80155dc0,
                    .snd_set_aux_processing_callbacks = 0x80156198,
                    .aux_callback = {0x8016a914, 0x8016a0b0}, // sndAuxCallbackReverbHI, sndAuxCallbackReverbSTD
                    .aux_user = {0x8022737c, 0x80227228},     // their reverbs
                    .aux_prepare = {0x8016a970, 0x8016a10c},  // sndAuxCallbackPrepareReverbHI and STD
                    .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
                    .snd_volume = 0x80156024,
                    .group_volume = 100,
                    .snd_master_volume = 0x80156084,
                    .master_volume = 110,
                    .master_music = 1,
                    .master_fx = 0,
                },
            .snd_seq_cntrl = 0x80155bb8,
            .os_create_alarm = 0x801117ac,
            .os_set_alarm = 0x80111a08,
            .song_control_delay = 10125000, // 250 ms at 40.5 MHz. The time base counts the bus clock / 4.
        }
            .Words(),
    .library_name = "ResidentEvil",
    .musyx_files = MusyXFiles::kResidentEvilSnd,
    .reverbs =
        {{{.block = 0x8022737c,
           .coloration = 0.5f,
           .mix = 0.8f,
           .time = 4.2f,
           .damping = 0.5f,
           .pre_delay = 0.0f,
           .high = true,
           .crosstalk = 0.0f},
          {.block = 0x80227228, .coloration = 0.5f, .mix = 0.8f, .time = 0.9f, .damping = 0.5f, .pre_delay = 0.03f}}},
    .song_lists = {0x80260f3c, 0x80260f38}, // seqActiveRoot and seqPausedRoot
    .section_loops = 0x80239d84,            // in seqInstance
    .section_loop_size = 0x38,
    .musyx_voices = {.count = 48,
                     .synth = 0x80260f68, // synthVoice
                     .synth_size = 0x404,
                     .dsp = 0x80261044, // dspVoice
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x8015a34c}, // mcmdLoop
};

// Resident Evil Zero (USA). Both discs hold the same code. Each disc holds the songs of its part of the game, and the
// songs that both parts play. A rip's library therefore takes the disc's number. The game's sound library is MusyX,
// under Capcom's sound layer.
//
// OSInit initialises the disc and lets the disc's library take long names. The game's sound set-up (0x80148DCC)
// initialises the ARAM with a stack of 24 entries at 0x8031B140. It then starts the audio interface, sets MusyX's
// memory hooks and calls sndInit: 64 voices, 32 for music, 32 for effects, one studio, and 10 MiB of ARAM. The output
// is in stereo unless the console is set to mono. The game's options offer only those two. The set-up then gives
// studio 0's aux A and aux B MusyX's high-quality reverb. Aux A's lasts 1.5 s, and aux B's 1 s. The set-up also calls
// sndMasterVolume(127, 0, 1, 1) and sndVolume(127, 0, 0xFF) (0x80148F88). That call gives every volume group its full
// volume.
//
// The game's start-up (0x8014BA5C) pushes the group of bio0/sound/all.arc, then the groups of the archives in its table
// at 0x80255D68, and keeps them. A row of the table gives a group's ID in a big-endian word and the address of the
// archive's name. Ten rows name archives of weapons, such as we02. A negative ID ends the table. A rip of an effect
// pushes the group of all.arc and the effect's group (see ntdlib's ReadCatalogue). The game starts an effect through
// 0x80149918, with a volume and a pan from the caller. 112 of the 117 calls there give a volume of 0xFF. MusyX then
// plays the effect at the volume of the effect's row. 72 of the calls give a pan of 64, and 12 give 0xFF. 98% of the
// rows give a pan of 64, and so do the rows of the effects that calls name by a constant ID. 0x801499B8 then sets
// controller 0x5D, aux B's send, to a level that the game keeps at 0x80379BC0. A rip plays an effect at the volume and
// the pan of its row, without that send.
//
// The length analysis reads the first of MusyX's song instances, at 0x8033AA90. Its 16 sections start 0x14E8 bytes into
// it, 0x38 bytes apart, and each holds its count of loops at +0x34. The analysis reads MusyX's 64 voices for an effect.
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
            .snd_volume = 0x801cf2f8,
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
    .musyx_voices = {.count = 64,
                     .synth = 0x8037a300,
                     .synth_size = 0x404,
                     .dsp = 0x8037a3dc,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x801d553c},
    .musyx_effects = 0x80258be4, // the cutscenes' effects that the game plays as songs (see ntdlib's ReadCatalogue)
    .musyx_effect_count = 135,   // the cutscenes 0 to 134 (the next table starts at 0x80258E00)
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
// and Dr. Mario then call sndVolume(110, 0, 0xFF). A song's start sets the song's group again. A rip of a song leaves
// the second call out. A rip of an effect makes the call (see AddPuzzleMenu). Yoshi's Cookie also sets MusyX's master
// volumes from its options (see AddYoshisCookie).
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
            .snd_fx_start_ex = 0x800227d4,
            .snd_set_aux_processing_callbacks = 0x80022b54,
            .aux_callback = {0x80035f00, 0x80037088}, // sndAuxCallbackReverbHI and sndAuxCallbackChorus
            .aux_user = {0x80100000, 0x80100200},     // aux A's reverb and aux B's chorus (see above)
            .aux_prepare = {0x80035f5c, 0x800373f4},  // sndAuxCallbackPrepareReverbHI and sndAuxCallbackPrepareChorus
            .aux_midi = {0xff, 0xff},                 // no MIDI channel controls aux A or aux B
            .snd_volume = 0x80022968,
            .snd_fx_ctrl = 0x800226d4,
            .snd_fx_ctrl14 = 0x80022734,
            .snd_master_volume = 0x800229c8, // for Yoshi's Cookie's sounds alone (see AddYoshisCookie)
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
    .musyx_voices = {.count = 64,
                     .synth = 0x8022d730,
                     .synth_size = 0x404,
                     .dsp = 0x8022d80c,
                     .dsp_size = 0xf4,
                     .state_offset = 0xec,
                     .loop_handler = 0x80025a60},
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
// streams its music. Its jingles and some of its effects are MusyX songs (see MusyXFiles::kPaperMario).
//
// The game has no DVDReadAsyncPrio. The driver reads through DVDReadPrio (0x802A03C0) instead. That one waits for the
// read and takes the priority where DVDReadAsyncPrio takes the callback. The driver passes 0 there.
//
// The game's sound set-up (0x800E239C) initialises the ARAM with a stack of 8 entries, its queue and the audio
// interface. It sets MusyX's memory hooks and calls sndInit: 64 voices, 12 for music, 44 for effects, one studio, the
// flag that encodes the default studio for Dolby Pro Logic II, and 0xBFC000 bytes of ARAM. It gives studio 0's aux A
// MusyX's high-quality reverb of 3 s and aux B MusyX's chorus. It calls sndVolume(127, 0, 0xFF) and sets the output
// to stereo. A rip writes the chorus's settings: a delay of 10 ms, no variation and a period of 500 ms.
//
// The length analysis reads the first of MusyX's song instances, at 0x803EBDC0. The instance's 16 sections start
// 0x1508 bytes in, 0x38 bytes apart. Each section holds a count of loops at +0x34.
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
    .song_lists = {0x8041ee5c, 0x8041ee58},
    .section_loops = 0x803ed2fc,
    .section_loop_size = 0x38,
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

// Pokémon Channel (USA). Its sound library is MusyX 2.0, the build of Metroid Prime's. The game plays its music as
// MusyX effects (see MusyXFiles::kPokemonChannel). One sample file holds every sample of the game, more than main
// memory holds. The game hands MusyX the samples a chunk at a time, through MusyX's upload callback. A rip does the
// same through driver/musyx_upload.c.
//
// OSInit initialises the disc. The game's start-up initialises the ARAM with a stack of 2 entries at 0x8035054C (at
// 0x80034A0C), its queue and the audio interface. The game's sound set-up (0x8001498C) sets MusyX's memory hooks and
// calls sndInit: 64 voices, none for music, 64 for effects, one studio, the flag that encodes the default studio for
// Dolby Pro Logic II, and 0xFFC000 bytes of ARAM. The output is in stereo unless the console is set to mono. The
// set-up then calls sndMasterVolume(100, 100, 1, 1) and sndVolume(127, 100, 0xFF). A rip makes both calls with a time
// of 0, sndVolume first. The game gives studio 0 no aux effects. Its loader of groups (0x80015228) sets the upload
// callback, with chunks of 0x20000 bytes, before it pushes a group.
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
                    .snd_master_volume = 0x80119c5c,
                    .master_volume = 100,
                    .master_music = 1,
                    .master_fx = 1,
                },
            .snd_set_upload_callback = 0x80126328,
            .aram_sync = 0x8012f090,
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
// into it, 0x2C bytes apart, and each holds its count of loops at +0x26. The early MusyX's voices differ in size from
// those of MusyX 2.0. A synthesizer voice takes 0x408 bytes, and a DSP voice 0xF0 with its state at +0xE8.
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
            .snd_fx_start_ex = 0x80047dd4,
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
    .musyx_voices = {.count = 48,
                     .synth = 0x800a91d8,
                     .synth_size = 0x408,
                     .dsp = 0x800a92b4,
                     .dsp_size = 0xf0,
                     .state_offset = 0xe8,
                     .loop_handler = 0x8004aadc},
};

} // namespace gcsf::rip
