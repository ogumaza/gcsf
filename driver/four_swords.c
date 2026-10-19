// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for The Legend of Zelda: Four Swords Adventures (USA) and Four Swords+ (Japan). Their sound library
 * is JAudio2, the sound library of Nintendo EAD's JSystem. The driver is compiled for Gekko and stored in the game's
 * .gcsflib. The player boots the game's start-up code: the OS initialisation and the static constructors. The rip
 * patches the game's main to branch here.
 *
 * The driver then calls the game's functions to do what the game's main does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The driver starts the video
 * interface first. Its retraces pace the driver as they pace the game's frames. The steps that follow:
 *   2. the heaps and the ARAM manager that JSystem's framework sets up;
 *   3. the game's audio heap and game heap;
 *   4. the game's audio object, then the game's disc thread. The driver puts the two heaps and the object where the
 *      game keeps them;
 *   5. the game's audio set-up: the driver calls it once a frame until it has loaded the start-up archive and the
 *      sequence archive and initialised the sound library;
 *   6. every wave bank of the sound library's section loaded into ARAM;
 *   7. the sound: the sound manager starts it by its JAudio sound ID. For the piece that the profile names, the driver
 *      then mutes the tracks that the game's music start mutes.
 * Then the game's audio frame process runs once a frame, forever. A step that fails stops the driver with its number in
 * the status word (step 1 is the driver's check of its parameter block). `driver_params.status` reports the driver's
 * progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py four_swords` (clang). The result is committed as src/rip/driver_four_swords_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The children of a track. */
#define TRACK_CHILDREN 16u

/* The size of a JKRThread in the game's build of JSystem, for the main thread's. */
#define THREAD_SIZE 0x7cu

struct Params {
    u32 magic;
    u32 version;
    u32 status;         /* written by the driver */
    u32 sound_id;       /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;          /* the sound's JAISoundHandle (written by the game): its JAISound while it plays, and 0 after */
    u32 frame_retraces; /* video retraces between the audio frame process's calls */
    /* the sizes that the game's start-up gives */
    u32 max_heaps;        /* the root heap's maximum of standard heaps */
    u32 system_heap_size;
    u32 aram_audio_size;  /* ARAM for the sound library */
    u32 aram_graph_size;  /* ARAM for graphics */
    u32 aram_priorities;  /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 audio_heap_size;
    u32 disc_heap_size;   /* the heap that the disc thread allocates from */
    u32 audio_object_size;
    /* the game profile: the addresses of the game's functions and variables */
    u32 vi_init;              /* void VIInit(void) */
    u32 vi_wait_for_retrace;  /* void VIWaitForRetrace(void) */
    u32 get_current_thread;   /* OSThread* OSGetCurrentThread(void) */
    u32 get_thread_priority;  /* s32 OSGetThreadPriority(OSThread*) */
    u32 create_root_heap;     /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;      /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_solid_heap;    /* JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_aram;          /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 alloc;                /* void* operator new(u32 size) from the current heap */
    u32 alloc_from;           /* void* operator new(u32 size, JKRHeap*, int alignment) */
    u32 thread_ctor;          /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 become_current_heap;  /* JKRHeap* JKRHeap::becomeCurrentHeap() */
    u32 system_heap;          /* the variable that holds the heap that the game's heaps come from */
    u32 game_globals;         /* the game's variables: the audio heap, game heap and audio object at +4, +8, +12 */
    u32 audio_object_ctor;    /* the game's audio object's constructor */
    u32 create_disc_thread;   /* the game's disc thread: create(s32 priority, JKRHeap*, int) */
    u32 audio_setup;          /* the game's audio set-up, called with an object whose byte at +4 it sets when done */
    u32 audio_frame;          /* the game's audio frame process, called with the audio object */
    u32 sound_manager_offset; /* the sound manager (a JAISoundStarter) in the audio object */
    u32 section_offset;       /* the sound library's section in the audio object */
    u32 load_wave_bank;       /* bool JAUSection::loadWaveArc(u32 bank): every archive of a wave bank */
    u32 section_data_offset;  /* the section heap's data in the section */
    u32 wave_banks_offset;    /* the data's table of wave banks, and its count after it */
    u32 wave_arc_state_offset; /* a wave archive's state (2 once it is in ARAM), and its disc entry after it */
    /* the game profile: the piece whose tracks the game's music start mutes */
    u32 track_mute;     /* void JASTrack::mute(bool) */
    u32 muted_sound;    /* the piece's JAudio sound ID, or 0 for none */
    u32 muted_children; /* the children of the piece's track 0 that its start mutes, a bit each from bit 0 */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

/* The game's audio set-up sets the byte at +4 of this when it's done. */
static volatile u32 setup_state[2];

#include "aram_manager.h"
#include "frames.h"
#include "section_waves.h"

/* Mutes the children of the sound's track 0 that the profile names and lets the others play. The game's music start
   does the same. The sound library has opened the tracks by the time startSound returns. A sound's vtable gives its
   JAISeq at slot 0x18. A JAISeq's vtable gives getChildTrack(int) at slot 0x24: child n >> 4 of the root track, then
   that track's child n & 15. The game turns external interrupts off around the loop. No tick of the sound library
   comes between the calls. */
static void mute_children(u32 sound) {
    volatile struct Params* p = &driver_params;
    const u32 sequence = ((fn1)virtual_function(sound, 0x18))(sound);
    if (!sequence)
        return;
    const u32 msr = disable_interrupts();
    for (u32 i = 0; i < TRACK_CHILDREN; i++) {
        const u32 track = ((fn2)virtual_function(sequence, 0x24))(sequence, i);
        if (track)
            ((fn2)p->track_mute)(track, (p->muted_children >> i) & 1);
    }
    restore_interrupts(msr);
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* JSystem: the root heap, the system heap, the ARAM manager and the main thread's JKRThread. */
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    if (!root || !((fn3)p->create_exp_heap)(p->system_heap_size, root, 0))
        fail(2);
    create_aram_manager();
    const u32 thread = ((fn1)p->alloc)(THREAD_SIZE);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);

    /* The game's heaps, audio object and disc thread. The game's main sets them up in the same way. */
    const u32 parent = word(p->system_heap);
    const u32 audio_heap = ((fn3)p->create_solid_heap)(p->audio_heap_size, parent, 0);
    const u32 game_heap = ((fn3)p->create_exp_heap)((u32)-1, parent, 0);
    if (!audio_heap || !game_heap)
        fail(3);
    const u32 audio = ((fn3)p->alloc_from)(p->audio_object_size, game_heap, 0);
    if (!audio)
        fail(4);
    ((fn1)p->audio_object_ctor)(audio);
    *(volatile u32*)(p->game_globals + 4) = audio_heap;
    *(volatile u32*)(p->game_globals + 8) = game_heap;
    *(volatile u32*)(p->game_globals + 12) = audio;
    ((fn1)p->become_current_heap)(game_heap);
    const u32 disc_heap = ((fn3)p->create_exp_heap)(p->disc_heap_size, game_heap, 0);
    const u32 priority = ((fn1)p->get_thread_priority)(((fn0)p->get_current_thread)());
    ((fn3)p->create_disc_thread)(priority - 1, disc_heap, 0);

    /* The game's audio set-up, once a frame until it's done. */
    u32 frames = 0;
    while (!((volatile unsigned char*)setup_state)[4]) {
        ((fn1)p->audio_setup)((u32)setup_state);
        if (++frames > SETUP_FRAMES)
            fail(5);
        wait_frame();
    }

    /* Every wave bank into ARAM. The audio frame process runs while the archives load. */
    const u32 section = word(audio + p->section_offset);
    const u32 data = word(section + p->section_data_offset);
    const u32 count = word(data + p->wave_banks_offset + 4);
    for (u32 b = 0; b < count; b++)
        ((fn2)p->load_wave_bank)(section, b);
    for (frames = 0; !waves_loaded(section); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn1)p->audio_frame)(audio);
        wait_frame();
    }

    /* The sound. The sound manager's vtable gives startSound(JAISoundID*, JAISoundHandle*, const TVec3*) at 0xc. */
    const u32 manager = word(audio + p->sound_manager_offset);
    const u32 id = p->sound_id;
    if (!(((fn4)virtual_function(manager, 0xc))(manager, (u32)&id, (u32)&p->sound, 0) & 0xff))
        fail(7);
    if (p->muted_sound && id == p->muted_sound && p->sound)
        mute_children(p->sound);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn1)p->audio_frame)(audio);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
