// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Mario Party 4. The game's sound library is MusyX, under Hudson's sound manager (its msm
 * functions). The sound manager reads one file of sound data. That file holds every sound group, song and effect. The
 * driver is compiled for Gekko and stored in the game's .gcsflib. The player boots the game's start-up code: the OS
 * initialisation and the static constructors. The rip patches the game's main to branch here.
 *
 * The driver then does what the game's start-up and its sound test do for a song. It leaves out the graphics. The
 * profile in `driver_params` gives the addresses. The ripper fills in the profile. The steps:
 *   - the disc library (DVDInit), after OSInit as in the game's system set-up;
 *   - the sound manager's heap in the OS's arena, at the size that the game gives it;
 *   - msmSysInit with the paths of the sound manager's file and the stream file. msmSysInit starts the ARAM, the audio
 *     interface and MusyX. It reads the tables of the sound manager's file and sets up the reverbs and the stream
 *     slots;
 *   - the output mode that the game's audio set-up picks on a console set to stereo: MusyX's surround mode;
 *   - the base groups that the game's boot module loads;
 *   - the group set that the game's sound test loads for the song;
 *   - the song, by its number in the sound manager's table. The game's play function starts it the same way.
 * The driver's thread then sleeps for good. The song plays from the audio and DSP interrupts. The sound manager runs
 * its fades and its streams from the audio interface's interrupt. The OS's idle loop runs while no thread can. A step
 * that fails stops the driver with its number in the status word: 1 for the driver's check of its parameter block, 2
 * for the heap, 3 for msmSysInit, 4 for the base groups, 5 for the group set and 6 for the song. `driver_params.status`
 * reports the driver's progress to the player (see docs/gcsf.md).
 *
 * The driver hands out the OS's arena from the bottom up and never takes memory back. The sound manager reads a group's
 * samples into a buffer and moves them to ARAM. The game then frees the buffer. The driver keeps it.
 *
 * Build with `driver/build.py mario_party` (clang). The result is committed as src/rip/driver_mario_party_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* A sound ID's group set of 0xffff loads no group set. */
#define NO_GROUP_SET 0xffffu

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the song: the group set in the top 16 bits, the song's number in the low 16 bits */
    u32 sound;    /* unused: the sound manager keeps the song's state itself */
    /* the OS and the disc */
    u32 os_sleep_thread; /* void OSSleepThread(OSThreadQueue*) */
    u32 os_get_arena_lo; /* void* OSGetArenaLo(void) */
    u32 os_get_arena_hi; /* void* OSGetArenaHi(void) */
    u32 dvd_init;        /* void DVDInit(void) */
    /* the sound manager */
    u32 msm_sys_init;            /* s32 msmSysInit(MSM_INIT*, MSM_ARAM*) */
    u32 msm_path;                /* the game's path of the sound manager's file */
    u32 pdt_path;                /* the game's path of the stream file */
    u32 heap_size;               /* the sound manager's heap */
    u32 aram_end;                /* the end of the ARAM that the sound manager may take */
    u32 msm_sys_set_output_mode; /* BOOL msmSysSetOutputMode(SND_OUTPUTMODE) */
    u32 output_mode;
    u32 msm_sys_get_samp_size;  /* s32 msmSysGetSampSize(BOOL other): the size of the largest samples of a base
                                   group, or with `other` set of any other group */
    u32 msm_sys_load_group;     /* s32 msmSysLoadGroup(s32 group, void* buffer, BOOL): group 0 loads the base groups */
    u32 msm_sys_load_group_set; /* s32 msmSysLoadGroupSet(s32 set, void* buffer) */
    u32 msm_mus_play;           /* s32 msmMusPlay(s32 song, MSM_MUSPARAM*): the song's channel, or below 0 */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "sleep.h"

/* The part of the OS's arena that the driver hasn't handed out yet. */
static u32 heap_next;
static u32 heap_end;

/* msmSysInit's MSM_INIT: the paths of the sound manager's file and the stream file, three disc functions (0 for the
 * sound manager's defaults), then the heap and its size. */
static u32 msm_init[7];

/* msmSysInit's MSM_ARAM: the game's flag 1, then the end of the ARAM that the sound manager may take. With the flag
 * set, msmSysInit checks that MusyX's share of ARAM ends there. It then starts the ARAM without allocating that share.
 * The last word belongs to the structure's other form. */
static u32 msm_aram[3];

/* `size` bytes of the arena, at a 32-byte boundary: the DSP and the disc require that for their transfers. Returns 0
 * when the arena is used up. */
static u32 heap_alloc(u32 size) {
    const u32 at = (heap_next + 31) & ~31u;
    if (at + size < at || at + size > heap_end)
        return 0;
    heap_next = at + size;
    return at;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    ((fn0)p->dvd_init)();

    /* The sound manager's heap, then the sound manager. */
    heap_next = ((fn0)p->os_get_arena_lo)();
    heap_end = ((fn0)p->os_get_arena_hi)() & ~31u;
    const u32 heap = heap_alloc(p->heap_size);
    if (!heap)
        fail(2);
    msm_init[0] = p->msm_path;
    msm_init[1] = p->pdt_path;
    msm_init[5] = heap;
    msm_init[6] = p->heap_size;
    msm_aram[0] = 1;
    msm_aram[1] = p->aram_end;
    if ((s32)((fn2)p->msm_sys_init)((u32)msm_init, (u32)msm_aram) < 0)
        fail(3);
    ((fn1)p->msm_sys_set_output_mode)(p->output_mode);

    /* The base groups, then the song's group set. */
    const u32 base = heap_alloc(((fn1)p->msm_sys_get_samp_size)(0));
    if (!base || (s32)((fn3)p->msm_sys_load_group)(0, base, 0) < 0)
        fail(4);
    const u32 set = p->sound_id >> 16;
    if (set != NO_GROUP_SET) {
        const u32 samples = heap_alloc(((fn1)p->msm_sys_get_samp_size)(1));
        if (!samples || (s32)((fn2)p->msm_sys_load_group_set)(set, samples) < 0)
            fail(5);
    }

    /* The song. */
    if ((s32)((fn2)p->msm_mus_play)(p->sound_id & 0xffff, 0) < 0)
        fail(6);
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
