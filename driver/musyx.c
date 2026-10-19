// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for games whose sound library is MusyX. It calls MusyX's functions, not a game's layer over
 * them. The driver is compiled for Gekko and stored in the game's .gcsflib. The player boots the game's start-up code:
 * the OS initialisation and the static constructors. The rip patches the game's main to branch here.
 *
 * The driver then does what a game's start-up does for MusyX. It leaves out the graphics. The profile in
 * `driver_params` gives the addresses and MusyX's settings. The ripper fills in the profile. The driver first calls
 * the start-up functions that the game calls before its sound, such as DVDInit. Then it sets up the ARAM, its queue
 * and the audio interface. The steps that follow:
 *   2. MusyX: its memory hooks, sndInit with the game's numbers of voices and its share of ARAM, the functions that the
 *      game calls after sndInit, the output mode, and sndVolume for some games;
 *   3. the header of the rip's catalogue of sounds (see docs/gcsf.md, "MusyX catalogue");
 *   4. the sound's description in the catalogue;
 *   5. the sound's groups, words and controllers in the catalogue. Then the words that the catalogue gives the sound,
 *      such as the settings of the game's reverbs, and the aux effects of studio 0;
 *   6. the sound's groups: each group's project, pool, sample directory and samples, read from the rip's files, then
 *      pushed with sndPushGroup. Groups in a row from one set of files share the files. MusyX moves the samples to
 *      ARAM;
 *   7. the sound's kind: a song that has a file, or an effect;
 *   8. a song: its file, then sndSeqPlayEx and the song's volume;
 *   9. an effect: sndFXStartEx and the effect's MIDI controllers.
 * The driver's thread then sleeps for good. The sound plays from the audio and DSP interrupts. The OS's idle loop runs
 * while no thread can. A step that fails stops the driver with its number in the status word (step 1 is the driver's
 * check of its parameter block). A file of the rip that doesn't open fails as step 10 for the catalogue, 20, 30, 40 and
 * 50 for a group's project, pool, sample directory and samples, and 60 for a song. A failed allocation for the file
 * adds 1 to that step, a read that doesn't start adds 2 and a read that fails adds 3. `driver_params.status` reports
 * the driver's progress to the player (see docs/gcsf.md).
 *
 * MusyX allocates its memory through the hooks. The driver hands out the OS's arena, from the bottom up, and never
 * takes memory back. The sound's files follow MusyX's memory in the arena.
 *
 * Build with `driver/build.py musyx` (clang). The result is committed as src/rip/driver_musyx_blob.h. Building GCSF
 * itself requires no PowerPC toolchain.
 */

#include "driver.h"

#define STARTUP_CALLS 4u
#define AFTER_INIT_CALLS 2u

/* The catalogue's header: its magic, its version, the number of sounds and where they start, the number of group
 * entries and where they start, the number of words to write and where they start. All values are big-endian. A
 * sound's settings of its MIDI controllers follow its words, in the same form. */
#define CATALOGUE_MAGIC 0x47434d58u /* 'GCMX' */
#define CATALOGUE_VERSION 1u
#define CATALOGUE_HEADER_SIZE 32u
#define SOUND_SIZE 20u
#define GROUP_SIZE 4u
#define WRITE_SIZE 8u

/* A sound's kind in the catalogue. */
#define KIND_SONG 0u
#define KIND_EFFECT 1u

/* A sound's volume of 0xff leaves the volume that MusyX gives it. A song's number of 0xffff means none. */
#define DEFAULT_VOLUME 0xffu
#define NO_SONG 0xffffu

/* sndSeqPlayEx's and sndFXStartEx's failure. */
#define INVALID_ID 0xffffffffu

/* A controller setting's first word holds the controller in its low byte. This bit sends a 14-bit value through
 * sndFXCtrl14. Without it, sndFXCtrl sends a 7-bit value. */
#define CONTROLLER_14_BIT 0x100u

/* The DVD library's states of a read. */
#define DVD_STATE_END 0
#define DVD_STATE_BUSY 1
#define DVD_STATE_WAITING 2

/* The forms of sndInit: before MusyX 1.5.1 it takes no number of studios. */
#define INIT_WITHOUT_STUDIOS 0u
#define INIT_WITH_STUDIOS 1u

/* The forms of sndSeqPlay: sndSeqPlayEx takes a studio after the parameters. */
#define SEQ_PLAY_WITHOUT_STUDIO 0u
#define SEQ_PLAY_WITH_STUDIO 1u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the sound's number in the rip's catalogue */
    u32 sound;    /* unused: MusyX keeps the sound's state itself */
    /* the OS and the disc */
    u32 os_sleep_thread;                /* void OSSleepThread(OSThreadQueue*) */
    u32 os_get_arena_lo;                /* void* OSGetArenaLo(void) */
    u32 os_get_arena_hi;                /* void* OSGetArenaHi(void) */
    u32 startup[STARTUP_CALLS];         /* functions without arguments, called first; 0 for none */
    u32 dvd_open;                       /* BOOL DVDOpen(const char*, DVDFileInfo*) */
    u32 dvd_read_async_prio;            /* BOOL DVDReadAsyncPrio(DVDFileInfo*, void*, s32, s32, callback, s32). For a
                                           game without it, the profile gives DVDReadPrio. That function waits for the
                                           read. It takes the priority in place of the callback. The driver passes 0
                                           there. */
    u32 dvd_get_command_block_status;   /* s32 DVDGetCommandBlockStatus(DVDCommandBlock*) */
    u32 dvd_close;                      /* BOOL DVDClose(DVDFileInfo*) */
    /* the ARAM and the audio interface */
    u32 ar_init;          /* u32 ARInit(u32* stack, u32 count) */
    u32 aram_blocks;      /* the ARAM allocator's block list (ARInit's stack) */
    u32 aram_block_count; /* its number of entries */
    u32 arq_init;         /* void ARQInit(void) */
    u32 ai_init;          /* void AIInit(u8* stack): 0 for no call */
    /* MusyX */
    u32 snd_set_hooks; /* void sndSetHooks(SND_HOOKS*): 0 if the library has no hooks */
    u32 snd_init;      /* s32 sndInit(voices, music, sfx, [studios,] flags, aram_size) */
    u32 init_form;     /* INIT_WITH_STUDIOS or INIT_WITHOUT_STUDIOS */
    u32 voices, music, sfx, studios, init_flags, aram_size;
    u32 after_init[AFTER_INIT_CALLS]; /* functions without arguments, called after sndInit; 0 for none */
    u32 snd_output_mode;              /* void sndOutputMode(SND_OUTPUTMODE): 0 for no call */
    u32 output_mode;
    u32 snd_push_group;  /* bool sndPushGroup(void* project, u16 group, void* samples, void* sdir, void* pool) */
    u32 snd_seq_play;    /* u32 sndSeqPlayEx(u16 group, u16 song, void* arr, SND_PLAYPARA*, [u8 studio]) */
    u32 seq_play_form;   /* SEQ_PLAY_WITH_STUDIO or SEQ_PLAY_WITHOUT_STUDIO */
    u32 snd_seq_volume;  /* void sndSeqVolume(u8 volume, u16 time, u32 seq, u8 mode) */
    u32 snd_fx_start_ex; /* u32 sndFXStartEx(u16 effect, u8 volume, u8 pan, u8 studio) */
    /* studio 0's aux effects, A then B: 0 for none */
    u32 snd_set_aux_processing_callbacks; /* void sndSetAuxProcessingCallbacks(u8 studio, callback A, void* user A,
                                             u8 midi A, u32 seq A, callback B, void* user B, u8 midi B, u32 seq B) */
    u32 aux_callback[2]; /* the effects' callbacks */
    u32 aux_user[2];     /* their parameter blocks in the game's memory */
    u32 aux_prepare[2];  /* bool prepare(void* user): 0 for no call */
    u32 aux_midi[2];     /* the MIDI channel that controls each effect, or 0xff for none. The driver reads it only
                            with sndSetAuxProcessingCallbacks. */
    /* void sndVolume(u8 volume, u16 time, u8 group): 0 for no call. MusyX starts every volume group at a volume of 0.
     * The start-ups of Metroid Prime and Metroid Prime 2 call sndVolume(127, 0, 0xff). That call gives every group its
     * full volume. A song's start sets the song's group again. Nothing else sets an effect's group. */
    u32 snd_volume;
    /* bool sndFXCtrl(u32 handle, u8 controller, u8 value) and bool sndFXCtrl14(u32 handle, u8 controller, u16 value):
     * 0 for a game whose catalogue sets no controllers */
    u32 snd_fx_ctrl;
    u32 snd_fx_ctrl14;
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

/* The OS's arena, which the driver hands out from the bottom up. */
static u32 heap_next;
static u32 heap_end;

/* MusyX's memory hooks: its malloc and its free. */
static u32 hooks[2];

/* A DVDFileInfo: the command block (0x30 bytes), the file's start and length, and the callback. */
static u32 file_info[16] __attribute__((aligned(32)));

#include "sleep.h"

/* `size` bytes of the arena, at a 32-byte boundary: the DSP and the disc require that for their transfers. Returns 0
 * when the arena is used up. */
static u32 heap_alloc(u32 size) {
    const u32 at = (heap_next + 31) & ~31u;
    if (at + size < at || at + size > heap_end)
        return 0;
    heap_next = at + size;
    return at;
}

static u32 hook_malloc(u32 size) {
    return heap_alloc(size);
}

static void hook_free(u32 address) {
    (void)address;
}

/* The path "MusyX/<number><extension>" in `out`, which holds at least 32 bytes. */
static void file_path(char* out, u32 number, const char* extension) {
    static const char prefix[] = "MusyX/";
    u32 n = 0;
    for (const char* c = prefix; *c; c++)
        out[n++] = *c;
    char digits[10];
    u32 count = 0;
    do {
        digits[count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number);
    while (count)
        out[n++] = digits[--count];
    for (const char* c = extension; *c; c++)
        out[n++] = *c;
    out[n] = 0;
}

/* Reads the rip's file at `path` into the arena. Returns its address and sets `*size`. Fails at `step` if the file
 * is missing or can't be read. */
static u32 load_file(const char* path, u32* size, u32 step) {
    volatile struct Params* p = &driver_params;
    if (!((fn2)p->dvd_open)((u32)path, (u32)file_info))
        fail(step);
    const u32 length = file_info[0x34 / 4];
    const u32 rounded = (length + 31) & ~31u;
    const u32 data = heap_alloc(rounded ? rounded : 32);
    if (!data)
        fail(step + 1);
    if (rounded) {
        if (!((fn6)p->dvd_read_async_prio)((u32)file_info, data, rounded, 0, 0, 2))
            fail(step + 2);
        for (;;) {
            const int state = (int)((fn1)p->dvd_get_command_block_status)((u32)file_info);
            if (state == DVD_STATE_END)
                break;
            if (state != DVD_STATE_BUSY && state != DVD_STATE_WAITING)
                fail(step + 3);
        }
    }
    ((fn1)p->dvd_close)((u32)file_info);
    *size = length;
    return data;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    for (u32 i = 0; i < STARTUP_CALLS; i++)
        if (p->startup[i])
            ((fn0)p->startup[i])();

    /* The ARAM, its queue and the audio interface. */
    ((fn2)p->ar_init)(p->aram_blocks, p->aram_block_count);
    ((fn0)p->arq_init)();
    if (p->ai_init)
        ((fn1)p->ai_init)(0);

    /* The arena, then MusyX. */
    heap_next = ((fn0)p->os_get_arena_lo)();
    heap_end = ((fn0)p->os_get_arena_hi)() & ~31u;
    if (p->snd_set_hooks) {
        hooks[0] = (u32)hook_malloc;
        hooks[1] = (u32)hook_free;
        ((fn1)p->snd_set_hooks)((u32)hooks);
    }
    const u32 result = p->init_form == INIT_WITH_STUDIOS
                           ? ((fn6)p->snd_init)(p->voices, p->music, p->sfx, p->studios, p->init_flags, p->aram_size)
                           : ((fn5)p->snd_init)(p->voices, p->music, p->sfx, p->init_flags, p->aram_size);
    if (result != 0)
        fail(2);
    for (u32 i = 0; i < AFTER_INIT_CALLS; i++)
        if (p->after_init[i])
            ((fn0)p->after_init[i])();
    if (p->snd_output_mode)
        ((fn1)p->snd_output_mode)(p->output_mode);
    if (p->snd_volume)
        ((fn3)p->snd_volume)(127, 0, 0xff);

    /* The catalogue and the sound's entry. */
    char path[32];
    u32 size = 0;
    const u8* catalogue = (const u8*)load_file("MusyX/sounds.bin", &size, 10);
    if (size < CATALOGUE_HEADER_SIZE || be32(catalogue) != CATALOGUE_MAGIC || be32(catalogue + 4) != CATALOGUE_VERSION)
        fail(3);
    const u32 sounds = be32(catalogue + 8), sounds_at = be32(catalogue + 12);
    const u32 groups = be32(catalogue + 16), groups_at = be32(catalogue + 20);
    const u32 writes = be32(catalogue + 24), writes_at = be32(catalogue + 28);
    if (p->sound_id >= sounds || sounds_at + sounds * SOUND_SIZE > size || groups_at + groups * GROUP_SIZE > size ||
        writes_at + writes * WRITE_SIZE > size)
        fail(4);
    const u8* sound = catalogue + sounds_at + p->sound_id * SOUND_SIZE;
    const u32 kind = sound[0], volume = sound[1], pan = sound[2], studio = sound[3];
    const u32 id = be16(sound + 4), song_group = be16(sound + 6), song = be16(sound + 8);
    const u32 first_group = be16(sound + 10), group_count = be16(sound + 12);
    const u32 first_write = be16(sound + 14), write_count = be16(sound + 16), control_count = be16(sound + 18);
    if (first_group + group_count > groups || first_write + write_count + control_count > writes)
        fail(5);
    if (control_count && (kind != KIND_EFFECT || !p->snd_fx_ctrl || !p->snd_fx_ctrl14))
        fail(5);

    /* The sound's words, then the aux effects. */
    for (u32 w = 0; w < write_count; w++) {
        const u8* write = catalogue + writes_at + (first_write + w) * WRITE_SIZE;
        *(volatile u32*)be32(write) = be32(write + 4);
    }
    if (p->snd_set_aux_processing_callbacks) {
        /* Tales of Symphonia ignores what the preparation returns. */
        for (u32 a = 0; a < 2; a++)
            if (p->aux_prepare[a])
                ((fn1)p->aux_prepare[a])(p->aux_user[a]);
        ((fn9)p->snd_set_aux_processing_callbacks)(0, p->aux_callback[0], p->aux_user[0], p->aux_midi[0], 0,
                                                   p->aux_callback[1], p->aux_user[1], p->aux_midi[1], 0);
    }

    /* The sound's groups, each from its set of files. A set's number has 16 bits, so `loaded` starts as none. */
    u32 loaded = 0xffffffffu, project = 0, pool = 0, sdir = 0, samples = 0;
    for (u32 g = 0; g < group_count; g++) {
        const u8* group = catalogue + groups_at + (first_group + g) * GROUP_SIZE;
        const u32 group_id = be16(group), set = be16(group + 2);
        if (set != loaded) {
            u32 unused;
            file_path(path, set, ".proj");
            project = load_file(path, &unused, 20);
            file_path(path, set, ".pool");
            pool = load_file(path, &unused, 30);
            file_path(path, set, ".sdir");
            sdir = load_file(path, &unused, 40);
            file_path(path, set, ".samp");
            samples = load_file(path, &unused, 50);
            loaded = set;
        }
        if (!((fn5)p->snd_push_group)(project, group_id, samples, sdir, pool))
            fail(6);
    }

    /* The sound. */
    if (kind == KIND_SONG) {
        if (song == NO_SONG)
            fail(7);
        file_path(path, song, ".sng");
        const u32 arr = load_file(path, &size, 60);
        const u32 seq = p->seq_play_form == SEQ_PLAY_WITH_STUDIO
                            ? ((fn5)p->snd_seq_play)(song_group, id, arr, 0, studio)
                            : ((fn4)p->snd_seq_play)(song_group, id, arr, 0);
        if (seq == INVALID_ID)
            fail(8);
        if (volume != DEFAULT_VOLUME)
            ((fn4)p->snd_seq_volume)(volume, 0, seq, 0);
    } else if (kind == KIND_EFFECT) {
        /* No audio frame runs between the effect's start and its controllers' settings. */
        const u32 msr = disable_interrupts();
        const u32 handle = ((fn4)p->snd_fx_start_ex)(id, volume, pan, studio);
        if (handle == INVALID_ID) {
            restore_interrupts(msr);
            fail(9);
        }
        for (u32 c = 0; c < control_count; c++) {
            const u8* control = catalogue + writes_at + (first_write + write_count + c) * WRITE_SIZE;
            const u32 controller = be32(control), value = be32(control + 4);
            if (controller & CONTROLLER_14_BIT)
                ((fn3)p->snd_fx_ctrl14)(handle, controller & 0xff, value);
            else
                ((fn3)p->snd_fx_ctrl)(handle, controller & 0xff, value);
        }
        restore_interrupts(msr);
    } else {
        fail(7);
    }
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
