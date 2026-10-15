// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for the menu of The Legend of Zelda: Collector's Edition. The disc's sound library is JAudio2, the
 * sound library of Nintendo EAD's JSystem. The build is close to the one in Mario Kart: Double Dash!!. The driver is
 * compiled for Gekko and stored in the disc's .gcsflib. The player boots the disc's startup code: the OS
 * initialisation and the static constructors. The rip patches the disc's main to branch here.
 *
 * The menu streams its music. Its sequenced sounds are effects. The driver calls the disc's functions to do what the
 * menu's start-up does for sound. The driver leaves out the graphics. The profile in `driver_params` gives the
 * functions' addresses. The ripper fills in the profile. The steps:
 *   1. the video interface: its retraces pace the driver as they pace the menu's frames;
 *   2. what JSystem's framework sets up first: OSInit, DVDInit, the root heap and the system heap. Then the ARAM
 *      manager and the main thread's JKRThread;
 *   3. the audio heap, a solid heap in the root heap;
 *   4. the sound files, in the order that the disc's loader thread reads them: the sequence archive in memory, then
 *      the start-up archive in the system heap;
 *   5. the audio set-up. The set-up reads the start-up archive into a section of the sound library and starts to load
 *      the wave archives that the start-up archive marks. The set-up returns the sound manager. The manager's frame
 *      work runs once a frame until every wave archive is in ARAM;
 *   6. the sound: the sound manager starts it by its JAudio sound ID;
 *   7. the sound manager's frame work, once a frame, forever.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py collectors_edition` (clang). The result is committed as
 * src/rip/driver_collectors_edition_blob.h. Building GCSF itself requires no PowerPC toolchain.
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

/* The frames that the driver waits for the wave archives before it gives up. */
#define SETUP_FRAMES 600u

/* A wave archive's state once it is in ARAM. */
#define WAVES_LOADED 2u

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

typedef u32 (*fn0)(void);
typedef u32 (*fn1)(u32);
typedef u32 (*fn2)(u32, u32);
typedef u32 (*fn3)(u32, u32, u32);
typedef u32 (*fn4)(u32, u32, u32, u32);
typedef u32 (*fn5)(u32, u32, u32, u32, u32);

static u32 word(u32 address) {
    return *(volatile u32*)address;
}

static void wait_frame(void) {
    for (u32 i = 0; i < driver_params.frame_retraces; i++)
        ((fn0)driver_params.vi_wait_for_retrace)();
}

__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    for (;;)
        wait_frame();
}

/* A virtual function of `object`. The vtable is the object's first word. */
static u32 virtual_function(u32 object, u32 slot) {
    return word(word(object) + slot);
}

/* Whether every wave archive of the section's wave banks is in ARAM. A bank's vtable gives its archive at slot 0x10
   and the number of its archives at slot 0x14. An archive without a file on the disc (entry number below 0) is never
   loaded. */
static int waves_loaded(u32 section) {
    volatile struct Params* p = &driver_params;
    const u32 data = word(section + p->section_data_offset);
    const u32 banks = word(data + p->wave_banks_offset);
    const u32 count = word(data + p->wave_banks_offset + 4);
    for (u32 b = 0; b < count; b++) {
        const u32 bank = word(banks + b * 4);
        if (!bank)
            continue;
        const u32 arcs = ((fn1)virtual_function(bank, 0x14))(bank);
        for (u32 i = 0; i < arcs; i++) {
            const u32 arc = ((fn2)virtual_function(bank, 0x10))(bank, i);
            if (arc && (s32)word(arc + p->wave_arc_state_offset + 4) >= 0 &&
                word(arc + p->wave_arc_state_offset) != WAVES_LOADED)
                return 0;
        }
    }
    return 1;
}

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
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
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
