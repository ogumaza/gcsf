// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Luigi's Mansion. The game's sound library is an early JAudio1, with a sound layer above it
 * (JAIBasic) and the game's wrapper above that. It's compiled for Gekko and stored in the game's .gcsflib. The player
 * boots the game's start-up code: the OS initialisation and the static constructors. The rip patches the game's main
 * to branch here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The driver starts the video
 * interface first. Its retraces pace the driver as they pace the game's frames. The steps that follow:
 *   2. the disc, with long file names as the disc's bi2.bin allows them, the root heap and the game's heap, and the
 *      ARAM manager, as the game's main makes them;
 *   3. the game's sound set-up, with a block of the game's heap for the sound library. Then the wave systems of load
 *      type 0. The game's start-up loads them before its first frame;
 *   4. the game's sound frame, once a frame, until the sound library is up and every wave system that stays loaded is
 *      in ARAM. The sound library loads those of load type 1 by itself once it's up;
 *   5. the sound: the sound library starts it by its JAudio sound ID. The game starts most of its music the same way.
 *      The game starts LuigiSings through a function of its own instead. That function reads the game's state: Luigi's
 *      health, the room's lights, the room and the room's sound information. It then picks the tracks that play and
 *      sets the piece's tempo. A sound ID with a state in bits 16 to 23 gives the game that state, then calls that
 *      function. The function puts the piece's JAISound in a word of the game's, and the driver copies that word to the
 *      sound's handle once a frame.
 * Then the game's sound frame runs once a frame, forever. A step that fails stops the driver with its number in the
 * status word (step 1 is the driver's check of its parameter block). `driver_params.status` reports the driver's
 * progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py luigis_mansion` (clang). The result is committed as
 * src/rip/driver_luigis_mansion_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The sound library's state once it's up and its effects sequence plays. */
#define STATE_RUNNING 4u

/* The load type of a wave system that a scene loads. The others stay loaded. */
#define WAVES_FOR_SCENE 2u

/* The most states of LuigiSings in the profile's table. */
#define MAX_STATES 16u

/* Where a sound ID gives a state of LuigiSings, from 1. A music ID uses only bit 31 and its low 16 bits. */
#define STATE_SHIFT 16u
#define STATE_MASK 0xffu

/* A state of the game for LuigiSings, as the game's code keeps it. */
struct State {
    u32 health;     /* Luigi's health, 0 to 100 */
    u32 lights;     /* 1 if the room's lights are on, or 0 */
    u32 room;       /* the room's number */
    u32 room_sound; /* the room's sound information: its sound_echo_parameter, sound_room_size from bit 6 and
                       sound_room_code from bit 8 */
};

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play, with a state of LuigiSings in bits 16 to 23 (from 1) or 0 there;
                     patched by each .minigcsf */
    u32 sound;    /* the sound's JAISound handle (written by the game): its JAISound while it plays, and 0 after */
    /* the game profile: the sizes that the game's main gives */
    u32 frame_retraces;  /* video retraces between the game's frames */
    u32 max_heaps;       /* the root heap's maximum of standard heaps */
    u32 game_heap_size;  /* the game's heap, from the root heap */
    u32 aram_audio_size; /* ARAM for the sound library */
    u32 aram_graph_size; /* ARAM for graphics */
    u32 aram_priorities; /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 audio_heap_size; /* the block of the game's heap for the sound library */
    u32 audio_heap_alignment;
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
    u32 audio_setup;         /* the game's sound set-up: (heap block, its size, ARAM for the sound library) */
    u32 load_staying_waves;  /* the game's call that loads the wave systems of load type 0 */
    u32 audio_frame;         /* the game's sound frame */
    u32 sound_library;       /* the variable that holds the sound library (a JAIBasic) */
    u32 state_offset;        /* the sound library's state block, whose byte +1 is the state */
    u32 wave_systems_offset; /* the sound library's list of wave systems: pointer, size, load type */
    u32 last_load_offset;    /* the sound library's word that the last finished wave load sets */
    u32 start_sound;         /* void JAIBasic::startSoundVec(u32 id, JAISound**, Vec*, u32, u8) */
    /* the game profile: LuigiSings's start and the game's state that the start reads */
    u32 singing_start;   /* the game's start of LuigiSings: (u32 id), which gives the piece's JAISound to `music` */
    u32 music;           /* the game's word for the JAISound of its music */
    u32 set_health;      /* the game's call that hands the sound layer Luigi's health: (s32) */
    u32 set_lights;      /* the game's call that says whether the lights are on for the music: (u8). The game's frame
                            gives it the room's lights, or 0 while the game's flag 61 is set */
    u32 set_room_lights; /* the game's call that says whether the room's lights are on: (u8) */
    u32 room;            /* the game's word for the room's number */
    u32 room_sound;      /* the game's word for the room's sound information */
    /* the game profile: LuigiSings's states */
    u32 state_count;
    struct State states[MAX_STATES];
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "aram_manager.h"
#include "frames.h"

/* Whether the sound library is up, and every wave system that stays loaded is in ARAM. The library loads them in the
   order of its list, one at a time, and each load that finishes sets the library's last load to the system's number
   in its high 16 bits. */
static int sound_library_ready(void) {
    volatile struct Params* p = &driver_params;
    const u32 library = word(p->sound_library);
    const u32 state = library ? word(library + p->state_offset) : 0;
    if (!state || byte(state + 1) < STATE_RUNNING)
        return 0;
    const u32 systems = word(library + p->wave_systems_offset);
    u32 last = 0xffffffffu;
    for (u32 i = 0; systems && word(systems + i * 12); i++) {
        if (word(systems + i * 12 + 8) != WAVES_FOR_SCENE)
            last = i;
    }
    return last == 0xffffffffu || word(library + p->last_load_offset) == last << 16;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces, and the disc. The game's OS reads the flag for long file names from the
       disc's bi2.bin, which the apploader leaves in memory. GCSF's machine leaves none. The game's disc sets the flag,
       and its sound files have long names. */
    ((fn0)p->vi_init)();
    *(volatile u32*)p->long_file_names = 1;
    ((fn0)p->dvd_init)();

    /* The game's main: the root heap, the game's heap as the system heap and the current heap, and the ARAM
       manager. */
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    if (!root)
        fail(2);
    const u32 game = ((fn3)p->create_exp_heap)(p->game_heap_size, root, 0);
    if (!game)
        fail(2);
    ((fn1)p->become_system_heap)(game);
    ((fn1)p->become_current_heap)(game);
    create_aram_manager();

    /* The game's sound set-up, with its block of the game's heap, and the wave systems of load type 0. */
    const u32 block = ((fn3)p->heap_alloc)(p->audio_heap_size, p->audio_heap_alignment, game);
    if (!block)
        fail(3);
    ((fn3)p->audio_setup)(block, p->audio_heap_size, p->aram_audio_size);
    ((fn0)p->load_staying_waves)();

    /* The game's sound frame until the sound library is up and its waves are in ARAM. */
    for (u32 frames = 0; !sound_library_ready(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(4);
        ((fn0)p->audio_frame)();
        wait_frame();
    }

    /* The sound, from no position, as the game starts its music. A state of LuigiSings goes to the game first. The
       game's calls set Luigi's health and the lights. The game's calls that set the room can start other sounds. The
       driver writes the room's words itself. */
    const u32 state = p->sound_id >> STATE_SHIFT & STATE_MASK;
    if (state) {
        if (!p->singing_start || state > p->state_count || state > MAX_STATES)
            fail(5);
        volatile struct State* s = &p->states[state - 1];
        ((fn1)p->set_health)(s->health);
        ((fn1)p->set_lights)(s->lights);
        ((fn1)p->set_room_lights)(s->lights);
        *(volatile u32*)p->room = s->room;
        *(volatile u32*)p->room_sound = s->room_sound;
        ((fn1)p->singing_start)(p->sound_id & ~(STATE_MASK << STATE_SHIFT));
        p->sound = word(p->music);
    } else {
        ((fn6)p->start_sound)(word(p->sound_library), p->sound_id, (u32)&p->sound, 0, 0, 4);
    }
    if (!p->sound)
        fail(5);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn0)p->audio_frame)();
        if (state)
            p->sound = word(p->music);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
