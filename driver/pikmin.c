// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Pikmin. The game's sound library is an early JAudio1 with a C interface (Jac, Jaf, Jaq and
 * Jam), without a sound layer above it. It's compiled for Gekko and stored in the game's .gcsflib. The player boots the
 * game's start-up code: the OS initialisation and the static constructors. The rip patches the game's main to branch
 * here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The driver first sets up the OS,
 * the disc with long file names as the disc's bi2.bin allows them, the video interface and ARAM. The video interface's
 * retraces pace the driver as they pace the game's frames. The steps that follow:
 *   2. the sound library, with a block of the arena. The sound library loads the game's sequence index, its banks and
 *      the effects' first wave group. Then it starts its threads. The game then gives it a buffer for the disc, from
 *      two words of the game's system object. Only the system object's set-up fills them in. The driver doesn't run
 *      that set-up. It passes the two words as they are, 0 and 0. The sound library's disc thread then keeps its two
 *      32 KB buffers;
 *   3. the wait until the sound library is up;
 *   4. the piece of music: its sequence and its wave group. The game's scene set-up reads them the same way;
 *   5. the piece, in the first of the two slots that the game plays music in, once the disc has read it.
 * The driver then waits for ever. The sound library's threads play the piece. The driver watches the piece's root
 * track and reports its end.
 * The music of the game's cutscenes is one piece in parts. The sound library plays that piece from its start-up. The
 * piece's root track polls a port. A cutscene writes the number of a part to that port. The root track then opens a
 * track that plays the part. For a part, the driver loads the part's wave group in step 4. In place of step 5, the
 * driver finds the piece's root track (step 6) and writes the part's number to the port. A cutscene does the same.
 * The driver then waits for the track that plays the part (step 7) and watches that track.
 * The sound library plays the effects' sequence and the events' sequence from its start-up too. Tracks of those
 * sequences poll ports, and the game's functions for effects write to them. For an effect, in place of steps 4 and 5:
 *   8. the effects' other wave groups, as the game's set-up of its first scene of play loads them;
 *   9. the wait for the tracks that play the effects;
 *   10. the effect, started by the game's function for its kind. An event's effect plays by an action of an event that
 *       the driver creates at the camera;
 *   11. the wait for the track that plays the effect.
 * The driver sets the game's scene to the play of a stage first. The game plays two of the system's effects only
 * there. Once a frame, the driver then calls the game's update of the camera, as the game's frame does. That update
 * gives each event its volume and pan by its place against the camera. The driver doesn't run the game's check of
 * its events. The event then stays as long as the effect plays, as an event that the game updates each frame does.
 * The driver watches the track that plays the effect.
 * The sound of the formation plays in a track of the effects sequence while the player moves the formation. The
 * game's function for the formation takes the move as x and y, -127 to 127, once a frame. It writes x and the move's
 * length to ports of the track. The driver gives it x from the port list's port 2 and y from port 3 each frame.
 * A step that fails stops the driver with its number in the status word (step 1 is the driver's check of its parameter
 * block). `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py pikmin` (clang). The result is committed as src/rip/driver_pikmin_blob.h. Building GCSF
 * itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The fields of Params.sound_id: 0x80000000 | group << 24 | wave system << 16 | part << 8 | piece. The piece field
   holds the piece's number in the sequence index. The driver loads the wave group `group` of the wave system. Part 0
   plays the piece in the game's slot for music. Any other part goes to the piece that the sound library plays from
   its start-up. */
#define SOUND_PIECE(id) ((id) & 0xffu)
#define SOUND_PART(id) (((id) >> 8) & 0xffu)
#define SOUND_SYSTEM(id) (((id) >> 16) & 0xffu)
#define SOUND_GROUP(id) (((id) >> 24) & 0x3fu)

/* An effect's ID: 0x80000000 | kind << 24 | number << 8 | sequence. The sequence field holds 0 or 1, the effects'
   sequence or the events'. Pieces start at FIRST_PIECE. The kind gives the game's function that starts the effect. An
   event's number holds the event's type and the action (EVENT_TYPE). */
#define FIRST_PIECE 2u
#define EFFECT_KIND(id) (((id) >> 24) & 0x7fu)
#define EFFECT_NUMBER(id) (((id) >> 8) & 0xffffu)
#define EFFECT_SYSTEM 1u
#define EFFECT_PLAYER 2u
#define EFFECT_EVENT 3u
#define EFFECT_FORMATION 4u
#define EVENT_TYPE(number) ((number) >> 12)
#define EVENT_ACTION(number) ((number) & 0xfffu)

/* The DVD task's passes for a piece's files and for the effects' wave groups. The game's scene set-up makes them the
   same way. */
#define PASS_OF_PIECE 0x10000u
#define PASS_OF_EFFECTS 0x20000u

/* The wave groups that the game's set-up of its first scene of play loads for the effects: groups 1 to 3 of wave
   system 0, then group 0 of systems 14, 15 and 2 (Jac_SceneSetup, 0x80019be0). It loads the first three without
   making them their system's scene. The ripper's profile names the same groups (effect_groups). */
#define EFFECTS_SYSTEM 0u
#define EFFECTS_FIRST_GROUP 1u
#define EFFECTS_LAST_GROUP 3u
static const u32 scene_systems[] = {14, 15, 2};

/* The game's scene of a stage's play, and the system's effect that closes the menu. Jac_PlaySystemSe plays that effect
   only while the menu's flag is set. The effect that opens the menu sets it. */
#define SCENE_OF_PLAY 5u
#define EFFECT_CLOSES_MENU 7u

/* The port that a cutscene writes its part to, and the word of a track that holds the track's first child. The root
   track opens the track that plays the part as its first child. */
#define PART_PORT 0u
#define TRACK_FIRST_CHILD 0x44u

/* A track's state, a byte: 0 while the track is free. A track has 16 children, in words from TRACK_FIRST_CHILD. */
#define TRACK_STATE 0x3cu
#define TRACK_FREE 0u
#define TRACK_CHILDREN 16u

/* The ports of the formation's track that Jac_Orima_Formation (0x80018200) writes: x, and the length of the move
   (x, y), from 0 to 179. It writes 1 to port 0 as a move starts and 0 as it ends. The driver passes port 2's value as
   x and port 3's as y. With x 0, the ports take those values. */
#define FORMATION_X_PORT 2u
#define FORMATION_Y_PORT 3u

/* The offset of the port list in the parameter block. The word after the driver's entry holds the list's address. */
#define PORT_LIST_OFFSET 0xcc
#define TEXT(x) #x
#define NUMBER_TEXT(x) TEXT(x)

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the piece or part to play (see SOUND_PIECE); patched by each .minigcsf */
    u32 sound;    /* the address of `piece_root` while the sound plays, and 0 after it (written by the driver) */
    /* the game profile: sizes and values that the game's start-up gives */
    u32 frame_retraces;       /* video retraces between the game's frames */
    u32 audio_heap_size;      /* the block of memory for the sound library */
    u32 audio_heap_alignment; /* its alignment */
    u32 aram_audio_size;      /* ARAM for the sound library */
    u32 aram_blocks;          /* the ARAM allocator's block list (ARInit's stack) */
    u32 aram_block_count;     /* its number of entries */
    u32 sound_path;           /* the sound files' folder on the disc, a string in the game's data */
    u32 system;               /* the variable that holds the game's system object */
    u32 disc_buffer;          /* the system object's word: the disc buffer that the game gives the sound library */
    u32 disc_buffer_size;     /* the system object's word: that buffer's size in units of 64 bytes */
    u32 music_slot;           /* the slot that the game plays its music in */
    u32 music_handle;         /* that slot's root sequence */
    /* the game profile: the addresses of the game's functions and variables */
    u32 os_init;             /* void OSInit(void) */
    u32 long_file_names;     /* the OS's flag that allows file names longer than 8.3 */
    u32 dvd_init;            /* void DVDInit(void) */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 ar_init;             /* u32 ARInit(u32* stack, u32 entries) */
    u32 arq_init;            /* void ARQInit(void) */
    u32 get_arena_lo;        /* void* OSGetArenaLo(void) */
    u32 set_arena_lo;        /* void OSSetArenaLo(void*) */
    u32 start;               /* void Jac_Start(void* heap, u32 size, u32 aram, char* path) */
    u32 add_disc_buffer;     /* void Jac_AddDVDBuffer(u8* buffer, u32 size) */
    u32 boot_ok;             /* the sound library's flag: it's up */
    u32 ready_bgm;           /* void Jac_ReadyBgm(u32 piece): the sequence, read from the disc */
    u32 collect_aram;        /* void Collect_AramMotherHeap(void) */
    u32 set_wave_scene;      /* void WaveScene_Set(u32 system, u32 group) */
    u32 check_pass;          /* void DVDT_CheckPass(u32 pass, u32* flag, void (*)(u32)) */
    u32 play_bgm;            /* void Jac_PlayBgm(u32 slot, u32 piece) */
    u32 handle_to_sequence;  /* seqp_* Jaf_HandleToSeq(u32 handle) */
    u32 part_track;          /* the name of the root track that a cutscene writes its part to */
    u32 get_track_handle;    /* seqp_* Jam_GetTrackHandle(u32 name) */
    u32 write_port;          /* u32 Jam_WritePortAppDirect(seqp_* track, u32 port, u16 value) */
    /* the game profile for effects */
    u32 load_wave_scene;    /* void WaveScene_Load(u32 system, u32 group) */
    u32 scene;              /* the sound library's variable: the game's scene */
    u32 menu;               /* the sound library's flag: the menu is open */
    u32 play_system_effect; /* void Jac_PlaySystemSe(u32 number) */
    u32 play_player_effect; /* void Jac_PlayOrimaSe(u32 number) */
    u32 create_event;       /* s32 Jac_CreateEvent(u8 type, Vector3D* position) */
    u32 play_event_action;  /* u32 Jac_PlayEventAction(s32 event, u32 action) */
    u32 update_camera;      /* void Jac_UpdateCamera(void) */
    u32 system_track;       /* the name of the track that plays the system's effects */
    u32 player_track;       /* the name of the track that plays the player's effects */
    u32 events_track;       /* the name of the events' root track. Its children play the events */
    /* the game profile for the formation */
    u32 move_formation;  /* void Jac_Orima_Formation(s32 x, s32 y) */
    u32 formation_track; /* the name of the effects sequence's track for the formation */
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

#include "frames.h"

/* The flag that the disc task's pass sets once the disc has read everything before it, and the sound's track: the
   piece's root track, or the track that plays the part. The length analysis reads that track through `sound`. */
static volatile u32 piece_read;
static volatile u32 piece_root;

/* The end of the sound: once its track is free again. */
static void check_end(volatile struct Params* p) {
    if (p->status == STATUS_PLAYING && byte(piece_root + TRACK_STATE) == TRACK_FREE) {
        p->sound = 0;
        p->status = STATUS_FINISHED;
    }
}

/* The wait for ever. */
__attribute__((noreturn)) static void watch_piece(volatile struct Params* p) {
    for (;;) {
        check_end(p);
        wait_frame();
    }
}

/* An event's place against the camera, as Jac_CreateEvent takes it: three floats. An event at the camera gets the full
   volume and the pan of the middle. */
static const u32 at_camera[3];

/* The first child of the `count` tracks at `parents` that plays, or 0. */
static u32 playing_child(const u32* parents, u32 count) {
    for (u32 i = 0; i < count; i++) {
        for (u32 n = 0; n < TRACK_CHILDREN; n++) {
            const u32 child = word(parents[i] + TRACK_FIRST_CHILD + 4 * n);
            if (child && byte(child + TRACK_STATE) != TRACK_FREE)
                return child;
        }
    }

    return 0;
}

/* The value of `port` in the port list, sign-extended from 16 bits, or 0. */
static s32 listed_port(volatile struct Params* p, u32 port) {
    s32 value = 0;
    for (u32 i = 0; i < p->port_count && i < MAX_PORTS; i++) {
        if (p->ports[i] >> 16 == port)
            value = (s32)(p->ports[i] << 16) >> 16;
    }
    return value;
}

/* One frame of a move of the formation by the port list's x and y, as the game's frame passes the player's move. */
static void move_formation(volatile struct Params* p) {
    ((fn2)p->move_formation)((u32)listed_port(p, FORMATION_X_PORT), (u32)listed_port(p, FORMATION_Y_PORT));
}

/* Steps 8 to 11: an effect. */
__attribute__((noreturn)) static void play_effect(volatile struct Params* p) {
    /* The effects' other wave groups, then a pass on the disc after them. */
    for (u32 group = EFFECTS_FIRST_GROUP; group <= EFFECTS_LAST_GROUP; group++)
        ((fn2)p->load_wave_scene)(EFFECTS_SYSTEM, group);
    for (u32 i = 0; i < sizeof(scene_systems) / sizeof(scene_systems[0]); i++)
        ((fn2)p->set_wave_scene)(scene_systems[i], 0);
    ((fn3)p->check_pass)(PASS_OF_EFFECTS, (u32)&piece_read, 0);
    for (u32 frames = 0; !piece_read; frames++) {
        if (frames > SETUP_FRAMES)
            fail(8);
        wait_frame();
    }

    /* The tracks that play the effects. The game's functions for effects drop an effect while their track is missing.
       The events' tracks are the children of the events' root track. */
    u32 system_track = 0, player_track = 0, events_track = 0;
    for (u32 frames = 0; !system_track || !player_track || !events_track; frames++) {
        if (frames > SETUP_FRAMES)
            fail(9);
        wait_frame();
        system_track = ((fn1)p->get_track_handle)(p->system_track);
        player_track = ((fn1)p->get_track_handle)(p->player_track);
        events_track = ((fn1)p->get_track_handle)(p->events_track);
    }

    /* The effect. The track that plays it becomes a child of the track for its kind. One of the system's effects plays
       one of the player's. */
    *(volatile u32*)p->scene = SCENE_OF_PLAY;
    const u32 number = EFFECT_NUMBER(p->sound_id);
    const u32 kind = EFFECT_KIND(p->sound_id);
    u32 parents[2] = {system_track, player_track};
    u32 count = 2;
    switch (kind) {
    case EFFECT_SYSTEM:
        if (number == EFFECT_CLOSES_MENU)
            *(volatile u32*)p->menu = 1;
        ((fn1)p->play_system_effect)(number);
        break;
    case EFFECT_PLAYER:
        ((fn1)p->play_player_effect)(number);
        break;
    case EFFECT_EVENT: {
        const u32 event = ((fn2)p->create_event)(EVENT_TYPE(number), (u32)at_camera);
        if (event >= TRACK_CHILDREN)
            fail(10);
        ((fn2)p->play_event_action)(event, EVENT_ACTION(number));
        parents[0] = word(events_track + TRACK_FIRST_CHILD + 4 * event);
        count = 1;
        break;
    }
    case EFFECT_FORMATION:
        /* Without a move, the formation plays nothing. */
        if (!listed_port(p, FORMATION_X_PORT) && !listed_port(p, FORMATION_Y_PORT)) {
            p->status = STATUS_FINISHED;
            for (;;)
                wait_frame();
        }
        parents[0] = ((fn1)p->get_track_handle)(p->formation_track);
        if (!parents[0])
            fail(10);
        move_formation(p);
        count = 1;
        break;
    default:
        fail(10);
    }

    p->status = STATUS_PLAYING;
    for (u32 frames = 0; !(piece_root = playing_child(parents, count)); frames++) {
        if (frames > SETUP_FRAMES)
            fail(11);
        if (kind == EFFECT_FORMATION)
            move_formation(p);
        ((fn0)p->update_camera)();
        wait_frame();
    }

    p->sound = (u32)&piece_root;
    for (;;) {
        if (kind == EFFECT_FORMATION)
            move_formation(p);
        ((fn0)p->update_camera)();
        check_end(p);
        wait_frame();
    }
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The game's start-up sets up the OS, the disc, the video interface and ARAM. So does the driver. The game's OS
       reads the flag for long file names from the disc's bi2.bin. The apploader leaves that file in memory. GCSF's
       machine leaves none. The game's disc sets the flag. Two of the game's wave archives have long names. */
    ((fn0)p->os_init)();
    *(volatile u32*)p->long_file_names = 1;
    ((fn0)p->dvd_init)();
    ((fn0)p->vi_init)();
    ((fn2)p->ar_init)(p->aram_blocks, p->aram_block_count);
    ((fn0)p->arq_init)();

    /* The sound library, with its block of the arena. Then the buffer for the disc, from two words of the game's
       system object. Only the system object's set-up fills them in, and the driver doesn't run it. The words hold 0.
       The sound library's disc thread then keeps its two 32 KB buffers. */
    const u32 lo = ((fn0)p->get_arena_lo)();
    const u32 heap = (lo + p->audio_heap_alignment - 1) & ~(p->audio_heap_alignment - 1);
    ((fn1)p->set_arena_lo)(heap + p->audio_heap_size);
    ((fn4)p->start)(heap, p->audio_heap_size, p->aram_audio_size, p->sound_path);
    const u32 system = word(p->system);
    if (!system)
        fail(2);
    ((fn2)p->add_disc_buffer)(word(system + p->disc_buffer), word(system + p->disc_buffer_size) << 6);

    /* The wait until the sound library is up. */
    for (u32 frames = 0; !word(p->boot_ok); frames++) {
        if (frames > SETUP_FRAMES)
            fail(3);
        wait_frame();
    }

    /* The piece's sequence and the wave group, then a pass on the disc after them. The pass sets `piece_read` once the
       disc has read them. A part's piece is in memory from the sound library's start-up. */
    const u32 piece = SOUND_PIECE(p->sound_id);
    if (piece < FIRST_PIECE)
        play_effect(p);
    const u32 part = SOUND_PART(p->sound_id);
    if (!part)
        ((fn1)p->ready_bgm)(piece);
    ((fn0)p->collect_aram)();
    ((fn2)p->set_wave_scene)(SOUND_SYSTEM(p->sound_id), SOUND_GROUP(p->sound_id));
    ((fn3)p->check_pass)(PASS_OF_PIECE | piece, (u32)&piece_read, 0);
    for (u32 frames = 0; !piece_read; frames++) {
        if (frames > SETUP_FRAMES)
            fail(4);
        wait_frame();
    }

    /* A part: its number, written to the port of the piece's root track. The sound starts there. The root track polls
       the port and opens the track that plays the part. */
    if (part) {
        const u32 root = ((fn1)p->get_track_handle)(p->part_track);
        if (!root)
            fail(6);
        ((fn3)p->write_port)(root, PART_PORT, part);
        p->status = STATUS_PLAYING;
        for (u32 frames = 0; !word(root + TRACK_FIRST_CHILD); frames++) {
            if (frames > SETUP_FRAMES)
                fail(7);
            wait_frame();
        }

        piece_root = word(root + TRACK_FIRST_CHILD);
        p->sound = (u32)&piece_root;
        watch_piece(p);
    }

    /* The piece, in the game's slot for music. */
    ((fn0)p->collect_aram)();
    ((fn2)p->play_bgm)(p->music_slot, piece);
    piece_root = ((fn1)p->handle_to_sequence)(p->music_handle);
    if (byte(piece_root + TRACK_STATE) == TRACK_FREE)
        fail(5);
    p->sound = (u32)&piece_root;
    p->status = STATUS_PLAYING;
    watch_piece(p);
}

/* The entry, then the port list's address. */
__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main\n\t.long driver_params + " NUMBER_TEXT(PORT_LIST_OFFSET));
}
