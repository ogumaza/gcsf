// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Animal Crossing. The game's sound library is a port of Nintendo's sound library for the
 * Nintendo 64. The port runs the Nintendo 64's sequence player and synthesis on the CPU, inside an early JAudio1 that
 * mixes the result with the DSP. The game calls it through C functions (Na_*). The driver is compiled for Gekko and
 * stored in the game's .gcsflib. The player boots the game's start-up code: the OS initialisation and the static
 * constructors. The rip patches the game's main to branch here.
 *
 * Doubutsu no Mori+ and Doubutsu no Mori e+, the game's Japanese releases, run the driver too. In e+, the sound
 * library numbers the pieces and their sequences with 16 bits. e+'s Na_InitAudio takes other arguments.
 * Params.wide_numbers and Params.aram_size cover the differences.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The steps:
 *   2. the sound ID's kind and the size of ARInit's block list. Then the OS, its alarms, the disc, the video interface
 *      and ARAM. The video interface's retraces pace the driver as they pace the game's frames. Then the sound
 *      library, with the sound of the game's start-up screen. The game's start-up starts the library the same way.
 *      The sound library starts its threads. One of them reads the game's sound data from the disc into ARAM;
 *   3. the game's sound frame, once a retrace, until the sound library is up. The game's start-up waits the same way;
 *   4. for a piece of music, the weather, for a piece that plays the hour, then the piece, by the game's number of the
 *      piece. In the museum, the room picks the arrangement of the museum's piece. On e+'s island, the arrangement of
 *      the island's piece depends on whether the islander is there. The first part of one of Kapp'n's songs gets an
 *      empty second part. A second part plays as a piece of its own;
 *   5. the piece's sequence player, once the game's sound frame has started it;
 *   6. for an effect, the effects sequence. The game's sound frame starts that sequence once the sound library is up;
 *   7. the effect, by the game's number of the effect. The game's function for it takes a free slot for effects.
 * Then the game's sound frame runs once a frame, for ever. The driver watches the sequence player that plays the
 * piece, or the game's slot of the effect, and reports the end. A piece of a music disc plays from a stereo. Before
 * each frame, the driver places the listener at the stereo. The game places the listener once a frame too. A step that
 * fails stops the driver with its number in the status word (step 1 is the driver's check of its parameter block). A
 * read of the disc that fails in the sound library stops the driver at step 9. `driver_params.status` reports the
 * driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py animal_crossing` (clang). The result is committed as
 * src/rip/driver_animal_crossing_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* The fields of Params.sound_id: the kind of sound in the top byte, the museum's room in bits 16 to 23, the weather in
   bits 8 to 15 and the game's number of the piece in the low byte. The weather picks the arrangement of a piece that
   plays the hour. The room picks the arrangement of the museum's piece. Room 0 is outside the museum. */
#define SOUND_KIND(id) ((id) >> 24)
#define SOUND_ROOM(id) (((id) >> 16) & 0xffu)
#define SOUND_WEATHER(id) (((id) >> 8) & 0xffu)
#define SOUND_PIECE(id) ((id) & 0xffu)
#define KIND_MUSIC 0u

/* With Params.wide_numbers, a piece's ID holds the game's number of the piece in the low 16 bits, the weather in bits
   16 to 19 and the museum's room in bits 20 to 22. Bit 23 is set when the islander is on the island. */
#define WIDE_PIECE(id) ((id) & 0xffffu)
#define WIDE_WEATHER(id) (((id) >> 16) & 0xfu)
#define WIDE_ROOM(id) (((id) >> 20) & 0x7u)
#define WIDE_ISLANDER(id) (((id) >> 23) & 0x1u)

/* An effect's sound ID: 1 in the top byte, and the game's number of the effect in the low 16 bits. */
#define KIND_EFFECT 1u
#define SOUND_EFFECT(id) ((id) & 0xffffu)

/* A sequence player's first byte holds its flags. The top bit is set while the player plays. */
#define PLAYER_ENABLED 0x80u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the sound to play (see SOUND_KIND); patched by each .minigcsf */
    u32 sound;    /* the piece's sequence player or the effect's channel, then 0 (written by the driver) */
    /* the game profile: sizes and values that the game's start-up gives */
    u32 frame_retraces;       /* video retraces between the game's frames */
    u32 aram_block_count;     /* the entries of the ARAM allocator's block list (ARInit's stack) */
    u32 start_up_sound;       /* the sound of the game's start-up screen, in the game's data */
    u32 start_up_sound_size;  /* its size, as the game's start-up gives it */
    u32 players;              /* the sound library's sequence players */
    u32 player_size;          /* the size of one */
    u32 music_player;         /* the byte that holds the number of the sequence player that plays the music */
    u32 music_request;        /* the byte of the game's request to the sound frame, 0 once the frame has run it */
    /* the game profile: the addresses of the game's functions */
    u32 os_init;             /* void OSInit(void) */
    u32 os_init_alarm;       /* void OSInitAlarm(void) */
    u32 dvd_init;            /* void DVDInit(void) */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 ar_init;             /* u32 ARInit(u32* stack, u32 entries) */
    u32 arq_init;            /* void ARQInit(void) */
    u32 init_audio;          /* void Na_InitAudio(void (*fatal)(void), u8*, u32, u8* start_up, u32 size, int) */
    u32 game_frame;          /* void Na_GameFrame(void) */
    u32 check_boot;          /* u8 Na_CheckNeosBoot(void): the sound library is up */
    u32 set_weather;         /* void Na_Tenki(u8 weather) */
    u32 start_music;         /* void Na_BgmStart(u8 piece) */
    /* the game profile: the music discs */
    u32 first_disc;    /* the game's number of the first music disc's piece */
    u32 last_disc;     /* and of the last */
    u32 disc_stereo;   /* the kind of stereo that plays a disc */
    u32 place_stereo;  /* void Na_MDPlayerPos(u16 angle, float distance, u16 facing, u16 kind, u32 source) */
    /* the game profile: the museum */
    u32 set_room; /* void Na_Museum(u8 room) */
    /* the game profile: the effects */
    u32 effects_player;    /* the number of the sequence player that plays the effects sequence */
    u32 player_channels;   /* the offset in a sequence player of its 16 channels' addresses */
    u32 effect_slots;      /* the game's slots of the effects that play. A slot's first halfword holds its effect */
    u32 effect_slot_count; /* the slots. Slot n plays in channel n of the effects sequence */
    u32 effect_slot_size;  /* the size of a slot */
    u32 start_effect;      /* void Sou_TrgStart(u16, float volume, float, float pitch, u8 pan, u8 reverb, float) */
    /* the game profile: Kapp'n's songs */
    u32 music_table;       /* SEQ_TABLE: the sequence of each of the game's numbers of pieces (see piece_sequence) */
    u32 first_song;        /* the game's number of the first part of the first song */
    u32 last_song;         /* and of the last song */
    u32 first_second_part; /* the game's number of the first of the songs' second parts */
    u32 last_second_part;  /* and of the last */
    u32 empty_piece;       /* the game's number of a piece whose sequence ends at once, without a sound */
    u32 player_ports;      /* the offset in a sequence player of its 8 ports, a byte each */
    /* the game profile: Doubutsu no Mori e+ */
    u32 aram_size;    /* 0, or the ARAM that Na_InitAudio takes in e+'s form of the call (see driver_main) */
    u32 wide_numbers; /* 1 if the game numbers its pieces and their sequences with 16 bits (see WIDE_PIECE) */
    u32 set_island;   /* 0, or void Na_PrivateIslandStatus(u8 islander) */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

static unsigned short halfword(u32 address) {
    return *(volatile unsigned short*)address;
}

static void wait_retraces(u32 count) {
    for (u32 i = 0; i < count; i++)
        ((fn0)driver_params.vi_wait_for_retrace)();
}

__attribute__((noreturn)) static void fail(u32 step) {
    driver_params.status = STATUS_ERROR | step;
    for (;;)
        wait_retraces(1);
}

/* The sound library calls this when the disc can't be read. The game's start-up shows its disc error then. */
static void disc_failed(void) {
    fail(9);
}

/* ARInit's block list. */
static u32 aram_blocks[4];

/* Calls `fn` with `a` to `d` in r3 to r6 and the four floats at `f` in f1 to f4, as the game's code passes floats. The
   driver is built without floating point. A float that it passed would go in a general register. */
__attribute__((naked)) static void call_with_floats(u32 fn, const u32* f, u32 a, u32 b, u32 c, u32 d) {
    __asm__ volatile("mtctr 3\n"
                     "lfs 1, 0(4)\n"
                     "lfs 2, 4(4)\n"
                     "lfs 3, 8(4)\n"
                     "lfs 4, 12(4)\n"
                     "mr 3, 5\n"
                     "mr 4, 6\n"
                     "mr 5, 7\n"
                     "mr 6, 8\n"
                     "bctr\n");
}

/* The game starts a music disc's piece at volume 0. Once a frame, it then sets the volume and the pan of the music from
   where the listener stands, by the kind of stereo. The driver places the listener at the stereo: at an angle of 0 and
   a distance of 0. The volume is then the stereo's highest, and every channel plays in the centre. A hi-fi stereo plays
   the disc without a filter. */
static const u32 at_stereo[4] = {0, 0, 0, 0}; /* the distance, 0.0f */

static void place_listener(volatile struct Params* p) {
    call_with_floats(p->place_stereo, at_stereo, 0, 0, p->disc_stereo, 0);
}

/* The game starts an effect that has no place in the scene through Na_SysTrgStart. That function starts the effect at
   a volume, an optional volume and a pitch of 1.0, in the centre, with no reverb and at a distance of 0. The driver
   passes the same values to the function that Na_SysTrgStart calls. In every scene mode but 1, Na_SysTrgStart plays
   effect 0x424 as effect 0x16e. */
static const u32 effect_values[4] = {0x3f800000, 0x3f800000, 0x3f800000, 0}; /* 1.0f, 1.0f, 1.0f, 0.0f */
#define EFFECT_PAN 0x3fu
#define EFFECT_REVERB 0u

/* Kapp'n sings one of his songs on the boat in two parts. The game picks the first part and starts it as a piece
   (first_song to last_song). Once he sings, the game asks for one of the second parts (first_second_part to
   last_second_part). For those numbers, Sou_BgmStart starts nothing. It writes ports 0 and 1 of the first part's
   sequence player: the other player for music, and the second part's sequence. About 45 s in, the first part's script
   starts that sequence on that player. Port 0 = 0xff stands for the script's player itself. The first part's sound
   has ended about 5 s before. Its script then waits about 26 s more, while the second part plays, and ends.

   With no request, the ports hold 0 and 0 from the piece's start (Na_StaffRollStart). The script would then start
   sequence 0 on player 0, the effects' player. For a first part, the driver writes the first part's player and the
   empty piece's sequence to the ports. The first part's player then stops where the second part would start. A port
   holds a byte. The empty piece's sequence therefore has a number below 256. For a second part, the driver writes the
   second part's sequence to the empty piece's entry in SEQ_TABLE and starts the empty piece. Sou_BgmStart then plays
   the second part as it plays any piece. */
#define PORTS_THIS_PLAYER 0xffu

static void write_byte(u32 address, u32 value) {
    *(volatile unsigned char*)address = (unsigned char)value;
}

static void write_halfword(u32 address, u32 value) {
    *(volatile unsigned short*)address = (unsigned short)value;
}

/* The sequence of the game's piece `piece` in SEQ_TABLE. An entry takes a byte, or 16 bits with wide_numbers. */
static u32 piece_sequence(volatile struct Params* p, u32 piece) {
    return p->wide_numbers ? halfword(p->music_table + 2 * piece) : byte(p->music_table + piece);
}

static void set_piece_sequence(volatile struct Params* p, u32 piece, u32 sequence) {
    if (p->wide_numbers)
        write_halfword(p->music_table + 2 * piece, sequence);
    else
        write_byte(p->music_table + piece, sequence);
}

static void end_after_first_part(volatile struct Params* p, u32 player) {
    write_byte(player + p->player_ports, PORTS_THIS_PLAYER);
    write_byte(player + p->player_ports + 1, piece_sequence(p, p->empty_piece));
}

/* An effect. The game's sound frame starts the effects sequence once the sound library is up. The sequence's first
   channels each play one effect at a time. The game's function for an effect takes a free slot. The effect then plays
   in the channel with the slot's number. The channel's port 5 is nonzero while the effect's first layer plays. The
   game's sound frame frees the slot once the port drops to 0. */
__attribute__((noreturn)) static void play_effect(volatile struct Params* p) {
    const u32 effects = p->players + p->effects_player * p->player_size;
    for (u32 frames = 0; !(byte(effects) & PLAYER_ENABLED); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        wait_retraces(p->frame_retraces);
        ((fn0)p->game_frame)();
    }

    call_with_floats(p->start_effect, effect_values, SOUND_EFFECT(p->sound_id), EFFECT_PAN, EFFECT_REVERB, 0);
    u32 slot = p->effect_slots;
    u32 n = 0;
    while (n < p->effect_slot_count && !halfword(slot)) {
        n++;
        slot += p->effect_slot_size;
    }
    if (n == p->effect_slot_count)
        fail(7);
    p->sound = word(effects + p->player_channels + 4 * n);
    p->status = STATUS_PLAYING;

    for (;;) {
        wait_retraces(p->frame_retraces);
        ((fn0)p->game_frame)();
        if (p->status == STATUS_PLAYING && !halfword(slot)) {
            p->sound = 0;
            p->status = STATUS_FINISHED;
        }
    }
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);
    const u32 kind = SOUND_KIND(p->sound_id);
    if (kind != KIND_MUSIC && kind != KIND_EFFECT)
        fail(2);
    if (p->aram_block_count > sizeof(aram_blocks) / sizeof(aram_blocks[0]))
        fail(2);

    /* The OS, its alarms, the disc, the video interface and ARAM, as the game's start-up sets them up. */
    ((fn0)p->os_init)();
    ((fn0)p->os_init_alarm)();
    ((fn0)p->dvd_init)();
    ((fn0)p->vi_init)();
    ((fn2)p->ar_init)((u32)aram_blocks, p->aram_block_count);
    ((fn0)p->arq_init)();

    /* The sound library, with the sound of the start-up screen. The game's start-up gives no data to copy in advance:
       the sound library reads the game's sound data from the disc. The sound library would then play the start-up
       screen's sound, for about 7 s. The rip patches that out (see the game's profile). e+'s Na_InitAudio takes no
       data to copy. It takes the start-up screen's sound, its size and the ARAM for the sound library. e+'s main works
       out that size from the size of the game's sound data. */
    if (p->aram_size)
        ((fn4)p->init_audio)((u32)disc_failed, p->start_up_sound, p->start_up_sound_size, p->aram_size);
    else
        ((fn6)p->init_audio)((u32)disc_failed, 0, 0, p->start_up_sound, p->start_up_sound_size, 0);
    for (u32 frames = 0; !((fn0)p->check_boot)(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(3);
        wait_retraces(1);
        ((fn0)p->game_frame)();
    }

    if (kind == KIND_EFFECT)
        play_effect(p);

    /* The piece. The game's sound frame runs the request: it picks the sequence player and sends the sound library
       the commands that start the piece there. The sound starts at that frame. The sequence player starts at the
       sound library's next audio frame. */
    const u32 id = p->sound_id;
    u32 piece = p->wide_numbers ? WIDE_PIECE(id) : SOUND_PIECE(id);
    const u32 disc = piece >= p->first_disc && piece <= p->last_disc;
    const u32 first_part = piece >= p->first_song && piece <= p->last_song;
    if (piece >= p->first_second_part && piece <= p->last_second_part) {
        set_piece_sequence(p, p->empty_piece, piece_sequence(p, piece));
        piece = p->empty_piece;
    }
    ((fn1)p->set_weather)(p->wide_numbers ? WIDE_WEATHER(id) : SOUND_WEATHER(id));
    /* e+'s island. The game's start of the island's piece mutes some channels unless the islander is there. */
    if (p->set_island)
        ((fn1)p->set_island)(WIDE_ISLANDER(id));
    ((fn1)p->start_music)(piece);
    wait_retraces(p->frame_retraces);
    ((fn0)p->game_frame)();
    if (byte(p->music_request))
        fail(4);
    const u32 player = p->players + byte(p->music_player) * p->player_size;
    p->sound = player;
    p->status = STATUS_PLAYING;

    /* The museum's room. The game sets it a frame or more after it has started the piece. */
    const u32 room = p->wide_numbers ? WIDE_ROOM(id) : SOUND_ROOM(id);
    if (room)
        ((fn1)p->set_room)(room);

    /* The game's sound frame, for ever. The piece ends when its sequence player stops. */
    u32 started = 0;
    for (u32 frames = 0;; frames++) {
        wait_retraces(p->frame_retraces);
        if (disc)
            place_listener(p);
        ((fn0)p->game_frame)();
        const u32 enabled = byte(player) & PLAYER_ENABLED;
        if (!started) {
            started = enabled;
            if (started && first_part)
                end_after_first_part(p, player);
            if (!started && frames > SETUP_FRAMES)
                fail(5);
        } else if (p->status == STATUS_PLAYING && !enabled) {
            p->sound = 0;
            p->status = STATUS_FINISHED;
        }
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
