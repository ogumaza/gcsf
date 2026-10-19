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
 *      of the music. The sound system starts an effect.
 * Then the sound layer's frame work runs once a frame, forever. A step that fails stops the driver with its number in
 * the status word. Step 1 is the driver's check of its parameter block and of the piece's number in the profile's
 * table. `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py pikmin2` (clang). The result is committed as src/rip/driver_pikmin2_blob.h. Building GCSF
 * itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The most pieces in the profile's table. */
#define MAX_PIECES 64u

/* A scene loads at most two wave groups, one in each of its areas. 255 names none. */
#define NO_WAVE_SCENE 255u

/* How the driver makes a piece's object. Each kind is a way in which the game makes a piece's object. */
#define PIECE_PLAIN 0u     /* PikSceneMgr::newBgmSeq: a BgmSeq that only plays */
#define PIECE_MAIN 1u      /* PSM::SceneMgr::newMainBgm: a JumpBgmSeq with the scene's directors, for the areas */
#define PIECE_DIRECTED 2u  /* PSM::SceneMgr::newDirectedBgm: the bosses' music and the music of 2-player battles */
#define PIECE_AUTO 3u      /* PSM::SceneMgr::newAutoBgm: an AutoBgm. Its conductor picks what the tracks play */
#define PIECE_BATTLE 4u    /* a DirectedBgm with the 2-player battle directors that newDirectedBgm gives battle_t */
#define PIECE_FREE 5u      /* a DirectedBgm without directors */
#define PIECE_AUTO_FREE 6u /* an AutoBgm without directors or the arrangement of its melody */

/* One piece of the profile's table. */
struct Piece {
    u32 kind; /* PIECE_*, a boss's start point from bit 8, and the root track's beat interval from bit 16 (0 keeps the
                 default) */
    u32 info[4];       /* the JAInter::SoundInfo that the game gives the piece's object */
    char sequence[24]; /* the sequence's file in the sequence archive */
    char conductor[24]; /* an AutoBgm's conductor in the conductor archive */
};

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* 0x80000000 | the piece's number in `pieces`, or an effect's ID; patched by each .minigcsf */
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
    u32 factory_ctor;        /* PSGame::SysFactory::SysFactory() */
    u32 factory_vtable;      /* PSM::Factory's vtable */
    u32 make_se;             /* PSM::SeSound::makeSeSound, the factory's maker of effects */
    u32 new_sound_system;    /* void PSGame::SysFactory::newSoundSystem() */
    u32 scene_manager;       /* the variable that holds the scene manager, a PSM::SceneMgr */
    u32 system_interface;    /* the variable that holds the sound system, a PSSystem::SysIF */
    u32 main_loop;           /* void PSSystem::SysIF::mainLoop(): the sound layer's frame work */
    u32 new_bgm_seq;         /* BgmSeq* PikSceneMgr::newBgmSeq(const char* bms, SoundInfo&) */
    u32 new_main_bgm;        /* BgmSeq* PSM::SceneMgr::newMainBgm(const char* bms, SoundInfo&) */
    u32 new_directed_bgm;    /* BgmSeq* PSM::SceneMgr::newDirectedBgm(const char* bms, SoundInfo&) */
    u32 new_auto_bgm;        /* BgmSeq* PSM::SceneMgr::newAutoBgm(cnd, bms, SoundInfo&, AccessMode, SceneInfo&, ..) */
    u32 new_game_scene;      /* Scene* PSM::SceneMgr::newGameScene(u8 wave_scene, SceneInfo*) */
    u32 append_seq;          /* void PSSystem::Scene::appendSeq(SeqBase*) */
    u32 adapt_child_scene;   /* void PSSystem::Scene::adaptChildScene(Scene*) */
    u32 scene_info_ctor;     /* PSGame::SceneInfo::SceneInfo() */
    u32 directed_bgm_ctor;   /* DirectedBgm::DirectedBgm(const char* bms, const SoundInfo&, DirectorMgrBase*) */
    u32 auto_bgm_ctor;       /* AutoBgm::AutoBgm(const char* cnd, const char* bms, const SoundInfo&, AccessMode, ..) */
    u32 battle_directors_ctor; /* PSM::DirectorMgr_2PBattle::DirectorMgr_2PBattle() */
    u32 adapt_directors;       /* void PSSystem::DirectorMgrBase::initAndAdaptToBgm(DirectedBgm&) */
    u32 start_jump_bgm;        /* void PSSystem::JumpBgmSeq::startSeq(u16 start_point) */
    u32 conductor_heap;        /* the variable that holds the heap of the conductors' objects */
    u32 play_system_se;        /* void PSSystem::SysIF::playSystemSe(u32 id, JAISound** handle, u32 flag) */
    u32 play_data;             /* the variable that holds the game's play data, Game::PlayData */
    u32 rappas;                /* PSGame::Rappa::sRappa: the two captains' marching horns */
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

/* The offsets of the game's objects that the driver reads and writes. */
#define FACTORY_HEAP 0x00         /* SysFactory: the heap for the sound system */
#define FACTORY_HEAP_SIZE 0x04    /* the sound library's ARAM */
#define FACTORY_ARCHIVE 0x08      /* the start-up archive */
#define FACTORY_MAKE_SE 0x0c      /* the maker of effects */
#define FACTORY_VTABLE 0x10       /* its vtable pointer, after the members above */
#define MANAGER_GLOBAL_SCENE 0x04 /* PSSystem::SceneMgr: the global scene */
#define MANAGER_ACCESS_MODE 0x0c  /* the access mode of the conductors' data (a byte) */
#define MANAGER_NEW_GLOBAL 0x10   /* its vtable's newAndSetGlobalScene */
#define SCENE_WAVE_LOADER 0x08    /* PSSystem::Scene: its WaveLoader, or 0 */
#define SCENE_LOAD 0x10           /* its vtable's scene1st(TaskChecker*) */
#define SCENE_START 0x1c          /* its vtable's startMainSeq */
#define LOADER_SECOND_SCENE 0x05  /* WaveLoader: the wave scene of its second area (a byte) */
#define INFO_SCENE_TYPE 0x06      /* PSGame::SceneInfo: the scene's type (a byte) */
#define SEQ_VTABLE 0x10           /* PSSystem::SeqBase: its vtable pointer, after its JSULink */
#define SEQ_INIT 0x0c             /* its vtable's init */
#define SEQ_HANDLE 0x3c           /* its vtable's getHandleP */
#define BGM_ROOT_TRACK 0x70       /* DirectedBgm: its SeqTrackRoot */
#define ROOT_BEAT_INTERVAL 0x3e   /* SeqTrackRoot: the beat interval (a halfword) */
#define TASK_COUNT 0x18           /* TaskChecker: the loads in flight (a byte), after its OSMutex */

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
#define RAPPA_SHORTEST_WAIT 3u
static u32 rappas[2][RAPPA_SIZE / 4];

#include "aram_manager.h"
#include "frames.h"

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

/* Makes the piece's object in the current heap, `heap`, the way the game makes it. Returns 0 if the game made none. */
static u32 make_piece(volatile struct Piece* piece, u32 manager, u32 info, u32 heap) {
    volatile struct Params* p = &driver_params;
    const u32 sequence = (u32)piece->sequence;
    const u32 sound_info = (u32)piece->info;
    u32 seq = 0;
    switch (piece->kind & 0xff) {
    case PIECE_PLAIN:
        seq = ((fn3)p->new_bgm_seq)(manager, sequence, sound_info);
        break;
    case PIECE_MAIN:
        seq = ((fn3)p->new_main_bgm)(manager, sequence, sound_info);
        break;
    case PIECE_DIRECTED:
        seq = ((fn3)p->new_directed_bgm)(manager, sequence, sound_info);
        break;
    case PIECE_AUTO:
        /* newAndSetCurrentScene gives the conductors the current heap first. */
        *(volatile u32*)p->conductor_heap = heap;
        seq = ((fn7)p->new_auto_bgm)(manager, (u32)piece->conductor, sequence, sound_info,
                                     byte(manager + MANAGER_ACCESS_MODE), info, 0);
        break;
    case PIECE_AUTO_FREE:
        /* newAutoBgm would look up the piece's directors in a track map, and the game has none for these pieces. */
        *(volatile u32*)p->conductor_heap = heap;
        seq = ((fn1)p->alloc)(p->auto_bgm_size);
        if (!seq)
            return 0;
        ((fn6)p->auto_bgm_ctor)(seq, (u32)piece->conductor, sequence, sound_info, byte(manager + MANAGER_ACCESS_MODE),
                                0);
        call_virtual(seq, SEQ_VTABLE, SEQ_INIT, 0);
        break;
    case PIECE_BATTLE:
    case PIECE_FREE: {
        u32 directors = 0;
        if ((piece->kind & 0xff) == PIECE_BATTLE) {
            directors = ((fn1)p->alloc)(p->battle_directors_size);
            if (!directors)
                return 0;
            ((fn1)p->battle_directors_ctor)(directors);
        }
        seq = ((fn1)p->alloc)(p->directed_bgm_size);
        if (!seq)
            return 0;
        ((fn4)p->directed_bgm_ctor)(seq, sequence, sound_info, directors);
        call_virtual(seq, SEQ_VTABLE, SEQ_INIT, 0);
        if (directors)
            ((fn2)p->adapt_directors)(directors, seq);
        break;
    }
    }

    /* The game slows the swing of some pieces' root tracks. */
    const u32 beat = piece->kind >> 16;
    if (seq && beat)
        *(volatile u16*)(word(seq + BGM_ROOT_TRACK) + ROOT_BEAT_INTERVAL) = (u16)beat;
    return seq;
}

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

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* JFWSystem::init's heaps: the root heap and the system heap. The system heap becomes the current heap. Then the
       ARAM manager and the main thread's JKRThread. System::initialize makes the root heap current again. The game's
       system object then makes a heap for the sound system. */
    const u32 root = ((fn2)p->create_root_heap)(p->max_heaps, 0);
    if (!root || !((fn3)p->create_exp_heap)(p->system_heap_size, root, 0))
        fail(2);
    create_aram_manager();
    const u32 thread = ((fn1)p->alloc)(p->thread_size);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);
    ((fn1)p->become_current_heap)(root);
    const u32 free = ((fn1)p->get_free_size)(root);
    if (free <= p->heap_margin)
        fail(2);
    const u32 sound = ((fn3)p->create_exp_heap)(free - p->heap_margin, root, 1);
    if (!sound)
        fail(2);
    ((fn1)p->become_current_heap)(sound);

    /* System::createSoundSystem: the start-up archive, read into a heap of its own, and the factory. The factory makes
       the sound system in the sound heap. Then the global scene, in a solid heap. The start-up archive's heap goes. */
    const u32 files = ((fn3)p->create_exp_heap)(p->file_heap_size, root, 1);
    if (!files)
        fail(3);
    ((fn1)p->become_current_heap)(files);
    const u32 archive = ((fn2)p->get_resource)((u32)start_up_archive,
                                               ((fn3)p->mount_file_cache)((u32)audio_folder, files, 0));
    if (!archive)
        fail(3);
    const u32 factory = ((fn1)p->alloc)(p->factory_size);
    if (!factory)
        fail(3);
    ((fn1)p->factory_ctor)(factory);
    *(volatile u32*)(factory + FACTORY_VTABLE) = p->factory_vtable;
    *(volatile u32*)(factory + FACTORY_MAKE_SE) = p->make_se;
    *(volatile u32*)(factory + FACTORY_HEAP) = sound;
    *(volatile u32*)(factory + FACTORY_HEAP_SIZE) = p->sound_memory;
    *(volatile u32*)(factory + FACTORY_ARCHIVE) = archive;
    ((fn1)p->new_sound_system)(factory);
    const u32 manager = word(p->scene_manager);
    const u32 sys_if = word(p->system_interface);
    if (!manager || !sys_if)
        fail(3);
    u32 solid = ((fn3)p->create_solid_heap)(((fn1)p->get_free_size)(sound), sound, 1);
    ((fn1)p->become_current_heap)(solid);
    call_virtual(manager, 0, MANAGER_NEW_GLOBAL, 0);
    ((fn1)p->adjust_size)(solid);
    ((fn1)p->become_current_heap)(sound);
    ((fn1)p->destroy_heap)(files);

    /* System::loadSoundResource: the global scene's resources, in a solid heap. */
    const u32 global = word(manager + MANAGER_GLOBAL_SCENE);
    if (!global)
        fail(4);
    solid = ((fn3)p->create_solid_heap)(((fn1)p->get_free_size)(sound), sound, 1);
    ((fn1)p->become_current_heap)(solid);
    if (!load_scene(global))
        fail(4);
    ((fn1)p->adjust_size)(solid);
    ((fn1)p->become_current_heap)(sound);

    /* The game's objects that the sequences ask about. */
    *(volatile u32*)p->play_data = (u32)play_data;
    for (u32 i = 0; i < 2; i++) {
        *(volatile u16*)((u32)rappas[i] + RAPPA_WAIT) = (u16)RAPPA_SHORTEST_WAIT;
        *(volatile u32*)(p->rappas + 4 * i) = (u32)rappas[i];
    }

    /* The scene's wave groups: the .minigcsf's groups other than the global scene's group 0, one in each of the
       scene's two areas. An effect's waves all lie in group 0. */
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

    /* A piece's scene, with a scene information of the profile's type, and the piece's object in it. */
    u32 seq = 0;
    if (!effect) {
        const u32 info = ((fn1)p->alloc)(p->scene_info_size);
        if (!info)
            fail(5);
        ((fn1)p->scene_info_ctor)(info);
        *(volatile u8*)(info + INFO_SCENE_TYPE) = (u8)p->scene_type;
        const u32 scene = ((fn3)p->new_game_scene)(manager, areas[0], info);
        if (!scene)
            fail(5);
        seq = make_piece(piece, manager, info, sound);
        if (!seq)
            fail(5);
        ((fn2)p->append_seq)(scene, seq);
        if (areas[1] != NO_WAVE_SCENE) {
            const u32 loader = word(scene + SCENE_WAVE_LOADER);
            if (!loader)
                fail(5);
            *(volatile u8*)(loader + LOADER_SECOND_SCENE) = (u8)areas[1];
        }
        ((fn2)p->adapt_child_scene)(global, scene);

        /* The scene's resources, then the piece. The game starts a boss's music when the boss appears, at the part of
           the music that the boss gives. The scene starts the other pieces. */
        if (!load_scene(scene))
            fail(6);
        const u32 start_point = (piece->kind >> 8) & 0xff;
        if (start_point)
            ((fn2)p->start_jump_bgm)(seq, start_point);
        else
            call_virtual(scene, 0, SCENE_START, 0);
    }

    /* An effect starts as the game's system effects start: in two dimensions, with no actor. The sound library keeps
       the effect's JAISound in `sound`, and clears it when the effect ends. */
    u32 handle = (u32)&p->sound;
    if (effect)
        ((fn4)p->play_system_se)(sys_if, p->sound_id, handle, 0);
    else
        handle = call_virtual(seq, SEQ_VTABLE, SEQ_HANDLE, 0);
    p->sound = word(handle);
    if (!p->sound)
        fail(7);
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn1)p->main_loop)(sys_if);
        p->sound = word(handle);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
