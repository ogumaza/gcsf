// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for WarioWare, Inc.: Mega Party Game$!. The game's sound library is MusyX, under a layer of
 * Intelligent Systems' code. The driver is compiled for Gekko and stored in the game's .gcsflib. The player boots the
 * game's start-up code: the OS initialisation and the static constructors. The OS initialisation also sets up the
 * SDK's disc library. The rip patches the game's main to branch here.
 *
 * The driver then does what the game's start-up does for sound. It leaves out the graphics. The profile in
 * `driver_params` gives the addresses. The ripper fills in the profile. The steps:
 *   1. the game's heaps, in the OS's arena;
 *   2. the sound files: the game's loader reads each one from the disc into the heap, and the driver leaves it where
 *      the game's sound set-up looks for it. The game's start-up loads two of them this way. The set-up loads the
 *      others with a second loader. That loader also reads the controllers and draws on screen. The driver loads all
 *      five. The set-up then uploads the samples to ARAM from memory and frees them;
 *   3. the game's sound set-up: ARAM, the audio interface, MusyX, every sound group and the reverb;
 *   4. the song, by its number, through the game's play function.
 * The driver's thread then sleeps for good, and the song plays from the audio and DSP interrupts. The OS's idle loop
 * runs while no thread can. `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py warioware` (clang). The result is committed as src/rip/driver_warioware_blob.h. Building
 * GCSF itself requires no PowerPC toolchain.
 */

typedef unsigned int u32;
typedef int s32;

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

/* Values of Params.status. They're macros because an enumerator can't hold 0x80000000 before C23. */
#define STATUS_BOOTING 0u
#define STATUS_PLAYING 1u
#define STATUS_FINISHED 2u
#define STATUS_ERROR 0x80000000u /* | step number */

/* The sound files that the game's sound set-up reads: the sample directory, the pool, the project, the songs and the
 * samples. */
#define SOUND_FILES 5u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the song: MusyX's song group in the top byte, the song's number in the low 16 bits */
    u32 sound;    /* unused: the game keeps the song's handle itself */
    /* the game's functions and variables */
    u32 os_sleep_thread; /* void OSSleepThread(OSThreadQueue*) */
    u32 heaps_init;      /* the game's heaps: void (int max_heaps) */
    u32 max_heaps;
    u32 load_file;                  /* void* load(void* to, const char* name, u32 offset, u32 size, s32* result) */
    u32 file_names[SOUND_FILES];    /* the game's names of its sound files */
    u32 file_pointers[SOUND_FILES]; /* the words in which the sound set-up looks for each file */
    u32 sound_init;                 /* the game's sound set-up */
    u32 play_sound;                 /* s32 play(s32 mode, s32 song): the slot, or -1 */
    u32 play_mode;                  /* the mode in which the game plays music */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

typedef u32 (*fn0)(void);
typedef u32 (*fn1)(u32);
typedef u32 (*fn2)(u32, u32);
typedef u32 (*fn5)(u32, u32, u32, u32, u32);

/* An OSThreadQueue (head and tail) that nothing wakes. */
static u32 never_woken[2];

/* The driver's thread sleeps for good. The OS then runs its idle loop. The player's CPU skips that loop to the next
 * interrupt. */
__attribute__((noreturn)) static void sleep_forever(void) {
    for (;;)
        ((fn1)driver_params.os_sleep_thread)((u32)never_woken);
}

__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    sleep_forever();
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The heaps, then the sound files where the sound set-up looks for them. */
    ((fn1)p->heaps_init)(p->max_heaps);
    for (u32 i = 0; i < SOUND_FILES; i++) {
        s32 result = 0;
        const u32 data = ((fn5)p->load_file)(0, p->file_names[i], 0, 0, (u32)&result);
        if (!data || result != 0)
            fail(10 + i);
        *(volatile u32*)p->file_pointers[i] = data;
    }

    /* The sound set-up, then the song. */
    ((fn0)p->sound_init)();
    if ((s32)((fn2)p->play_sound)(p->play_mode, p->sound_id & 0xffff) < 0)
        fail(3);
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
