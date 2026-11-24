// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Pikmin 2's sound layer (PSSystem, PSGame and PSM), on JAudio1, the first generation of the
 * sound library of Nintendo EAD's JSystem. It's compiled for Gekko and stored in the game's .gcsflib. The player boots
 * the game's start-up code: the OS initialisation and the static constructors. The rip patches the game's main to
 * branch here.
 *
 * The game plays its music through objects of its sound layer. A sound scene holds the objects. A piece's object
 * handles the calls that its sequence makes to the game: the root track's tempo and swing, and the jumps of the music
 * that changes with the action. A cave's music has a conductor. It picks the tracks and phrases that play. The driver
 * makes the piece's object with the game's functions, in a scene of its own, and starts it. The object's directors get
 * no game objects to follow. The music then plays as it does with nothing going on: the layers for work, treasure,
 * nearby enemies, battle and the ground stay muted, and the layers for the number of Pikmin play in full.
 *
 * An effect plays in the effects sequence. The global scene starts that sequence and loads wave group 0. That group
 * holds the waves of every effect. The driver starts an effect as the game starts its system effects, without a
 * position. Some effects ask the game about its state: the story's flags and the captains' marching horns (see
 * play_data and rappas).
 *
 * The profile in `driver_params` gives the game's addresses and sizes, and a table of the pieces. The ripper fills it
 * in. The driver starts the video interface first. Its retraces pace the driver as they pace the game's frames. The
 * steps that follow:
 *   2. the heaps, the ARAM manager and the main thread's JKRThread that JFWSystem::init makes. Then the heap that the
 *      game's system object makes for the sound system;
 *   3. the sound system that System::createSoundSystem makes: the start-up archive from the disc, the factory of the
 *      game's sound layer, and the global scene with its effects sequence;
 *   4. the global scene's resources that System::loadSoundResource loads: its waves and its effects sequence;
 *   5. a piece's object (see Piece), in a scene with no game objects. An effect gets no scene;
 *   6. the scene's resources: the piece's sequence, its conductor, and the wave groups that the .minigcsf names;
 *   7. the sound: the scene starts the piece's first sequence. A boss's music starts as the boss starts it, at a part
 *      of the music. The sound system starts an effect. Before the set-up, the driver draws the sound library's
 *      random numbers that an effect's value names. The effect's first draw then comes later in the generator's run.
 * Then the sound layer's frame work runs once a frame, forever. A step that fails stops the driver with its number in
 * the status word. Step 1 is the driver's check of its parameter block and of the piece's number in the profile's
 * table. `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py pikmin2` (clang). The result is committed as src/rip/driver_pikmin2_blob.h. Building GCSF
 * itself requires no PowerPC toolchain.
 */

#include "driver.h"
#include "jaudio/pikmin2.h"

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* 0x80000000 | the piece's number in `pieces`, or an effect's ID and the draws before it
                     (DRAWS_SHIFT); patched by each .minigcsf */
    u32 sound;    /* the sound's JAISound: 0 once the sound has ended. The driver copies a piece's from the game once a
                     frame. The sound library keeps an effect's here itself */
    /* the waves that the sound uses, patched by each .minigcsf: (wave system << 16) | group */
    u32 wave_count;
    u32 waves[MAX_WAVES];
    /* the game profile: sizes */
    u32 frame_retraces;   /* video retraces between the sound layer's frames */
    u32 max_heaps;        /* the root heap's maximum of standard heaps */
    u32 system_heap_size; /* JFWSystem's system heap */
    u32 aram_audio_size;  /* ARAM for the sound library */
    u32 aram_graph_size;  /* ARAM for graphics */
    u32 aram_priorities;  /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 thread_size;      /* a JKRThread, for the main thread's */
    u32 heap_margin;      /* the root heap's space that the sound heap leaves free */
    u32 file_heap_size;   /* the heap that holds the start-up archive while the sound system starts */
    u32 factory_size;     /* PSM::Factory */
    u32 sound_memory;     /* the factory's heap size: the sound library's ARAM */
    u32 scene_info_size;  /* PSGame::SceneInfo */
    u32 scene_type;       /* the type of the piece's scene: one that the game plays with no game objects */
    u32 battle_directors_size; /* PSM::DirectorMgr_2PBattle */
    u32 directed_bgm_size;     /* PSSystem::DirectedBgm */
    u32 auto_bgm_size;         /* PSAutoBgm::AutoBgm */
    /* the game profile: the addresses of the game's functions and variables */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 get_current_thread;  /* OSThread* OSGetCurrentThread(void) */
    u32 init_mutex;          /* void OSInitMutex(OSMutex*) */
    u32 create_root_heap;    /* JKRExpHeap* JKRExpHeap::createRoot(int max_heaps, bool) */
    u32 create_exp_heap;     /* JKRExpHeap* JKRExpHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 create_solid_heap;   /* JKRSolidHeap* JKRSolidHeap::create(u32 size, JKRHeap* parent, bool) */
    u32 get_free_size;       /* s32 JKRHeap::getFreeSize() */
    u32 become_current_heap; /* JKRHeap* JKRHeap::becomeCurrentHeap() */
    u32 destroy_heap;        /* void JKRHeap::destroy() */
    u32 adjust_size;         /* s32 JKRSolidHeap::adjustSize() */
    u32 alloc;               /* void* operator new(u32 size) from the current heap */
    u32 thread_ctor;         /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 mount_file_cache;    /* JKRFileCache* JKRFileCache::mount(const char* path, JKRHeap*, const char*) */
    u32 get_resource;        /* void* JKRFileLoader::getGlbResource(const char* name, JKRFileLoader*) */
    struct SoundLayer layer; /* the sound layer's functions and variables (jaudio/pikmin2.h) */
    u32 get_random;            /* s32 JASPlayer::getRandomS32(void): the sound library's random numbers */
    u32 big_boss;              /* PSM::EnemyBigBoss::sBigBoss: the final boss's sound object */
    /* the pieces */
    u32 piece_count;
    struct Piece pieces[MAX_PIECES];
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

/* The game's paths: the folder of its sound files and its start-up archive. */
static const char audio_folder[] = "/AudioRes";
static const char start_up_archive[] = "PSound.aaf";

/* A TaskChecker. It counts a scene's loads in flight. */
static u32 task[8] __attribute__((aligned(8)));

/* The game's play data at the story's start: every flag clear. The effects of the ship and the pod ask whether the
   debt is paid (the byte at +0x2f). */
static u32 play_data[0x40];

/* The captains' marching horns (PSGame::Rappa). Their effects ask for the horn's place in its tune (+0x1e) and for the
   wait after each note (+0x1c). The place starts at 0, the tune's beginning. The wait is the shortest that the game
   sets: the stick tilted all the way. */
#define RAPPA_SIZE 0x20
#define RAPPA_WAIT 0x1c
static u32 rappas[2][RAPPA_SIZE / 4];

/* The boss of a boss's music. PSM::MiddleBossSeq and PSM::BigBossSeq hold it at +0x134. When the music jumps into an
   attack, their onJump (0x8045c2f0 and 0x8045c98c) asserts that the boss is set, then writes 1 to the boss's byte at
   +0xff. Without a boss, the assert stops the game, and the attack plays nothing. The driver's scene has no boss. A
   boss's music gets a stand-in: a zeroed object that covers the byte. It also stands in for the final boss's sound
   object: the game only tests that sBigBoss is set. */
#define STAND_IN_BOSS_SIZE 0x100
static u32 stand_in_boss[STAND_IN_BOSS_SIZE / 4];

#include "aram_manager.h"
#include "frames.h"
#include "jsystem_heaps.h"

/* Calls a virtual function: the function at `slot` in the vtable that `object` holds at `vtable`. */
static u32 call_virtual(u32 object, u32 vtable, u32 slot, u32 argument) {
    return ((fn2)word(word(object + vtable) + slot))(object, argument);
}

/* Loads a scene's resources the way Scene::scene1stLoadSync does. Waits until the loads are done. */
static int load_scene(u32 scene) {
    volatile struct Params* p = &driver_params;
    ((fn1)p->init_mutex)((u32)task);
    *(volatile u8*)((u32)task + TASK_COUNT) = 0;
    call_virtual(scene, 0, SCENE_LOAD, (u32)task);
    for (u32 frames = 0; byte((u32)task + TASK_COUNT) != 0; frames++) {
        if (frames > SETUP_FRAMES)
            return 0;
        wait_frame();
    }
    return 1;
}

#include "jaudio/pikmin2_steps.h"

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);
    /* A piece's ID sets the top bit. An effect's ID doesn't. */
    const int effect = (p->sound_id & 0x80000000u) == 0;
    const u32 number = p->sound_id & 0xffff;
    if (!effect && (number >= p->piece_count || number >= MAX_PIECES))
        fail(1);
    volatile struct Piece* piece = &p->pieces[effect ? 0 : number];
    if (effect)
        pikmin2_draw_randoms();

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* JFWSystem::init's heaps: the root heap and the system heap. The system heap becomes the current heap. Then the
       ARAM manager, the main thread's JKRThread and the sound heap. */
    u32 system;
    const u32 root = create_jsystem_heaps(&system);
    create_aram_manager();
    const u32 sound = pikmin2_sound_heap(root);

    /* System::createSoundSystem: the start-up archive, read into a heap of its own, then the sound system. */
    const u32 files = ((fn3)p->create_exp_heap)(p->file_heap_size, root, 1);
    if (!files)
        fail(3);
    ((fn1)p->become_current_heap)(files);
    const u32 archive = ((fn2)p->get_resource)((u32)start_up_archive,
                                               ((fn3)p->mount_file_cache)((u32)audio_folder, files, 0));
    if (!archive)
        fail(3);
    u32 sys_if;
    const u32 manager = pikmin2_sound_system(archive, sound, files, &sys_if);

    /* System::loadSoundResource: the global scene's resources, in a solid heap. */
    const u32 global = word(manager + MANAGER_GLOBAL_SCENE);
    if (!global)
        fail(4);
    const u32 solid = ((fn3)p->create_solid_heap)(((fn1)p->get_free_size)(sound), sound, 1);
    ((fn1)p->become_current_heap)(solid);
    if (!load_scene(global))
        fail(4);
    ((fn1)p->adjust_size)(solid);
    ((fn1)p->become_current_heap)(sound);

    pikmin2_game_objects();

    /* The scene's wave groups: the .minigcsf's groups other than the global scene's group 0, one in each of the
       scene's two areas. All the waves of an effect are in group 0. */
    u32 areas[2] = {NO_WAVE_SCENE, NO_WAVE_SCENE};
    u32 area_count = 0;
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        const u32 group = p->waves[i] & 0xffff;
        if (group == 0)
            continue;
        if (area_count == 2 || effect)
            fail(5);
        areas[area_count++] = group;
    }

    const u32 seq = effect ? 0 : pikmin2_start_piece(piece, manager, global, sound, areas);
    pikmin2_play(effect, piece, seq, sys_if);
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
