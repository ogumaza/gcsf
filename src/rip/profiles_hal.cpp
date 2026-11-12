// SPDX-License-Identifier: MIT

// The driver profile of the Pokémon Colosseum Bonus Disc, whose sound library is HAL Laboratory's (see GameProfile in
// rip/rip.h). The comment of the profile says what the game's code does for sound.

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{

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
    .other_files = {"pm6fgm.ssm"},
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

} // namespace gcsf::rip
