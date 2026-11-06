// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for The Wind Waker's sound layer (JAIZelBasic), on JAudio1, the first generation of the sound
 * library of Nintendo EAD's JSystem. It's compiled for Gekko and stored in the game's .gcsflib. The player boots the
 * game's start-up code: the OS initialisation and the static constructors. The rip patches the game's main to branch
 * here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The driver starts the video
 * interface first. Its retraces pace the driver as they pace the game's frames. The steps that follow:
 *   2. the root heap and the system heap that the game's machine set-up makes. Then the ARAM manager and the main
 *      thread's JKRThread;
 *   3. the game's heaps. Then the ARAM transfer buffer and the game's disc thread;
 *   4. the game's audio heap;
 *   5. the game's audio process, once a frame until the sound library is up: the process loads the start-up archive
 *      and the sequence archive, and starts the effects sequence. The sound library loads the waves that stay loaded
 *      by itself once it's up;
 *   6. the waves that the .minigcsf names, one group of a wave system each;
 *   7. the sound: the sound library starts an effect by its JAudio sound ID. The game's sound layer starts a piece of
 *      music through the starter that the game uses for it (see "Music" below). Before the sound, the driver sets the
 *      sound library's default camera once. The game's sound set-up clears the camera. The game's camera process then
 *      sets it once a frame. The driver also draws the sound library's random numbers that the sound's value names, so
 *      that the sound's first draw comes later in the generator's run. The sound gets the ports that the player sets
 *      (the port list).
 * Then the game's audio process runs once a frame, forever. A step that fails stops the driver with its number in the
 * status word (step 1 is the driver's check of its parameter block). `driver_params.status` reports the driver's
 * progress to the player (see docs/gcsf.md).
 *
 * Music: the game's sound layer starts its music through three starters, and so does the driver. bgmStart starts the
 * main music. subBgmStart starts the music that plays over it, such as a miniboss's. bgmNowBattle starts the battle
 * music. The starters mute some of a piece's tracks as they start it, by the game's state. A piece's ID can also hold
 * that state (MUSIC_STATE_SHIFT). The driver gives it to the game's sound layer before the start. The game calls
 * bgmNowBattle once a frame while a fight goes on. Without that call, the sound layer stops the battle music after 30
 * frames. The driver calls bgmNowBattle once a frame for each piece that subBgmStart or bgmNowBattle starts. A piece
 * that the game's actors give a status gets it on port 9 of the port list. The driver passes it to the game's
 * changeBgmStatus or changeSubBgmStatus a frame after the piece's sequence has started.
 *
 * Build with `driver/build.py wind_waker` (clang). The result is committed as src/rip/driver_wind_waker_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* A wave system's load status in the sound library: 2 once its group is in ARAM. */
#define WAVES_LOADED 2

/* The load types of a wave system in the start-up archive: 0 and 1 stay loaded, 2 is loaded for a scene. */
#define WAVES_FOR_SCENE 2u

/* A JAISound's state once its sequence plays. */
#define SOUND_PLAYING 4u

/* The port that holds the low 16 bits of the map information that the game passes with an effect
   (JAIZelBasic::getMapInfoFxline), such as the material of the ground under a footstep. */
#define MAP_INFO_PORT 6u

/* The sound's value holds the random numbers to draw before the start in these bits, and its JAudio sound ID in the
   others. The game's sound IDs leave bits 20 to 29 clear. */
#define DRAWS_SHIFT 20
#define DRAWS_MASK 0xffu

/* The port that holds the status that the game's actors give a piece of music (JAIZelBasic::changeBgmStatus). */
#define MUSIC_STATUS_PORT 9u

/* A piece of music's ID: bit 31 set and bit 30 clear. The ID gives the piece's index in the low 12 bits. The IDs from
   0x80000105 to 0x80000152 start a piece in another arrangement. */
#define SOUND_KIND_MASK 0xc0000000u
#define MUSIC_KIND 0x80000000u
#define MUSIC_INDEX_MASK 0xfffu
#define MUSIC_PIECES 128u

/* The game's state that a piece of music's ID holds, at MUSIC_STATE_SHIFT: the boat's sail up (setShipSailState), Link
   on board (setLinkOnBoard), Link's sword out (bgmSetSwordUsing) and an enemy near Link (bgmNowBattle's distance). A
   piece's ID leaves bits 16 to 19 clear. An effect's ID holds its category there. */
#define MUSIC_STATE_SHIFT 16
#define MUSIC_STATE_MASK 0xfu
#define STATE_SAILING 1u
#define STATE_ON_BOARD 2u
#define STATE_SWORD 4u
#define STATE_NEAR 8u

/* The distance from Link to the nearest enemy that the driver passes to bgmNowBattle, as a float's bits: 0 for an enemy
   near, and 10,000 for one far away. The game's sound layer counts an enemy within 30 units as near. */
#define NEAR_DISTANCE 0x00000000u
#define FAR_DISTANCE 0x461c4000u

/* The offset of the port list in the parameter block. The word after the driver's entry holds the list's address. */
#define PORT_LIST_OFFSET 0x208
#define TEXT(x) #x
#define NUMBER_TEXT(x) TEXT(x)

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play, with a piece's state (MUSIC_STATE_SHIFT) and the draws before it
                     (DRAWS_SHIFT); patched by each .minigcsf */
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
    u32 sound_state_offset; /* a JAISound's state (a byte) */
    u32 set_camera;         /* void JAIBasic::setCameraInfo(Vec* position, Vec* last_position, Mtx, u32 camera) */
    u32 default_camera;     /* the sound library's default camera: its position, last position and matrix */
    u32 set_port_data;      /* void JAISound::setPortData(u8 port, u16 value) */
    u32 get_random;         /* s32 JASystem::Player::getRandomS32(void): the sound library's random numbers */
    /* the game profile: the game's sound layer (JAIZelBasic). A profile without its starters leaves them 0. The driver
       then starts music as it starts an effect. */
    u32 bgm_start;             /* void bgmStart(u32 id, u32 fade, s32) */
    u32 sub_bgm_start;         /* void subBgmStart(u32 id) */
    u32 battle_bgm_start;      /* void bgmNowBattle(f32 distance) */
    u32 set_ship_sail_state;   /* void setShipSailState(s32) */
    u32 set_link_on_board;     /* void setLinkOnBoard(s32) */
    u32 set_sword_using;       /* void bgmSetSwordUsing(s32) */
    u32 change_bgm_status;     /* void changeBgmStatus(s32): port 9 of the main music */
    u32 change_sub_bgm_status; /* void changeSubBgmStatus(s32): port 9 of the music over it */
    u32 sub_bgm[MUSIC_PIECES / 32]; /* the pieces that subBgmStart starts: bit n % 32 of word n / 32 for piece n */
    u32 battle_bgm;                 /* the ID of the piece that bgmNowBattle starts */
    /* the offset in the sound layer of the main music's handle. The handle of the music over it follows */
    u32 bgm_handle;
    /* the offset in the sound layer of the byte that the game sets once a scene has started. bgmNowBattle requires
       it */
    u32 scene_started;
    /* the ports that the player sets: (port << 16) | value */
    u32 port_count;
    u32 ports[MAX_PORTS];
};

_Static_assert(__builtin_offsetof(struct Params, port_count) == PORT_LIST_OFFSET,
               "the word after the entry holds the port list's address");

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "aram_manager.h"
#include "frames.h"

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
    return effects && byte(effects + p->sound_state_offset) >= SOUND_PLAYING;
}

/* Calls `fn` with `a` in r3 and the float whose bits are at `f` in f1, as the game's code passes a float. The driver is
   built without floating point. A float that it passed would go in a general register. */
__attribute__((naked)) static void call_with_float(u32 fn, const u32* f, u32 a) {
    __asm__ volatile("mtctr 3\n"
                     "lfs 1, 0(4)\n"
                     "mr 3, 5\n"
                     "bctr\n");
}

/* The distance to the nearest enemy that bgmNowBattle gets, as a float's bits. */
static u32 enemy_distance;

/* Whether the game's sound layer starts the piece of music `id` with subBgmStart. */
static int is_sub_bgm(u32 id) {
    const u32 piece = id & MUSIC_INDEX_MASK;
    return piece < MUSIC_PIECES && (driver_params.sub_bgm[piece / 32] >> (piece % 32) & 1);
}

/* Plays the piece of music that the sound's ID names, as the game's sound layer does, forever. The ID's state goes to
   the sound layer first. bgmStart starts the main music without a fade, as every start of the game's scenes does
   (mDoAud_bgmStart). The music's handle is the sound layer's. A frame after the piece's sequence has started, the
   driver gives the piece the ports of the port list: port 9, the piece's status, through the sound layer's writer of
   it, and any other port through setPortData. onEnemyDamage writes port 11 of the battle music when Link hits an
   enemy. */
__attribute__((noreturn)) static void play_music(void) {
    volatile struct Params* p = &driver_params;
    const u32 basic = word(p->sound_library);
    const u32 state = p->sound_id >> MUSIC_STATE_SHIFT & MUSIC_STATE_MASK;
    const u32 id = p->sound_id & ~(MUSIC_STATE_MASK << MUSIC_STATE_SHIFT);
    if (state & STATE_SAILING)
        ((fn2)p->set_ship_sail_state)(basic, 1);
    if (state & STATE_ON_BOARD)
        ((fn2)p->set_link_on_board)(basic, 1);
    if (state & STATE_SWORD)
        ((fn2)p->set_sword_using)(basic, 1);
    enemy_distance = state & STATE_NEAR ? NEAR_DISTANCE : FAR_DISTANCE;

    /* The music over the main music, and the battle music, have the handle after the main music's. bgmNowBattle runs
       once a frame for them, from the frame of the start on. It returns at once unless a scene has started. Its call
       also sets the countdown of the sound layer's frame work (bgmBattleGFrame). With the countdown at 0, the frame
       work stops JA_BGM_SEA_ENEMY. */
    const int over = id == p->battle_bgm || is_sub_bgm(id);
    const u32 handle = basic + p->bgm_handle + (over ? 4 : 0);
    if (over)
        *(volatile u8*)(basic + p->scene_started) = 1;
    if (id == p->battle_bgm) {
        call_with_float(p->battle_bgm_start, &enemy_distance, basic);
    } else if (over) {
        ((fn2)p->sub_bgm_start)(basic, id);
        call_with_float(p->battle_bgm_start, &enemy_distance, basic);
    } else {
        ((fn4)p->bgm_start)(basic, id, 0, 0);
    }
    p->sound = word(handle);
    if (!p->sound)
        fail(7);

    int ports_pending = p->port_count > 0;
    p->status = STATUS_PLAYING;
    u32 frames_playing = 0;
    for (;;) {
        ((fn0)p->audio_process)();
        p->sound = word(handle);
        if (ports_pending && p->sound && byte(p->sound + p->sound_state_offset) >= SOUND_PLAYING &&
            ++frames_playing > 1) {
            for (u32 i = 0; i < p->port_count && i < MAX_PORTS; i++) {
                const u32 port = p->ports[i] >> 16;
                const u32 value = p->ports[i] & 0xffff;
                if (port == MUSIC_STATUS_PORT)
                    ((fn2)(over ? p->change_sub_bgm_status : p->change_bgm_status))(basic, value);
                else if (port < MAX_PORTS)
                    ((fn3)p->set_port_data)(p->sound, port, value);
            }
            ports_pending = 0;
        }
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
        if (over && p->sound)
            call_with_float(p->battle_bgm_start, &enemy_distance, basic);
    }
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
    create_aram_manager();
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

    /* The draws that the sound's value names, and the sound's ID without them. A sequence's command draws a random
       number from the sound library's generator. From the state at the game's start, the first draw gives some effects
       a key that plays nothing. In the game, every earlier draw moves the generator on. The draws come before the
       frames of the set-up. The sound then starts at the same time as without them. */
    const u32 draws = p->sound_id >> DRAWS_SHIFT & DRAWS_MASK;
    for (u32 i = 0; i < draws; i++)
        ((fn0)p->get_random)();
    p->sound_id &= ~(DRAWS_MASK << DRAWS_SHIFT);

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

    /* A piece of music starts through the game's sound layer. */
    if (p->bgm_start && (p->sound_id & SOUND_KIND_MASK) == MUSIC_KIND)
        play_music();

    /* An effect, from no position. The value of the map information's port is the map information. The game gives an
       effect its other ports after the start, before the sound library's next frame starts the effect's sequence. The
       game's seStart gives every effect port 9 that way: the room's reverb. */
    u32 map_info = 0;
    for (u32 i = 0; i < p->port_count && i < MAX_PORTS; i++) {
        if (p->ports[i] >> 16 == MAP_INFO_PORT)
            map_info = p->ports[i] & 0xffff;
    }
    ((fn7)p->start_sound)(word(p->sound_library), p->sound_id, (u32)&p->sound, 0, 0, map_info, 4);
    if (!p->sound)
        fail(7);
    for (u32 i = 0; i < p->port_count && i < MAX_PORTS; i++) {
        const u32 port = p->ports[i] >> 16;
        if (port != MAP_INFO_PORT && port < MAX_PORTS)
            ((fn3)p->set_port_data)(p->sound, port, p->ports[i] & 0xffff);
    }
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn0)p->audio_process)();
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

/* The entry, then the port list's address. */
__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main\n\t.long driver_params + " NUMBER_TEXT(PORT_LIST_OFFSET));
}
