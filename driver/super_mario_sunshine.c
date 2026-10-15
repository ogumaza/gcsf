// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Super Mario Sunshine. The game's sound library is JAudio1, with a sound layer above it
 * (JAIBasic) and the game's sound object (MSound) above that. It's compiled for Gekko and stored in the game's
 * .gcsflib. The player boots the game's startup code: the OS initialisation and the static constructors. The rip
 * patches the game's main to branch here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The steps:
 *   1. the video interface: its retraces pace the driver as they pace the game's frames;
 *   2. the disc, with long file names as the disc's bi2.bin allows them, the root heap and the system heap, the ARAM
 *      manager and its transfer buffer. The game's start-up makes them the same way;
 *   3. the game's sound object. It sets the sound library up. The game takes the start-up archive from a compressed
 *      archive and hands it to the sound object. The driver hands it none. The sound library then reads the start-up
 *      archive from a file of the rip;
 *   4. the waves that the game loads at start-up and before its title: the game's sound object loads each;
 *   5. the game's sound frame, once a frame, until the sound library is up and those waves are in ARAM;
 *   6. the scene's waves: the group of the scene system that the .minigcsf names. The game's scene loader puts it at
 *      the top of ARAM. Groups of that system share some waves, with the same samples;
 *   7. the sound: the sound library starts it by its JAudio sound ID. The game starts its music the same way. A music
 *      piece plays at the volume that the game gives its music;
 *   8. the game's sound frame, once a frame, forever.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py super_mario_sunshine` (clang). The result is committed as
 * src/rip/driver_super_mario_sunshine_blob.h. Building GCSF itself requires no PowerPC toolchain.
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

/* The frames that the driver waits for the sound library and for the waves before it gives up. */
#define SETUP_FRAMES 600u

/* The most waves that a .minigcsf names. */
#define MAX_WAVES 64u

/* The most wave loads that the game's start-up makes. */
#define MAX_STAYING_WAVES 4u

/* The sound library's state once it's up and its effects sequence plays. */
#define STATE_RUNNING 4u

/* A wave system's load status in the sound library: 2 once its group is in ARAM. */
#define WAVES_LOADED 2

/* A wave ID of the game's sound object: (wave system << 8) | group. NO_WAVES names none. */
#define NO_WAVES 0xffffffffu

/* A sound ID's type, in its top two bits. */
#define SOUND_TYPE_MASK 0xc0000000u
#define SOUND_SEQUENCE 0x80000000u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* the sound's JAISound handle (written by the game): its JAISound while it plays, and 0 after */
    /* the waves that the sound uses, patched by each .minigcsf: (wave system << 16) | group */
    u32 wave_count;
    u32 waves[MAX_WAVES];
    /* the game profile: the sizes that the game's start-up gives */
    u32 frame_retraces;   /* video retraces between the game's frames */
    u32 max_heaps;        /* the root heap's maximum of standard heaps */
    u32 system_heap_size; /* the system heap, from the root heap */
    u32 aram_audio_size;  /* ARAM for the sound library */
    u32 aram_graph_size;  /* ARAM for graphics */
    u32 aram_priorities;  /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 aram_buffer_size; /* the ARAM transfer buffer, from the root heap */
    u32 sound_object_size;
    u32 staying_waves[MAX_STAYING_WAVES]; /* the game's start-up loads, as the sound object's wave IDs */
    u32 scene_system;                     /* the wave system whose groups the scenes load */
    u32 music_volume;                     /* a float's bits: the volume of the game's music */
    /* the game profile: the addresses of the game's functions and variables */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 dvd_init;            /* void DVDInit(void) */
    u32 long_file_names;     /* the OS's flag that allows file names longer than 8.3 */
    u32 create_root_heap;    /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;     /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 become_system_heap;  /* JKRHeap* JKRHeap::becomeSystemHeap() */
    u32 become_current_heap; /* JKRHeap* JKRHeap::becomeCurrentHeap() */
    u32 heap_alloc;          /* void* JKRHeap::alloc(u32 size, int alignment, JKRHeap*) */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 set_aram_buffer;     /* void JKRAramStream::setTransBuffer(u8*, u32 size, JKRHeap*) */
    u32 alloc;               /* void* operator new(u32 size) from the current heap */
    u32 sound_object_ctor;   /* the sound object's constructor: (heap, 0, ARAM size, start-up archive, 0, 0) */
    u32 sound_object;        /* the variable that holds the game's sound object */
    u32 set_output_mode;     /* the sound library's output: 1 for stereo */
    u32 load_waves;          /* the sound object's wave load, by its wave ID */
    u32 change_scene_waves;  /* JAIBasic's change of a wave system's group: (wave system, group) */
    u32 audio_frame;         /* the game's sound frame: the sound object's frame work */
    u32 sound_library;       /* the variable that holds the sound library (a JAIBasic) */
    u32 state_offset;        /* the sound library's state block, whose byte +1 is the state */
    u32 wave_status_offset;  /* the sound library's array of each wave system's load status */
    u32 start_sound;         /* void JAIBasic::startSoundVec(u32 id, JAISound**, Vec*, u32, u8) */
    u32 set_volume;          /* void JAISound::setVolume(float, u32 move time, u8 kind) */
    u32 music_volume_kind;
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

typedef u32 (*fn0)(void);
typedef u32 (*fn1)(u32);
typedef u32 (*fn2)(u32, u32);
typedef u32 (*fn3)(u32, u32, u32);
typedef u32 (*fn5)(u32, u32, u32, u32, u32);
typedef u32 (*fn6)(u32, u32, u32, u32, u32, u32);
typedef u32 (*fn7)(u32, u32, u32, u32, u32, u32, u32);
typedef void (*set_volume_fn)(u32, float, u32, u32);

static u32 word(u32 address) {
    return *(volatile u32*)address;
}

static unsigned char byte(u32 address) {
    return *(volatile unsigned char*)address;
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

/* The load status of a wave system in the sound library. */
static s32 wave_status(u32 system) {
    volatile struct Params* p = &driver_params;
    const u32 library = word(p->sound_library);
    const u32 status = library ? word(library + p->wave_status_offset) : 0;
    return status ? (s32)word(status + system * 4) : 0;
}

/* Whether the sound library is up, and the start-up's wave systems are in ARAM. A start-up load of one group of the
   scene system reports no status. The sound library loads in the order of the requests, and a later request's
   system is in ARAM after it. */
static int start_up_ready(void) {
    volatile struct Params* p = &driver_params;
    const u32 library = word(p->sound_library);
    const u32 state = library ? word(library + p->state_offset) : 0;
    if (!state || byte(state + 1) < STATE_RUNNING)
        return 0;
    for (u32 i = 0; i < MAX_STAYING_WAVES; i++) {
        const u32 id = p->staying_waves[i];
        if (id != NO_WAVES && (id >> 8) != p->scene_system && wave_status(id >> 8) != WAVES_LOADED)
            return 0;
    }
    return 1;
}

/* The game's sound frame, once a frame, until `ready` holds. Fails at `step` if it takes too long. */
static void run_until(int (*ready)(void), u32 step) {
    volatile struct Params* p = &driver_params;
    for (u32 frames = 0; !ready(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(step);
        ((fn1)p->audio_frame)(word(p->sound_object));
        wait_frame();
    }
}

static int scene_ready(void) {
    return wave_status(driver_params.scene_system) == WAVES_LOADED;
}

/* Whether the start-up loads the waves with the sound object's wave ID `id`. Those stay loaded. */
static int staying(u32 id) {
    for (u32 i = 0; i < MAX_STAYING_WAVES; i++) {
        if (driver_params.staying_waves[i] == id)
            return 1;
    }
    return 0;
}

/* The scene's group, as the sound object's wave ID: the first group of the scene system that the .minigcsf names and
   the start-up doesn't load. NO_WAVES if there's none to load. */
static u32 scene_waves(void) {
    volatile struct Params* p = &driver_params;
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        const u32 id = p->scene_system << 8 | (p->waves[i] & 0xff);
        if ((p->waves[i] >> 16) == p->scene_system && !staying(id))
            return id;
    }
    return NO_WAVES;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces, and the disc. The game's OS reads the flag for long file names from the
       disc's bi2.bin. The apploader leaves that file in memory. GCSF's machine leaves none. The game's disc sets the
       flag, and its wave archives have long names. */
    ((fn0)p->vi_init)();
    *(volatile u32*)p->long_file_names = 1;
    ((fn0)p->dvd_init)();

    /* The game's start-up: the root heap, a small system heap from it, the root heap as the current heap, the ARAM
       manager, and the ARAM transfer buffer from the root heap. */
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    if (!root)
        fail(2);
    const u32 system = ((fn3)p->create_exp_heap)(p->system_heap_size, root, 0);
    if (!system)
        fail(2);
    ((fn1)p->become_system_heap)(system);
    ((fn1)p->become_current_heap)(root);
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
    const u32 buffer = ((fn3)p->heap_alloc)(p->aram_buffer_size, 0x20, root);
    if (!buffer)
        fail(2);
    ((fn3)p->set_aram_buffer)(buffer, p->aram_buffer_size, 0);

    /* The game's sound object, with the root heap for the sound library and no start-up archive: the sound library
       reads it from the disc. Then stereo output, and the waves of the game's start-up. */
    const u32 object = ((fn1)p->alloc)(p->sound_object_size);
    if (!object)
        fail(3);
    ((fn7)p->sound_object_ctor)(object, root, 0, p->aram_audio_size, 0, 0, 0);
    *(volatile u32*)p->sound_object = object;
    ((fn1)p->set_output_mode)(1);
    for (u32 i = 0; i < MAX_STAYING_WAVES; i++) {
        if (p->staying_waves[i] != NO_WAVES)
            ((fn2)p->load_waves)(object, p->staying_waves[i]);
    }

    run_until(start_up_ready, 4);

    /* The scene's waves. */
    const u32 scene = scene_waves();
    if (scene != NO_WAVES) {
        ((fn3)p->change_scene_waves)(word(p->sound_library), scene >> 8, scene & 0xff);
        run_until(scene_ready, 5);
    }

    /* The sound, from no position. The game starts its music the same way. */
    ((fn6)p->start_sound)(word(p->sound_library), p->sound_id, (u32)&p->sound, 0, 0, 4);
    if (!p->sound)
        fail(6);
    if ((p->sound_id & SOUND_TYPE_MASK) == SOUND_SEQUENCE && p->set_volume) {
        union {
            u32 bits;
            float value;
        } volume = {p->music_volume};
        ((set_volume_fn)p->set_volume)(p->sound, volume.value, 0, p->music_volume_kind);
    }
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn1)p->audio_frame)(object);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
