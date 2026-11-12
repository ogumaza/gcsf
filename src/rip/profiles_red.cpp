// SPDX-License-Identifier: MIT

// The driver profile of Final Fantasy Crystal Chronicles, whose sound library is Square Enix's Red (see GameProfile
// in rip/rip.h). The comment of the profile says what the game's code does for sound.

#include "rip/profiles.h"
#include "rip/rip.h"

namespace gcsf::rip
{

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

} // namespace gcsf::rip
