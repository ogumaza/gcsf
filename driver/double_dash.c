// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Mario Kart: Double Dash!!. The game's sound library is JAudio2, the sound library of Nintendo
 * EAD's JSystem, in its earliest known build. The driver is compiled for Gekko and stored in the game's .gcsflib. The
 * player boots the game's startup code: the OS initialisation and the static constructors. The static constructors
 * construct the game's audio object. The rip patches the game's main to branch here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The steps:
 *   1. the video interface: its retraces pace the driver as they pace the game's frames;
 *   2. what JSystem's framework sets up first: OSInit, DVDInit, the root heap and the system heap;
 *   3. the ARAM manager, the main thread's JKRThread, the ARAM streams' transfer buffer, and the game's audio heap;
 *   4. the audio object's first step;
 *   5. the start-up archive, loaded the way the game's loader task loads it, and the audio object's set-up with it.
 *      The set-up copies what it keeps. The archive's buffer is then freed;
 *   6. every wave bank of the sound library's section loaded into ARAM. The game keeps them all loaded. Its voices
 *      come from other start-up archives. The driver doesn't load those;
 *   7. the sound: the sound manager starts it by its JAudio sound ID;
 *   8. the audio object's frame process, once a frame, forever. A level sound, such as an engine's, lasts a frame
 *      unless the game starts it again. The game starts it again each frame for as long as the sound should play. The
 *      driver does the same until the sound ends.
 * `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py double_dash` (clang). The result is committed as src/rip/driver_double_dash_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
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

/* JKRDvdRipper's options: decode Yaz0, and allocate from a heap's tail. */
#define EXPAND 1u
#define FROM_TAIL 2u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* the sound's JAISoundHandle (written by the game): its JAISound while it plays, and 0 after */
    /* the game profile: the sizes that the game's start-up gives */
    u32 frame_retraces;   /* video retraces between the frame process's calls */
    u32 max_heaps;        /* the root heap's maximum of standard heaps */
    u32 system_heap_size;
    u32 aram_audio_size;  /* ARAM for the sound library */
    u32 aram_graph_size;  /* ARAM for graphics */
    u32 aram_priorities;  /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 thread_size;      /* a JKRThread, for the main thread's */
    u32 transfer_size;    /* the ARAM streams' transfer buffer */
    u32 audio_heap_size;
    /* the game profile: the addresses of the game's functions and variables */
    u32 os_init;              /* void OSInit(void) */
    u32 dvd_init;             /* void DVDInit(void) */
    u32 vi_init;              /* void VIInit(void) */
    u32 vi_wait_for_retrace;  /* void VIWaitForRetrace(void) */
    u32 get_current_thread;   /* OSThread* OSGetCurrentThread(void) */
    u32 create_root_heap;     /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;      /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_solid_heap;    /* JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_aram;          /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 set_transfer_buffer;  /* void JKRAramStream::setTransBuffer(u8*, u32 size, JKRHeap*) */
    u32 alloc;                /* void* operator new(u32 size) from the current heap */
    u32 free;                 /* void operator delete(void*) */
    u32 thread_ctor;          /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 path_to_entry;        /* s32 DVDConvertPathToEntrynum(const char*) */
    u32 load_file;            /* JKRDvdRipper::loadToMainRAM(const char*, u8*, expand, u32, JKRHeap*, direction, ...) */
    u32 sound_archive_path;   /* the game's path of its start-up archive */
    u32 audio_heap;           /* the variable that holds the game's audio heap */
    u32 audio;                /* the variable that holds the game's audio object */
    u32 audio_start;          /* the audio object's first step: (audio) */
    u32 audio_setup;          /* the audio object's set-up: (audio, JKRHeap*, u32 aram, void* baa, 0, 0) */
    u32 audio_frame;          /* the audio object's frame process: (audio) */
    u32 sound_manager_offset; /* the sound manager (a JAISoundStarter) in the audio object */
    u32 section_offset;       /* the sound library's section in the audio object */
    u32 load_wave_bank;       /* bool JAUSection::loadWaveArc(u32 bank): every archive of a wave bank */
    u32 section_data_offset;  /* the section heap's data in the section */
    u32 wave_banks_offset;    /* the data's table of wave banks, and its count after it */
    u32 wave_arc_state_offset; /* a wave archive's state (2 once it is in ARAM), and its disc entry after it */
    u32 sound_info_offset;    /* the game's sound information in the audio object. It reads the sound table. */
    u32 is_level_sound;       /* bool (sound information*, JAISoundID*): whether the sound is a level sound */
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
typedef u32 (*fn6)(u32, u32, u32, u32, u32, u32);
typedef u32 (*fn9)(u32, u32, u32, u32, u32, u32, u32, u32, u32);

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

    /* What JSystem's framework sets up first. The system heap becomes the system heap and the current heap. */
    ((fn0)p->os_init)();
    ((fn0)p->dvd_init)();
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    const u32 system = root ? ((fn3)p->create_exp_heap)(p->system_heap_size, root, 0) : 0;
    if (!system)
        fail(2);

    /* The ARAM manager, the main thread's JKRThread and the ARAM streams' transfer buffer. The buffer comes from the
       system heap. Then the game's audio heap, from the root heap. */
    ((fn5)p->create_aram)(p->aram_audio_size, p->aram_graph_size, (p->aram_priorities >> 16) & 0xff,
                          (p->aram_priorities >> 8) & 0xff, p->aram_priorities & 0xff);
    const u32 thread = ((fn1)p->alloc)(p->thread_size);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);
    ((fn3)p->set_transfer_buffer)(0, p->transfer_size, system);
    const u32 audio_heap = ((fn3)p->create_solid_heap)(p->audio_heap_size, root, 0);
    if (!audio_heap)
        fail(3);
    *(volatile u32*)p->audio_heap = audio_heap;

    /* The audio object and its first step. The static constructors built the object. */
    const u32 audio = word(p->audio);
    if (!audio)
        fail(4);
    ((fn1)p->audio_start)(audio);

    /* The start-up archive, at the root heap's tail, and the audio object's set-up. The game's loader task does the
       same, then frees the archive's buffer. A disc without the file fails here, before the loader reads it. */
    if ((s32)((fn1)p->path_to_entry)(p->sound_archive_path) < 0)
        fail(5);
    const u32 baa = ((fn9)p->load_file)(p->sound_archive_path, 0, EXPAND, 0, root, FROM_TAIL, 0, 0, 0);
    if (!baa)
        fail(5);
    ((fn6)p->audio_setup)(audio, audio_heap, p->aram_audio_size, baa, 0, 0);
    ((fn1)p->free)(baa);
    const u32 section = word(audio + p->section_offset);
    if (!section)
        fail(5);

    /* Every wave bank into ARAM. The audio frame process runs while the archives load. */
    const u32 data = word(section + p->section_data_offset);
    const u32 banks = word(data + p->wave_banks_offset);
    const u32 count = word(data + p->wave_banks_offset + 4);
    for (u32 b = 0; b < count; b++) {
        if (word(banks + b * 4))
            ((fn2)p->load_wave_bank)(section, b);
    }
    for (u32 frames = 0; !waves_loaded(section); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn1)p->audio_frame)(audio);
        wait_frame();
    }

    /* The sound. The sound manager's vtable gives startSound(JAISoundID*, JAISoundHandle*, const TVec3*) at 0xc. */
    const u32 manager = word(audio + p->sound_manager_offset);
    const u32 id = p->sound_id;
    if (!manager || !(((fn4)virtual_function(manager, 0xc))(manager, (u32)&id, (u32)&p->sound, 0) & 0xff))
        fail(7);
    const u32 info = word(audio + p->sound_info_offset);
    const int level = info && (((fn2)p->is_level_sound)(info, (u32)&id) & 0xff);
    p->status = STATUS_PLAYING;

    for (;;) {
        /* A level sound's lifetime is one frame. While the handle holds the sound, startSound only renews it. */
        if (level && p->sound)
            ((fn4)virtual_function(manager, 0xc))(manager, (u32)&id, (u32)&p->sound, 0);
        ((fn1)p->audio_frame)(audio);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
