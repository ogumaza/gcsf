// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for the menu of The Legend of Zelda: Collector's Edition. The disc's sound library is JAudio2, the
 * sound library of Nintendo EAD's JSystem. The build is close to the one in Mario Kart: Double Dash!!. The driver is
 * compiled for Gekko and stored in the disc's .gcsflib. The player boots the disc's start-up code: the OS
 * initialisation and the static constructors. The rip patches the disc's main to branch here.
 *
 * The menu streams its music. Its sequenced sounds are effects. The driver calls the disc's functions to do what the
 * menu's start-up does for sound. The driver leaves out the graphics. The profile in `driver_params` gives the
 * functions' addresses. The ripper fills in the profile. The driver starts the video interface first. Its retraces
 * pace the driver as they pace the menu's frames. The steps that follow:
 *   2. what JSystem's framework sets up first: OSInit, DVDInit, the root heap and the system heap. Then the ARAM
 *      manager and the main thread's JKRThread;
 *   3. the audio heap, a solid heap in the root heap;
 *   4. the sound files, in the order that the disc's loader thread reads them: the sequence archive in memory, then
 *      the start-up archive in the system heap;
 *   5. the audio set-up. The set-up reads the start-up archive into a section of the sound library and starts to load
 *      the wave archives that the start-up archive marks. The set-up returns the sound manager;
 *   6. the wave archives: the manager's frame work runs once a frame until every wave archive is in ARAM;
 *   7. the sound: the sound manager starts it by its JAudio sound ID.
 * Then the sound manager's frame work runs once a frame, forever. A step that fails stops the driver with its number in
 * the status word (step 1 is the driver's check of its parameter block). `driver_params.status` reports the driver's
 * progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py collectors_edition` (clang). The result is committed as
 * src/rip/driver_collectors_edition_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* How the loader thread mounts the sequence archive: JKRArchive's mount mode for memory, and its direction from the
   heap's head. */
#define MOUNT_MEMORY 1u
#define MOUNT_HEAD 1u

/* The audio set-up's second argument, as the disc's main loop passes it. */
#define SETUP_FLAG 1u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* the sound's JAISoundHandle, written by the sound library: the JAISound while it plays, then 0 */
    /* the game profile: the sizes that the start-up gives */
    u32 frame_retraces;   /* video retraces between the frame work's calls */
    u32 max_heaps;        /* the root heap's maximum of standard heaps */
    u32 system_heap_size;
    u32 aram_audio_size;  /* ARAM for the sound library */
    u32 aram_graph_size;  /* ARAM for graphics */
    u32 aram_priorities;  /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 thread_size;      /* the size of a JKRThread, for the main thread */
    u32 audio_heap_size;
    /* the game profile: the addresses of the disc's functions and variables */
    u32 os_init;             /* void OSInit(void) */
    u32 dvd_init;            /* void DVDInit(void) */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 get_current_thread;  /* OSThread* OSGetCurrentThread(void) */
    u32 create_root_heap;    /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;     /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_solid_heap;   /* JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 alloc;               /* void* operator new(u32 size) from the current heap */
    u32 thread_ctor;         /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 mount_archive;       /* JKRArchive* JKRArchive::mount(const char* path, mode, JKRHeap*, direction) */
    u32 load_file;           /* the loader's reader of a whole file: void* load(const char* path, JKRHeap*) */
    u32 sequence_archive_path; /* the disc's path of its sequence archive */
    u32 sound_archive_path;  /* the disc's path of its start-up archive */
    u32 audio_setup;         /* the audio set-up: manager* setup(JKRHeap* audio, flag, void* baa, JKRArchive* seqs) */
    u32 audio_frame;         /* the sound manager's frame work: (manager) */
    u32 section_offset;      /* the sound library's section heap in the sound manager */
    u32 section_data_offset; /* the section's data */
    u32 wave_banks_offset;   /* the data's table of wave banks. The number of banks follows it */
    u32 wave_arc_state_offset; /* a wave archive's state (2 once it is in ARAM). The archive's disc entry follows it */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "aram_manager.h"
#include "frames.h"
#include "section_waves.h"

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* What JSystem's framework sets up first. The new heap becomes JKRHeap's system heap and the current heap. Then
       the ARAM manager and the main thread's JKRThread. */
    ((fn0)p->os_init)();
    ((fn0)p->dvd_init)();
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    const u32 system = root ? ((fn3)p->create_exp_heap)(p->system_heap_size, root, 0) : 0;
    if (!system)
        fail(2);
    create_aram_manager();
    const u32 thread = ((fn1)p->alloc)(p->thread_size);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);

    /* The audio heap, from the root heap. */
    const u32 audio_heap = ((fn3)p->create_solid_heap)(p->audio_heap_size, root, 0);
    if (!audio_heap)
        fail(3);

    /* The sound files. The sequence archive goes into the current heap. The start-up archive goes into the system
       heap. */
    const u32 sequences = ((fn4)p->mount_archive)(p->sequence_archive_path, MOUNT_MEMORY, 0, MOUNT_HEAD);
    if (!sequences)
        fail(4);
    const u32 baa = ((fn2)p->load_file)(p->sound_archive_path, system);
    if (!baa)
        fail(4);

    /* The audio set-up, then the frame work until the wave archives are in ARAM. */
    const u32 manager = ((fn4)p->audio_setup)(audio_heap, SETUP_FLAG, baa, sequences);
    if (!manager)
        fail(5);
    const u32 section = word(manager + p->section_offset);
    if (!section)
        fail(5);
    for (u32 frames = 0; !waves_loaded(section); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn1)p->audio_frame)(manager);
        wait_frame();
    }

    /* The sound. The sound manager's vtable gives startSound(JAISoundID*, JAISoundHandle*, const TVec3*) at 0xc. The
       menu starts its effects without a position. */
    const u32 id = p->sound_id;
    if (!(((fn4)virtual_function(manager, 0xc))(manager, (u32)&id, (u32)&p->sound, 0) & 0xff))
        fail(7);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn1)p->audio_frame)(manager);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
