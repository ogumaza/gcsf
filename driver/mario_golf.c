// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Mario Golf: Toadstool Tour, whose sound library is MusyX, under a layer of Camelot code. It's
 * compiled for Gekko and stored in the game's .gcsflib. The player boots the game's start-up code: the OS
 * initialisation and the static constructors. The rip patches the game's main to branch here.
 *
 * The driver then does what the game's main does for sound, without its graphics. The profile in `driver_params`
 * gives the addresses. The ripper fills in the profile. The steps:
 *   1. the video interface: a jingle's thread waits for its retraces;
 *   2. the OS's arena, below the driver, and the OS heap in it;
 *   3. the ARAM and its queue;
 *   4. Camelot's heaps 1 and 7 (MusyX allocates from heap 7), as the game's system set-up makes them;
 *   5. the game's sound set-up: MusyX, its output mode and volumes;
 *   6. Camelot's heap 0, then the game's sound banks: two MusyX groups from the disc;
 *   7. the song, by its number in the game's song table, or the jingle, by its number in the jingle table.
 * The driver's thread then sleeps for good, and the song plays from the audio and DSP interrupts. The OS's idle loop
 * runs while no thread can. `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py mario_golf` (clang). The result is committed as src/rip/driver_mario_golf_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

typedef unsigned int u32;

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

/* Values of Params.status. They're macros because an enumerator can't hold 0x80000000 before C23. */
#define STATUS_BOOTING 0u
#define STATUS_PLAYING 1u
#define STATUS_FINISHED 2u
#define STATUS_ERROR 0x80000000u /* | step number */

/* A sound ID's table: its section byte, as in docs/gcsf.md. Its low half is the number in that table. */
#define SECTION_SONGS 0u
#define SECTION_JINGLES 1u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the song's table and number (see SECTION_SONGS); patched by each .minigcsf */
    u32 sound;    /* unused: the game keeps the song's handle itself */
    /* the OS's arena, and the sizes of Camelot's heaps 1, 7 and 0 */
    u32 arena_lo;
    u32 arena_hi;
    u32 heap1_size;
    u32 heap7_size;
    u32 heap0_size;
    u32 song_heap; /* the heap that the game loads a song into */
    /* the OS's functions and variables */
    u32 os_get_arena_lo;     /* void* OSGetArenaLo(void) */
    u32 os_get_arena_hi;     /* void* OSGetArenaHi(void) */
    u32 os_set_arena_lo;     /* void OSSetArenaLo(void*) */
    u32 os_set_arena_hi;     /* void OSSetArenaHi(void*) */
    u32 os_init_alloc;       /* void* OSInitAlloc(void* lo, void* hi, int max_heaps) */
    u32 os_create_heap;      /* int OSCreateHeap(void* lo, void* hi) */
    u32 os_set_current_heap; /* int OSSetCurrentHeap(int) */
    u32 os_alloc_from_heap;  /* void* OSAllocFromHeap(int heap, u32 size) */
    u32 os_current_heap;     /* the variable that holds the current heap */
    u32 os_sleep_thread;     /* void OSSleepThread(OSThreadQueue*) */
    u32 vi_init;             /* void VIInit(void) */
    u32 ar_init;             /* u32 ARInit(u32* stack, u32 count) */
    u32 ar_stack;            /* the game's ARAM allocation stack */
    u32 arq_init;            /* void ARQInit(void) */
    /* the game's functions */
    u32 context_init; /* the game's set-up of its main context, called with context_object */
    u32 context_object;
    u32 heaps_init;      /* Camelot's heap table */
    u32 create_heap;     /* Camelot's heap `index` from `start` to `end` */
    u32 select_heap;     /* selects Camelot's heap `index`, and returns the heap it replaces */
    u32 before_sound[4]; /* the file loader's set-up, and what the system set-up calls before the sound set-up */
    u32 sound_init;      /* the game's sound set-up */
    u32 load_banks;      /* the game's sound banks: two MusyX groups */
    u32 play_song;       /* playSong(number) */
    u32 play_jingle;     /* playJingle(number) */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

typedef u32 (*fn0)(void);
typedef u32 (*fn1)(u32);
typedef u32 (*fn2)(u32, u32);
typedef u32 (*fn3)(u32, u32, u32);

/* An OSThreadQueue (head and tail) that nothing wakes. */
static u32 never_woken[2];

static u32 word(u32 address) {
    return *(volatile u32*)address;
}

/* The driver's thread sleeps for good. The OS then runs its idle loop, which the player's CPU skips to the next
 * interrupt. */
__attribute__((noreturn)) static void sleep_forever(void) {
    for (;;)
        ((fn1)driver_params.os_sleep_thread)((u32)never_woken);
}

__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    sleep_forever();
}

/* Camelot's heap `index`, `size` bytes from the OS heap. */
static void make_heap(u32 index, u32 size) {
    volatile struct Params* p = &driver_params;
    const u32 start = ((fn2)p->os_alloc_from_heap)(word(p->os_current_heap), size);
    if (!start)
        fail(10 + index);
    ((fn3)p->create_heap)(index, start, start + size);
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    const u32 section = p->sound_id >> 24;
    const u32 number = p->sound_id & 0xffff;
    if (section != SECTION_SONGS && section != SECTION_JINGLES)
        fail(2);

    /* The video interface, then the arena and the OS heap over all of it. */
    ((fn0)p->vi_init)();
    ((fn1)p->os_set_arena_lo)(p->arena_lo);
    ((fn1)p->os_set_arena_hi)(p->arena_hi);
    ((fn1)p->context_init)(p->context_object);
    ((fn0)p->heaps_init)();
    u32 lo = ((fn3)p->os_init_alloc)(((fn0)p->os_get_arena_lo)(), ((fn0)p->os_get_arena_hi)(), 1);
    ((fn1)p->os_set_arena_lo)(lo);
    lo = (lo + 31) & ~31u;
    const u32 hi = ((fn0)p->os_get_arena_hi)() & ~31u;
    ((fn1)p->os_set_current_heap)(((fn2)p->os_create_heap)(lo, hi));
    ((fn1)p->os_set_arena_lo)(hi);

    /* The ARAM. */
    ((fn2)p->ar_init)(p->ar_stack, 2);
    ((fn0)p->arq_init)();

    /* Camelot's heaps 1 and 7, the sound set-up, then heap 0. */
    make_heap(1, p->heap1_size);
    make_heap(7, p->heap7_size);
    ((fn1)p->select_heap)(0);
    for (u32 i = 0; i < 4; i++)
        if (p->before_sound[i])
            ((fn0)p->before_sound[i])();
    ((fn0)p->sound_init)();
    make_heap(0, p->heap0_size);

    /* The banks, then the song or the jingle. */
    ((fn0)p->load_banks)();
    const u32 previous = ((fn1)p->select_heap)(p->song_heap);
    ((fn1)(section == SECTION_SONGS ? p->play_song : p->play_jingle))(number);
    ((fn1)p->select_heap)(previous);
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
