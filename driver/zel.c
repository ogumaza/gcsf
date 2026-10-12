// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for The Wind Waker's sound layer (JAIZelBasic), on JAudio1, the first generation of the sound
 * library of Nintendo EAD's JSystem. It's compiled for Gekko and stored in the game's .gcsflib. The player boots the
 * game's startup code: the OS initialisation and the static constructors. The rip patches the game's main to branch
 * here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The steps:
 *   1. the video interface: its retraces pace the driver as they pace the game's frames;
 *   2. the heaps and the ARAM manager that the game's machine set-up makes, and the game's disc thread;
 *   3. the game's audio heap, then the game's audio process once a frame until the sound library is up: the process
 *      loads the start-up archive and the sequence archive, and starts the effects sequence;
 *   4. the waves that stay loaded: the sound library loads them by itself once it's up;
 *   5. the waves that the .minigcsf names, one group of a wave system each;
 *   6. the sound library's default camera, which stays still: the game's sound set-up clears the camera, and the
 *      game's camera process sets it once a frame;
 *   7. the sound: the sound library starts it by its JAudio sound ID;
 *   8. the game's audio process, once a frame, forever.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py zel` (clang). The result is committed as src/rip/driver_zel_blob.h. Building GCSF itself
 * requires no PowerPC toolchain.
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

/* A wave system's load status in the sound library: 2 once its group is in ARAM. */
#define WAVES_LOADED 2

/* The load types of a wave system in the start-up archive: 0 and 1 stay loaded, 2 is loaded for a scene. */
#define WAVES_FOR_SCENE 2u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* the sound's JAISound handle (written by the game): its JAISound while it plays, and 0 after */
    /* the waves that the sound uses, patched by each .minigcsf: (wave system << 16) | group */
    u32 wave_count;
    u32 waves[MAX_WAVES];
    /* the game profile: the sizes that the game's machine set-up gives */
    u32 frame_retraces;    /* video retraces between the audio process's calls */
    u32 max_heaps;         /* the root heap's maximum of standard heaps */
    u32 archive_heap_size; /* the game's archive, game and command heaps */
    u32 game_heap_size;
    u32 command_heap_size;
    u32 aram_audio_size; /* ARAM for the sound library */
    u32 aram_graph_size; /* ARAM for graphics */
    u32 aram_priorities; /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 aram_buffer_size; /* the ARAM transfer buffer */
    u32 audio_heap_size;
    u32 thread_size; /* a JKRThread, for the main thread's */
    /* the game profile: the addresses of the game's functions and variables */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 get_current_thread;  /* OSThread* OSGetCurrentThread(void) */
    u32 get_thread_priority; /* s32 OSGetThreadPriority(OSThread*) */
    u32 get_arena_lo;        /* void* OSGetArenaLo(void) */
    u32 get_arena_hi;        /* void* OSGetArenaHi(void) */
    u32 create_root_heap;    /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;     /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_solid_heap;   /* JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 get_free_size;       /* s32 JKRHeap::getFreeSize() */
    u32 become_current_heap; /* JKRHeap* JKRHeap::becomeCurrentHeap() */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 set_aram_buffer;     /* void JKRAramStream::setTransBuffer(u8*, u32 size, JKRHeap*) */
    u32 alloc;               /* void* operator new(u32 size) from the current heap */
    u32 thread_ctor;         /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 create_command_heap; /* the game's heaps: each creates its heap and keeps it in the game's variable */
    u32 create_archive_heap;
    u32 create_game_heap;
    u32 create_zelda_heap;
    u32 create_disc_thread; /* the game's disc thread: create(s32 priority) */
    u32 audio_heap;         /* the variable that holds the game's audio heap */
    u32 audio_process;      /* the game's audio process: it sets the sound library up, then runs it once a frame */
    u32 audio_ready;        /* the byte that the game's audio set-up sets when the sound library is up */
    u32 sound_library;      /* the variable that holds the sound library (a JAIBasic) */
    u32 start_sound;        /* void JAIBasic::startSoundVec(u32 id, JAISound**, Vec*, u32, u32, u8) */
    u32 wave_systems;       /* the variable that holds the start-up archive's wave systems: pointer, size, load type */
    u32 wave_status;        /* the variable that holds each wave system's load status */
    u32 load_wave_group;    /* void JAInter::BankWave::loadGroupWave(s32 wave_system, s32 group) */
    u32 effects_sequence;   /* the variable that holds the effects sequence's JAISound */
    u32 sound_state_offset; /* a JAISound's state: 4 (playing) or more once the sequence plays */
    u32 set_camera;         /* void JAIBasic::setCameraInfo(Vec* position, Vec* last_position, Mtx, u32 camera) */
    u32 default_camera;     /* the sound library's default camera: its position, last position and matrix */
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
typedef u32 (*fn7)(u32, u32, u32, u32, u32, u32, u32);

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

/* Whether the sound library has loaded every wave system that stays loaded. The start-up archive's list gives each
   wave system 12 bytes: its data, its size and its load type. A null pointer ends the list. */
static int staying_waves_loaded(void) {
    volatile struct Params* p = &driver_params;
    const u32 systems = word(p->wave_systems);
    const u32 status = word(p->wave_status);
    if (!systems || !status)
        return 0;
    for (u32 i = 0; word(systems + i * 12); i++) {
        if (word(systems + i * 12 + 8) != WAVES_FOR_SCENE && (s32)word(status + i * 4) != WAVES_LOADED)
            return 0;
    }
    return 1;
}

/* The number of wave systems in the start-up archive. */
static u32 wave_system_count(void) {
    const u32 systems = word(driver_params.wave_systems);
    u32 count = 0;
    while (systems && word(systems + count * 12))
        count++;
    return count;
}

/* Whether a wave system's group is in ARAM. */
static int wave_system_loaded(u32 system) {
    return (s32)word(word(driver_params.wave_status) + system * 4) == WAVES_LOADED;
}

/* Whether every wave that the .minigcsf names is in ARAM. */
static int named_waves_loaded(void) {
    volatile struct Params* p = &driver_params;
    const u32 count = wave_system_count();
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        if ((p->waves[i] >> 16) < count && !wave_system_loaded(p->waves[i] >> 16))
            return 0;
    }
    return 1;
}

/* Whether the effects sequence plays. The sound library's frame work waits for it. */
static int effects_play(void) {
    volatile struct Params* p = &driver_params;
    const u32 effects = word(p->effects_sequence);
    return effects && byte(effects + p->sound_state_offset) >= 4;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* The game's machine set-up: the root heap and the system heap from the arena that the game's other heaps leave,
       the ARAM manager and the main thread's JKRThread. Then the game's heaps, with the current heap in the last one,
       the ARAM transfer buffer from what the system heap has left, and the game's disc thread. */
    const u32 others = p->archive_heap_size + p->game_heap_size + p->command_heap_size;
    const u32 arena = ((fn0)p->get_arena_hi)() - ((fn0)p->get_arena_lo)() - 0xf0;
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    if (!root || arena <= others)
        fail(2);
    const u32 system = ((fn3)p->create_exp_heap)(arena - others, root, 0);
    if (!system)
        fail(2);
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
    const u32 thread = ((fn1)p->alloc)(p->thread_size);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);
    if (!((fn2)p->create_command_heap)(p->command_heap_size, root) ||
        !((fn2)p->create_archive_heap)(p->archive_heap_size, root) ||
        !((fn2)p->create_game_heap)(p->game_heap_size, root))
        fail(3);
    const u32 zelda = ((fn2)p->create_zelda_heap)(((fn1)p->get_free_size)(system) - 0x10000, system);
    if (!zelda)
        fail(3);
    ((fn1)p->become_current_heap)(zelda);
    ((fn3)p->set_aram_buffer)(0, p->aram_buffer_size, system);
    const u32 priority = ((fn1)p->get_thread_priority)(((fn0)p->get_current_thread)());
    ((fn1)p->create_disc_thread)(priority - 2);

    /* The game's audio heap, then its audio process once a frame until the sound library is up, its staying waves
       are in ARAM and its effects sequence plays. */
    const u32 audio_heap = ((fn3)p->create_solid_heap)(p->audio_heap_size, zelda, 0);
    if (!audio_heap)
        fail(4);
    *(volatile u32*)p->audio_heap = audio_heap;
    u32 frames = 0;
    while (!byte(p->audio_ready) || !staying_waves_loaded() || !effects_play()) {
        ((fn0)p->audio_process)();
        if (++frames > SETUP_FRAMES)
            fail(5);
        wait_frame();
    }

    /* The waves that the sound uses: one group of a wave system each. A wave system that the sound library has loaded
       already keeps its group. Each of the game's wave systems has one group. */
    const u32 systems = wave_system_count();
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        const u32 system = p->waves[i] >> 16;
        if (system < systems && !wave_system_loaded(system))
            ((fn2)p->load_wave_group)(system, p->waves[i] & 0xffff);
    }
    for (frames = 0; !named_waves_loaded(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn0)p->audio_process)();
        wait_frame();
    }

    /* The camera. The game's sound set-up clears it. The sound library reads it for an effect with a Doppler shift. */
    const u32 camera = p->default_camera;
    ((fn5)p->set_camera)(word(p->sound_library), word(camera), word(camera + 4), word(camera + 8), 0);

    /* The sound, from no position. The game starts its music that way. */
    ((fn7)p->start_sound)(word(p->sound_library), p->sound_id, (u32)&p->sound, 0, 0, 0, 4);
    if (!p->sound)
        fail(7);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn0)p->audio_process)();
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
