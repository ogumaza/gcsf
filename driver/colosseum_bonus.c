// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for the Jirachi download program on the Pokémon Colosseum Bonus Disc. The program is a GameCube
 * disc image of its own, a TGC, inside the Bonus Disc. Its sound library is HAL Laboratory's: a synthesizer on the
 * SDK's AX, and a driver above it that runs a script for each sound effect. The driver is compiled for Gekko and
 * stored in the program's .gcsflib. The player boots the program's start-up code: the OS initialisation and the static
 * constructors. The OS initialisation also sets up the SDK's disc library. The rip patches the program's main to
 * branch here.
 *
 * The driver then does what the program's start-up does for its sound effects. It leaves out the graphics, the
 * streamed music and the program's heaps. The profile in `driver_params` gives the addresses. The ripper fills in the
 * profile. The driver starts the video interface, the audio interface and ARAM first. The video interface's retraces
 * pace the driver. The steps that follow:
 *   2. the sound library, with an allocator over the OS's arena in place of the program's heap;
 *   3. the effects' scripts: their file on the disc;
 *   4. memory for the scripts;
 *   5. the read of the scripts. The driver gives them to the sound library;
 *   6. a bank in the sound library for the effects' samples;
 *   7. the samples: the sound library reads the bank from the disc into ARAM. The driver waits until it has;
 *   8. the effect, by its number. The program plays it the same way.
 * The sound plays from the audio and DSP interrupts. Once a retrace, the driver checks whether the effect still
 * plays. A step that fails stops the driver with its number in the status word (step 1 is the driver's check of its
 * parameter block). `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py colosseum_bonus` (clang). The result is committed as
 * src/rip/driver_colosseum_bonus_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The retraces that the driver waits for the samples and for the effect before it gives up: 10 s. */
#define SETUP_RETRACES 600u

/* The volume and the pan with which the program plays every effect: the highest volume, in the centre. */
#define EFFECT_VOLUME 0xffu
#define EFFECT_PAN 0x7fu

/* DVDFileInfo: 0x3c bytes. The file's length is at 0x34. */
#define FILE_INFO_WORDS 15u
#define FILE_LENGTH_WORD 13u

/* DVDReadPrio's priority for the read, as the program reads the file. */
#define READ_PRIORITY 2u

/* The driver block's address. The allocator ends below it. */
#define DRIVER_BASE 0x81700000u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the effect: its group in bits 16 to 31 and its number in the group in the low 16 bits */
    u32 sound;    /* the effect's entry in the sound library's table, then 0 (written by the driver) */
    /* the game profile: the SDK's functions */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 ai_init;             /* void AIInit(u8* stack) */
    u32 ar_init;             /* u32 ARInit(u32* stack, u32 entries) */
    u32 arq_init;            /* void ARQInit(void) */
    u32 os_get_arena_lo;     /* void* OSGetArenaLo(void) */
    u32 os_get_arena_hi;     /* void* OSGetArenaHi(void) */
    u32 dvd_open;            /* int DVDOpen(const char* path, DVDFileInfo*) */
    u32 dvd_read;            /* s32 DVDReadPrio(DVDFileInfo*, void*, s32 length, s32 offset, s32 priority) */
    /* the game profile: the sound library */
    u32 set_allocator;  /* void (void* (*alloc)(u32 size), void (*free)(void*)) */
    u32 init_sound;     /* int (u32 aram_size, u32 entries, u32 tracks): nonzero once set up */
    u32 aram_size;      /* the program's values for it: ARAM for the sound library, the entries for the effects that */
    u32 entries;        /* play, and the tracks that the effects can play in */
    u32 tracks;
    u32 set_scripts;    /* void (void* scripts): the effects' scripts, read into memory */
    u32 scripts_name;   /* the name of the scripts' file, in the program's data */
    u32 allocate_bank;  /* s32 (u32 size): a bank in ARAM, or -1 */
    u32 bank_size;      /* the program's value for it */
    u32 load_bank;      /* void (const char* path, s32 bank, void (*done)(s32, u32), u32 argument) */
    u32 bank_name;      /* the name of the samples' file, in the program's data */
    u32 start_effect;   /* s32 (u32 effect, u32 volume, u32 pan, u32, u32): the effect's handle, or -1 */
    u32 playing;        /* u32 (void): the effects that play */
    u32 playing_list;   /* the word that holds the first entry of the list of effects that play */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

static void wait_retrace(void) {
    ((fn0)driver_params.vi_wait_for_retrace)();
}

__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    for (;;)
        wait_retrace();
}

/* The sound library's allocator: the OS's arena below the driver block, 32-byte aligned. The program gives the sound
   library an allocator over its heap. Nothing that a rip plays frees memory. */
static u32 heap_next;
static u32 heap_end;

static u32 allocate(u32 size) {
    const u32 at = (heap_next + 31u) & ~31u;
    if (at > heap_end || size > heap_end - at)
        return 0;
    heap_next = at + size;
    return at;
}

static void release(u32 address) {
    (void)address;
}

/* ARInit's block list, and the DVDFileInfo of the scripts' file. */
static u32 aram_blocks[16];
static u32 file_info[FILE_INFO_WORDS];

/* The sound library calls this when the bank has loaded. */
static volatile u32 bank_loaded;

static void bank_done(s32 entry, u32 argument) {
    (void)entry;
    (void)argument;
    bank_loaded = 1;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, the audio interface and ARAM, as the program's main sets them up. */
    ((fn0)p->vi_init)();
    ((fn1)p->ai_init)(0);
    ((fn2)p->ar_init)((u32)aram_blocks, sizeof(aram_blocks) / sizeof(aram_blocks[0]));
    ((fn0)p->arq_init)();

    /* The sound library, with the allocator. */
    heap_next = ((fn0)p->os_get_arena_lo)();
    heap_end = ((fn0)p->os_get_arena_hi)();
    if (heap_end > DRIVER_BASE)
        heap_end = DRIVER_BASE;
    ((fn2)p->set_allocator)((u32)allocate, (u32)release);
    if (!((fn3)p->init_sound)(p->aram_size, p->entries, p->tracks))
        fail(2);

    /* The effects' scripts. The read's length is a multiple of 32 bytes. */
    if (!((fn2)p->dvd_open)(p->scripts_name, (u32)file_info))
        fail(3);
    const u32 length = (file_info[FILE_LENGTH_WORD] + 31u) & ~31u;
    const u32 scripts = allocate(length);
    if (!scripts)
        fail(4);
    if ((s32)((fn5)p->dvd_read)((u32)file_info, scripts, length, 0, READ_PRIORITY) < 0)
        fail(5);
    ((fn1)p->set_scripts)(scripts);

    /* The effects' samples. */
    const s32 bank = (s32)((fn1)p->allocate_bank)(p->bank_size);
    if (bank < 0)
        fail(6);
    ((fn4)p->load_bank)(p->bank_name, (u32)bank, (u32)bank_done, 0);
    for (u32 retraces = 0; !bank_loaded; retraces++) {
        if (retraces > SETUP_RETRACES)
            fail(7);
        wait_retrace();
    }

    /* The effect. The sound library puts its entry at the head of the list of effects that play. The entry leaves the
       list once the effect's voices have stopped. */
    if ((s32)((fn5)p->start_effect)(p->sound_id, EFFECT_VOLUME, EFFECT_PAN, 0, 0) < 0)
        fail(8);
    p->sound = word(p->playing_list);
    p->status = STATUS_PLAYING;

    for (;;) {
        wait_retrace();
        if (p->status == STATUS_PLAYING && !((fn0)p->playing)()) {
            p->sound = 0;
            p->status = STATUS_FINISHED;
        }
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
