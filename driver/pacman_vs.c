// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Pac-Man Vs.'s sound layer: a thin wrapper in the game's code around a JAIBasic. The sound
 * library is JAudio1, the first generation of the sound library of Nintendo EAD's JSystem. The driver is compiled for
 * Gekko and stored in the game's .gcsflib. The player boots the game's startup code: the OS initialisation and the
 * static constructors. The rip patches the game's main to branch here.
 *
 * The game streams its music. Its sequenced sounds are effects. They play in the tracks of the effects sequence. The
 * sound library starts that sequence when the library starts. The driver calls the game's functions to do what the
 * game's start-up does for sound. The driver leaves out the graphics. The profile in `driver_params` gives the
 * functions' addresses. The ripper fills in the profile. The steps:
 *   1. the game's heaps: the root heap and the system heap. The game's main makes them first;
 *   2. the video interface: its retraces pace the driver as they pace the game's frames;
 *   3. the game's task thread. The game runs its steps there. The driver runs the steps that follow there too. The
 *      game's reader of files stops on a thread whose priority number is below 17. The main thread runs at 16. The
 *      main thread then waits for the retraces for good;
 *   4. the disc, the ARAM manager and the sound library's track callback. The game's boot step sets up the same three;
 *   5. the sound files: the game's audio heap, the start-up archive in the system heap and the sequence archive in
 *      ARAM. Then the ARAM transfer buffer;
 *   6. the sound library. The game's wrapper starts it. The wrapper's frame work then runs once a frame until every
 *      wave system is in ARAM and the effects sequence plays;
 *   7. the sound: the game's wrapper starts it by its JAudio sound ID. The game starts its effects without a position;
 *   8. the wrapper's frame work, once a frame, forever.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py pacman_vs` (clang). The result is committed as src/rip/driver_pacman_vs_blob.h. Building
 * GCSF itself requires no PowerPC toolchain.
 */

typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;

#define PARAMS_MAGIC 0x47435344u /* 'GCSD' */
#define PARAMS_VERSION 1u

/* Values of Params.status. They're macros because an enumerator can't hold 0x80000000 before C23. */
#define STATUS_BOOTING 0u
#define STATUS_PLAYING 1u
#define STATUS_FINISHED 2u
#define STATUS_ERROR 0x80000000u /* | step number */

/* The frames that the driver waits for the waves and the effects sequence before it gives up. */
#define SETUP_FRAMES 600u

/* How the game mounts the sequence archive: JKRArchive's mount mode for ARAM, and its direction from the heap's
   head. */
#define MOUNT_ARAM 2u
#define MOUNT_HEAD 1u

/* A JAISound's state once its sequence plays. */
#define SOUND_PLAYING 4u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* written by the sound library: the sound's JAISound while it plays, then 0 */
    /* the game profile: sizes, counts and offsets */
    u32 frame_retraces;    /* video retraces between the wrapper's frames */
    u32 aram_audio_size;   /* ARAM for the sound library */
    u32 aram_graph_size;   /* ARAM for graphics */
    u32 aram_priorities;   /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 aram_buffer_size;  /* the ARAM transfer buffer */
    u32 wave_systems;      /* the start-up archive's wave systems. The sound library loads them all when it starts */
    u32 sound_state_offset; /* a JAISound's state (a byte) */
    /* the game profile: the addresses of the game's functions and variables */
    u32 create_heaps;        /* the game's function that makes the root heap and a system heap in all of it */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 dvd_init;            /* void DVDInit(void) */
    u32 create_task_thread;  /* the game's function that creates and starts its task thread */
    u32 send_task;           /* the game's sender of a function to the task thread: void send(void (*)(void)) */
    u32 alloc;               /* void* JKRHeap::alloc(u32 size, int alignment, JKRHeap*): from the current heap for 0 */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 set_aram_buffer;     /* void JKRAramStream::setTransBuffer(u8*, u32 size, JKRHeap*) */
    u32 mount_archive;       /* JKRArchive* JKRArchive::mount(const char* path, mode, JKRHeap*, direction) */
    u32 track_callback;      /* the sound library's set-up of its track callback. The game's boot step calls it */
    u32 create_audio_heap;   /* the game's function that makes its audio heap: a solid heap in the system heap */
    u32 audio_heap;          /* the variable that holds the game's audio heap */
    u32 set_audio_folder;    /* the sound library's setter of the folder of its sound files */
    u32 audio_folder;        /* the game's string: that folder */
    u32 load_file;           /* the game's reader of a whole file of the disc: void* load(const char* path, void*) */
    u32 startup_archive;     /* the game's string: the start-up archive's path */
    u32 sequence_archive;    /* the game's string: the sequence archive's path */
    u32 set_startup_archive; /* the sound library's setter of its start-up archive */
    u32 set_sequence_archive; /* the sound library's setter of its sequence archive */
    u32 sound_init;          /* the game's wrapper: starts JAIBasic (initDriver and initInterface) */
    u32 sound_frame;         /* the game's wrapper: the sound library's frame work */
    u32 play_sound;          /* the game's wrapper: void play(u32 id, JAISound** handle) */
    u32 waves_loaded;        /* int: 1 once the wave system's group is in ARAM (wave system, group) */
    u32 effects_sequence;    /* the variable that holds the effects sequence's JAISound */
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

static u8 byte(u32 address) {
    return *(volatile u8*)address;
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

/* Whether the sound library has loaded every wave system. Each has one group. The game waits for the first. */
static int waves_loaded(void) {
    volatile struct Params* p = &driver_params;
    for (u32 system = 0; system < p->wave_systems; system++) {
        if ((((fn2)p->waves_loaded)(system, 0) & 0xff) != 1)
            return 0;
    }
    return 1;
}

/* Whether the effects sequence plays. The sound library starts no effect before it does. */
static int effects_play(void) {
    volatile struct Params* p = &driver_params;
    const u32 effects = word(p->effects_sequence);
    return effects && byte(effects + p->sound_state_offset) >= SOUND_PLAYING;
}

/* The steps from the boot step on, in the game's task thread. */
__attribute__((noreturn)) static void driver_task(void) {
    volatile struct Params* p = &driver_params;

    /* The game's boot step: the disc, the ARAM manager and the track callback. */
    ((fn0)p->dvd_init)();
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
    ((fn0)p->track_callback)();

    /* The sound files. The start-up archive goes into the current heap. The sequence archive goes into ARAM. Its
       directory goes into the audio heap. The game then gives the ARAM's transfers a buffer. */
    ((fn0)p->create_audio_heap)();
    const u32 audio_heap = word(p->audio_heap);
    if (!audio_heap)
        fail(5);
    ((fn1)p->set_audio_folder)(p->audio_folder);
    const u32 startup = ((fn2)p->load_file)(p->startup_archive, 0);
    if (!startup)
        fail(5);
    const u32 sequences = ((fn4)p->mount_archive)(p->sequence_archive, MOUNT_ARAM, audio_heap, MOUNT_HEAD);
    if (!sequences)
        fail(5);
    const u32 buffer = ((fn3)p->alloc)(p->aram_buffer_size, 0x20, 0);
    if (!buffer)
        fail(5);
    ((fn3)p->set_aram_buffer)(buffer, p->aram_buffer_size, 0);

    /* The sound library, then its frame work until the waves are in ARAM and the effects sequence plays. */
    ((fn1)p->set_startup_archive)(startup);
    ((fn1)p->set_sequence_archive)(sequences);
    ((fn0)p->sound_init)();
    for (u32 frames = 0; !waves_loaded() || !effects_play(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn0)p->sound_frame)();
        wait_frame();
    }

    /* The sound. The game starts its effects without a position. */
    ((fn2)p->play_sound)(p->sound_id, (u32)&p->sound);
    if (!p->sound)
        fail(7);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn0)p->sound_frame)();
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The game's heaps, then the video interface, for the retraces. */
    ((fn0)p->create_heaps)();
    ((fn0)p->vi_init)();

    /* The game's task thread runs the other steps. */
    ((fn0)p->create_task_thread)();
    ((fn1)p->send_task)((u32)driver_task);
    for (;;)
        ((fn0)p->vi_wait_for_retrace)();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
