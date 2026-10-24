// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Mario Golf: Toadstool Tour and Mario Power Tennis. Their sound library is MusyX, under a layer
 * of Camelot code. The driver is compiled for Gekko and stored in the game's .gcsflib. The player boots the game's
 * start-up code: the OS initialisation and the static constructors. The rip patches the game's main to branch here.
 *
 * The driver then does what the game's main does for sound, without its graphics. The profile in `driver_params`
 * gives the addresses. The ripper fills in the profile. The steps:
 *   - the video interface: a jingle's thread waits for its retraces;
 *   - the OS's arena, below the driver, and the OS heap in it;
 *   - the ARAM and its queue;
 *   - Camelot's heaps 1 and 7 (MusyX allocates from heap 7), as the game's system set-up makes them;
 *   - the game's sound set-up: MusyX, its output mode and volumes. Mario Power Tennis's set-up fades MusyX's masters
 *     over 200 ms. The game plays no music before the fade has ended. The driver waits `set_up_retraces` retraces;
 *   - Camelot's heap 0, then the game's sound banks: two MusyX groups from the disc;
 *   - the song, by its number in the game's song table, or the jingle, by its number in the jingle table. An effect
 *     plays through the game's functions for effects instead. The sound ID gives what the game loads first: the
 *     effect's voice bank, its court's bank or its narrations. For an effect of a group that no table lists, the
 *     driver loads the group from the four files that the sound ID gives. It loads them as the game's bank loader
 *     loads its groups. While a voice bank loads, the driver limits MusyX's queue of uploads to ARAM to one upload
 *     (see load_voice_bank).
 * The driver's thread then sleeps for good, and the sound plays from the audio and DSP interrupts. The OS's idle loop
 * runs while no thread can. A step that fails stops the driver with its number in the status word: 1 for the
 * driver's check of its parameter block, 2 for its check of the sound ID, and 10 + n for Camelot's heap n.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py mario_golf` (clang). The result is committed as src/rip/driver_mario_golf_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* A sound ID's table: its section byte, as in docs/gcsf.md. Its low half is the number in that table. */
#define SECTION_SONGS 0u
#define SECTION_JINGLES 1u
/* An effect, by its ID in the low half. Bits 16 to 23 give the number of its voice bank plus 1, or 0 for an effect of
 * the banks that the game loads at start-up. */
#define SECTION_EFFECTS 0x80u
/* A narration, by its number in the game's table of narrations. */
#define SECTION_NARRATIONS 0x81u
/* An effect of a court's bank, by its ID in the low half. Bits 16 to 23 give the court's number. */
#define SECTION_COURT_EFFECTS 0x82u
/* An effect of a group that no table of the game lists, by its ID in bits 0 to 11. Bits 12 to 23 give half the number
 * of the group's first file. */
#define SECTION_GROUP_EFFECTS 0x83u

/* Camelot's heap that the game's bank loader selects for a group's files. */
#define BANK_HEAP 7u

/* The volume and the pan that make MusyX take both from the effect's row in the group's table of effects. */
#define EFFECT_DEFAULT 0xffu

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
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 ar_init;             /* u32 ARInit(u32* stack, u32 count) */
    u32 aram_blocks;         /* the ARAM allocator's block list (ARInit's stack) */
    u32 arq_init;            /* void ARQInit(void) */
    /* the game's functions */
    u32 context_init; /* the game's set-up of its main context, called with context_object */
    u32 context_object;
    u32 heaps_init;      /* sets up Camelot's heap table */
    u32 create_heap;     /* Camelot's heap `index` from `start` to `end` */
    u32 select_heap;     /* selects Camelot's heap `index`, and returns the heap it replaces */
    u32 before_sound[4]; /* the file loader's set-up, and what the system set-up calls before the sound set-up */
    u32 sound_init;      /* the game's sound set-up */
    u32 set_up_retraces; /* the retraces that the driver waits after the sound set-up */
    u32 load_banks;      /* the game's sound banks: two MusyX groups */
    u32 play_song;       /* playSong(number) */
    u32 play_jingle;     /* playJingle(number) */
    /* the game's functions for effects, 0 for one that the game lacks */
    u32 load_voice_bank; /* JVoiceLoad1(bank) */
    u32 play_voice;      /* voiceFXcall(bank, effect, volume) */
    u32 play_effect;     /* starts an effect of the start-up banks or of a court's bank: (effect, volume, pan) */
    u32 load_narrations; /* loads the group of the narrations that the game keeps */
    u32 play_narration;  /* (number, volume): loads the narration's group if the table gives files, then starts it */
    u32 load_court;      /* (court) */
    u32 load_file;       /* loads a file into the current heap: (file number, 0, 0) */
    u32 push_group;      /* pushes a group: (its samples' file number, project, group, sample directory, pool) */
    u32 upload_check;    /* MusyX's check of its queue of uploads to ARAM (cmplwi rN, 16), or 0 to leave it alone */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "sleep.h"

/* Writes the instruction `op` at `address`. The data cache writes the line back, and the instruction cache drops its
 * copy. The CPU then runs the new instruction. */
static void write_instruction(u32 address, u32 op) {
    *(volatile u32*)address = op;
    __asm__ volatile("dcbst 0, %0\n\tsync\n\ticbi 0, %0\n\tisync" : : "r"(address) : "memory");
}

/* Loads voice bank `bank` through the game's JVoiceLoad1. Mario Golf reads a bank's samples from the disc in chunks.
 * The chunks go into two buffers in turn. MusyX queues each chunk's upload to ARAM. The queue holds up to 16 uploads.
 * On the console, each upload ends before the drive has read the next chunk. GCSF's drive answers sooner than the ARAM
 * DMA moves a chunk. The uploads fall behind. A buffer then takes a new chunk before the last one has moved to ARAM.
 * While the bank loads, the driver therefore sets the limit in MusyX's check of the queue to 1. The driver puts back
 * the game's limit afterwards. */
static void load_voice_bank(u32 bank) {
    volatile struct Params* p = &driver_params;
    const u32 check = p->upload_check;
    const u32 original = check ? word(check) : 0;
    if (check)
        write_instruction(check, (original & 0xffff0000u) | 1);
    ((fn1)p->load_voice_bank)(bank);
    if (check)
        write_instruction(check, original);
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
    const u32 bank = p->sound_id >> 16 & 0xff;
    const u32 number = p->sound_id & 0xffff;
    if (section > SECTION_GROUP_EFFECTS || (section > SECTION_JINGLES && section < SECTION_EFFECTS))
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
    ((fn2)p->ar_init)(p->aram_blocks, 2);
    ((fn0)p->arq_init)();

    /* Camelot's heaps 1 and 7, the sound set-up, then heap 0. */
    make_heap(1, p->heap1_size);
    make_heap(7, p->heap7_size);
    ((fn1)p->select_heap)(0);
    for (u32 i = 0; i < 4; i++)
        if (p->before_sound[i])
            ((fn0)p->before_sound[i])();
    ((fn0)p->sound_init)();
    for (u32 i = 0; i < p->set_up_retraces; i++)
        ((fn0)p->vi_wait_for_retrace)();
    make_heap(0, p->heap0_size);

    /* The banks, then the song, the jingle or the effect. */
    ((fn0)p->load_banks)();
    if (section == SECTION_SONGS || section == SECTION_JINGLES) {
        const u32 previous = ((fn1)p->select_heap)(p->song_heap);
        ((fn1)(section == SECTION_SONGS ? p->play_song : p->play_jingle))(number);
        ((fn1)p->select_heap)(previous);
    } else if (section == SECTION_EFFECTS && bank != 0) {
        load_voice_bank(bank - 1);
        ((fn3)p->play_voice)(bank - 1, number, EFFECT_DEFAULT);
    } else if (section == SECTION_EFFECTS) {
        ((fn3)p->play_effect)(number, EFFECT_DEFAULT, EFFECT_DEFAULT);
    } else if (section == SECTION_NARRATIONS) {
        if (p->load_narrations)
            ((fn0)p->load_narrations)();
        ((fn2)p->play_narration)(number, EFFECT_DEFAULT);
    } else if (section == SECTION_COURT_EFFECTS) {
        ((fn1)p->load_court)(bank);
        ((fn3)p->play_effect)(number, EFFECT_DEFAULT, EFFECT_DEFAULT);
    } else {
        /* The pool, the project and the sample directory, then the group with its samples. The project's first group
         * header gives the group's ID at +4. */
        const u32 first = (p->sound_id >> 12 & 0xfff) * 2;
        const u32 previous = ((fn1)p->select_heap)(BANK_HEAP);
        const u32 pool = ((fn3)p->load_file)(first, 0, 0);
        const u32 project = ((fn3)p->load_file)(first + 2, 0, 0);
        const u32 sdir = ((fn3)p->load_file)(first + 4, 0, 0);
        ((fn5)p->push_group)(first + 6, project, be16((const u8*)project + 4), sdir, pool);
        ((fn1)p->select_heap)(previous);
        ((fn3)p->play_effect)(p->sound_id & 0xfff, EFFECT_DEFAULT, EFFECT_DEFAULT);
    }
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
