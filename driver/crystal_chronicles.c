// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Final Fantasy Crystal Chronicles. The game's sound library is Square's Red driver on the SDK's
 * AX, under a thin layer of the game's code. The driver is compiled for Gekko and stored in the game's .gcsflib. The
 * player boots the game's start-up code: the OS initialisation and the static constructors. The OS initialisation also
 * sets up the SDK's disc library. The rip patches the game's main to branch here.
 *
 * The driver then does what the game's system set-up does for sound. It leaves out the graphics and the controllers.
 * The profile in `driver_params` gives the addresses. The ripper fills in the profile. The steps:
 *   - the game's memory manager. Its heap covers the OS's arena. The rip moves the heap's top down to the arena's top,
 *     below the driver (see the profile);
 *   - the game's sound manager. It sets up the ARAM, the audio interface, AX and Red, with Red's threads;
 *   - the game's file manager. It reads the disc;
 *   - for a piece of music, the piece's wave bank. The piece's file names the bank. The game's scripts load that bank
 *     before the piece. The game's loader reads the bank from the disc and hands it to Red. Red moves the bank's
 *     samples to ARAM. Then the piece, through the game's loader, and the game's play function;
 *   - for an effect, the blocks of effects and the two wave banks that the game keeps loaded. The game loads them as
 *     it starts. They take Red's first two slots for banks. An effect's track can pick a wave from either slot,
 *     whatever bank the effect's file names. For an effect in a SEP file, the effect's wave bank and the file come
 *     next. The file names the bank. Then the game's function that plays an effect, with the arguments that the
 *     game's code passes.
 * The driver's thread then sleeps for good. The sound plays on Red's threads. The audio interrupt drives them. The OS's
 * idle loop runs while no thread can. A step that fails stops the driver with its number in the status word: 1 for
 * the driver's check of its parameter block, 2 for its check of the sound ID, 3 for a file that names the sound's wave
 * bank and doesn't open, and 4 for such a file without the magic of its kind. `driver_params.status` reports the
 * driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py crystal_chronicles` (clang). The result is committed as
 * src/rip/driver_crystal_chronicles_blob.h. Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* A sound ID's kind: its top byte. Its low 24 bits are the sound's number. */
#define KIND_MUSIC 0u
#define KIND_EFFECT 1u

/* An effect's number is the game's. Below 4000, it's the block's number times 1000 plus the effect's place in the
 * block. From 4000, it's the number of the effect's SEP file. */
#define FIRST_SEP 4000u

/* A piece's file: the magic "BGM " at 0, then the piece's number and its wave bank, big-endian halves at 4 and 6. */
#define BGM_MAGIC 0x42474d20u
#define BGM_BANK_HIGH 6u
#define BGM_BANK_LOW 7u

/* An effect's SEP file: the magic "SeSep" at 0. Its wave bank's number has its low byte at 0x11 and its high byte at
 * 0x12. */
#define SEP_MAGIC 0x53655365u /* "SeSe" */
#define SEP_MAGIC_LAST 0x70u  /* the "p" at 4 */
#define SEP_BANK_LOW 0x11u
#define SEP_BANK_HIGH 0x12u

/* The game's arguments to its function that plays an effect: the middle of the pan, full volume, no fade-in. */
#define EFFECT_PAN 0x40u
#define EFFECT_VOLUME 0x7fu
#define EFFECT_FADE 0u

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the sound's kind in the top byte (see KIND_MUSIC), its number in the low 24 bits */
    u32 sound;    /* unused: Red keeps the sound's state itself */
    /* the OS */
    u32 os_sleep_thread; /* void OSSleepThread(OSThreadQueue*) */
    /* the game's managers: each one's set-up, called with its object */
    u32 memory_init;
    u32 memory_manager;
    u32 sound_init;
    u32 sound_manager;
    u32 file_init;
    u32 file_manager;
    /* the file manager's functions. A handle's data arrives in the manager's buffer, a pointer at file_manager + 8 */
    u32 file_open;  /* handle* open(file_manager, const char* path, u32, u32 priority): 0 if the file is missing */
    u32 file_read;  /* void read(file_manager, handle*): the whole file */
    u32 file_sync;  /* void sync(file_manager, handle*): waits for the read */
    u32 file_close; /* void close(file_manager, handle*) */
    u32 sprintf;    /* int sprintf(char*, const char*, ...) */
    u32 music_path; /* the game's format of a piece's path, with the piece's number */
    /* the sound manager's functions, each called with sound_manager */
    u32 load_wave; /* void loadWave(sound_manager, int bank) */
    u32 load_bgm;  /* void loadBgm(sound_manager, int piece) */
    u32 play_bgm;  /* void playBgm(sound_manager, int piece) */
    u32 effect_path; /* the game's format of an effect's SEP file's path, with the effect's number */
    u32 load_block;  /* void loadBlock(sound_manager): the blocks of effects and the banks that the game keeps */
    u32 load_se;     /* void loadSe(sound_manager, int effect): an effect's SEP file */
    u32 play_se;     /* int playSe(sound_manager, int effect, int pan, int volume, int fade_in) */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

typedef int (*sprintf_fn)(char*, const char*, ...);

#include "sleep.h"

/* The wave bank that a file names: the bank's number in the bytes at `high` and `low`. The file starts with the word
 * `magic`. A SEP file also has `magic_last` at 4. The file manager reads the file, as the game's loaders do. */
static u32 file_bank(const char* path, u32 magic, u32 magic_last, u32 high, u32 low) {
    volatile struct Params* p = &driver_params;
    const u32 handle = ((fn4)p->file_open)(p->file_manager, (u32)path, 0, 0);
    if (!handle)
        fail(3);
    ((fn2)p->file_read)(p->file_manager, handle);
    ((fn2)p->file_sync)(p->file_manager, handle);
    const u8* data = *(const u8* volatile*)(p->file_manager + 8);
    const u32 start = be32(data);
    const u32 last = data[4];
    const u32 bank = (u32)data[high] << 8 | data[low];
    ((fn2)p->file_close)(p->file_manager, handle);
    if (start != magic || (magic_last && last != magic_last))
        fail(4);
    return bank;
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);
    const u32 kind = p->sound_id >> 24;
    const u32 number = p->sound_id & 0xffffff;
    if (kind != KIND_MUSIC && kind != KIND_EFFECT)
        fail(2);

    /* The managers, in the order of the game's system set-up. */
    ((fn1)p->memory_init)(p->memory_manager);
    ((fn1)p->sound_init)(p->sound_manager);
    ((fn1)p->file_init)(p->file_manager);

    char path[64];
    if (kind == KIND_MUSIC) {
        /* The piece's wave bank, then the piece. */
        ((sprintf_fn)p->sprintf)(path, (const char*)p->music_path, number);
        ((fn2)p->load_wave)(p->sound_manager, file_bank(path, BGM_MAGIC, 0, BGM_BANK_HIGH, BGM_BANK_LOW));
        ((fn2)p->load_bgm)(p->sound_manager, number);
        ((fn2)p->play_bgm)(p->sound_manager, number);
    } else {
        /* The blocks and the two banks, as the game loads them at its start. Effects in SEP files use the two banks
         * too. For such an effect, its wave bank and its file come next. Then the effect. */
        ((fn1)p->load_block)(p->sound_manager);
        if (number >= FIRST_SEP) {
            ((sprintf_fn)p->sprintf)(path, (const char*)p->effect_path, number);
            ((fn2)p->load_wave)(p->sound_manager,
                                file_bank(path, SEP_MAGIC, SEP_MAGIC_LAST, SEP_BANK_HIGH, SEP_BANK_LOW));
            ((fn2)p->load_se)(p->sound_manager, number);
        }
        ((fn5)p->play_se)(p->sound_manager, number, EFFECT_PAN, EFFECT_VOLUME, EFFECT_FADE);
    }
    p->status = STATUS_PLAYING;

    sleep_forever();
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
