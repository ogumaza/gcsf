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

/* The DVD task's pass for a piece's files. The game's scene set-up makes it the same way. */
#define PASS_OF_PIECE 0x10000u

/* The port that a cutscene writes its part to, and the word of a track that holds the track's first child. The root
   track opens the track that plays the part as its first child. */
#define PART_PORT 0u
#define TRACK_FIRST_CHILD 0x44u

/* A track's state, a byte: 0 while the track is free. */
#define TRACK_STATE 0x3cu
#define TRACK_FREE 0u

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
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

#include "frames.h"

/* The flag that the disc task's pass sets once the disc has read everything before it, and the sound's track: the
   piece's root track, or the track that plays the part. The length analysis reads that track through `sound`. */
static volatile u32 piece_read;
static volatile u32 piece_root;

/* The wait for ever. The sound ends when its track is free again. */
__attribute__((noreturn)) static void watch_piece(volatile struct Params* p) {
    for (;;) {
        if (p->status == STATUS_PLAYING && byte(piece_root + TRACK_STATE) == TRACK_FREE) {
            p->sound = 0;
            p->status = STATUS_FINISHED;
        }
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

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
