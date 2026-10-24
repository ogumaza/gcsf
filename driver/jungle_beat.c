// SPDX-License-Identifier: MIT

/*
 * GCSF guest driver for Donkey Kong Jungle Beat. The game's sound library is JAudio2, the sound library of Nintendo
 * EAD's JSystem. The driver is compiled for Gekko and stored in the game's .gcsflib. The player boots the game's
 * start-up code: the OS initialisation and the static constructors. The rip patches the game's main to branch here.
 *
 * The driver calls the game's functions to do what the game's start-up does for sound, without its graphics. The
 * profile in `driver_params` gives their addresses. The ripper fills in the profile. The driver starts the video
 * interface first. Its retraces pace the driver as they pace the game's frames. The steps that follow:
 *   2. JSystem's root heap, then the game's heaps: the system heap, the game heap and the audio heap. The game heap
 *      becomes the current heap;
 *   3. the ARAM manager and the main thread's JKRThread;
 *   4. the sound files, loaded the way the game's audio object loads them: the start-up archive into the game heap,
 *      decoded from Yaz0, and the sequence archive, the chord archive and the chord database into the audio heap;
 *   5. the audio manager, created the way the game's audio object creates it once the files are in. The sound library
 *      loads the waves that the start-up archive marks as staying loaded;
 *   6. the waves that the .minigcsf names, one group of a wave bank each. The game loads its waves per stage, and its
 *      waves don't all fit in ARAM;
 *   7. the sound, by its JAudio sound ID. Music starts in the game's BGM player, the way the game starts it. A chord
 *      effect plays over a stage's music with the music's tracks off (see play_over_music). The sound manager starts
 *      other sounds.
 * Then the audio manager's frame process runs once a frame, forever. While a chord effect plays, the rhythm's frame
 * process follows it. A step that fails stops the driver with its number in the status word (step 1 is the driver's
 * check of its parameter block). `driver_params.status` reports the driver's progress to the player (see docs/gcsf.md).
 *
 * Build with `driver/build.py jungle_beat` (clang). The result is committed as src/rip/driver_jungle_beat_blob.h.
 * Building GCSF itself requires no PowerPC toolchain.
 */

#include "driver.h"

/* A wave archive's state once it is in ARAM. */
#define WAVES_LOADED 2u

/* JKRDvdRipper's options: decode Yaz0, and allocate from a heap's head or tail. */
#define EXPAND 1u
#define FROM_HEAD 1u
#define FROM_TAIL 2u

/* The chord effects. The chord database's table of records holds their count, then the records. A record holds its
   entry, then its effect's sound ID. The audio manager's chord effects hold the database at +8. The database holds the
   table at +0xc. The chord effects hold their players from +0xc. A player's byte at +0x18 is set from its start until
   the game stops it. Its byte at +0x19 is set while a step of its effect waits for a beat. Its sound object at +0x34
   plays the steps. The object holds its JAISoundHandles at +0 and their number at +4. */
#define NO_RECORD 0xffffffffu
#define RECORD_SIZE 16u
#define CHORD_DATABASE 0x8u
#define RECORD_TABLE 0xcu
#define CHORD_PLAYER_LIST 0xcu
#define CHORD_PLAYERS 64u
#define PLAYER_BUSY 0x18u
#define PLAYER_WAITING 0x19u
#define PLAYER_SOUND_OBJECT 0x34u

/* The effects that some stages play in place of another effect (Params.swaps). */
#define SWAPS 2u

/* The stage object holds the stage's number at +4 and its row of the stage table at +0xc. */
#define STAGE_NUMBER 0x4u
#define STAGE_ROW 0xcu

struct Params {
    u32 magic;
    u32 version;
    u32 status;   /* written by the driver */
    u32 sound_id; /* the JAudio sound ID to play; patched by each .minigcsf */
    u32 sound;    /* the sound's JAISoundHandle (written by the game): its JAISound while it plays, and 0 after */
    /* the waves that the sound uses, patched by each .minigcsf: (wave bank << 16) | group */
    u32 wave_count;
    u32 waves[MAX_WAVES];
    /* the game profile: the sizes that the game's start-up gives */
    u32 frame_retraces;  /* video retraces between the frame process's calls */
    u32 aram_audio_size; /* ARAM for the sound library */
    u32 aram_graph_size; /* ARAM for graphics */
    u32 aram_priorities; /* the ARAM manager's three thread priorities, a byte each, from bit 16 down */
    u32 thread_size;     /* a JKRThread, for the main thread's */
    u32 archive_size;    /* a JKRMemArchive */
    /* the game profile: the addresses of the game's functions and variables */
    u32 vi_init;             /* void VIInit(void) */
    u32 vi_wait_for_retrace; /* void VIWaitForRetrace(void) */
    u32 get_current_thread;  /* OSThread* OSGetCurrentThread(void) */
    u32 start_jsystem;       /* the game's first step: OSInit, DVDInit and JKRExpHeap::createRoot */
    u32 create_heaps;        /* the game's heaps: it keeps them in the variable `heaps` */
    u32 heaps;               /* the variable that holds the game's heaps: the game heap at +0, the audio heap at +8 */
    u32 become_current_heap; /* JKRHeap* JKRHeap::becomeCurrentHeap() */
    u32 current_heap;        /* the variable that holds JSystem's current heap */
    u32 create_aram;         /* JKRAram* JKRAram::create(u32 audio, u32 graph, s32, s32, s32) */
    u32 alloc;               /* void* operator new(u32 size) from the current heap */
    u32 alloc_from;          /* void* operator new(u32 size, JKRHeap*, int alignment) */
    u32 alloc_array_from;    /* void* operator new[](u32 size, JKRHeap*, int alignment) */
    u32 free_array;          /* void operator delete[](void*) */
    u32 thread_ctor;         /* JKRThread::JKRThread(OSThread*, int message_count) */
    u32 path_to_entry;       /* s32 DVDConvertPathToEntrynum(const char*) */
    u32 file_size;           /* the game's file size: u32 (const char* path, bool), rounded up to 32 bytes */
    u32 load_file;           /* JKRDvdRipper::loadToMainRAM(const char*, u8*, expand, u32, JKRHeap*, direction, ...) */
    u32 sound_archive_path;  /* the game's paths of its sound files */
    u32 sequence_archive_path;
    u32 chord_archive_path;
    u32 chord_database_path;
    u32 archive_ctor;         /* JKRMemArchive::JKRMemArchive(void*, u32, JKRMemBreakFlag) */
    u32 create_audio_manager; /* the game's audio manager: (JKRHeap*, void* baa, JKRArchive* sequences,
                                 JKRArchive* chords, void* chord_database) */
    u32 audio_frame;          /* the audio manager's frame process */
    u32 section_heap;         /* the variable that holds the sound library's section heap */
    u32 wave_banks_offset;    /* the section heap's table of wave banks */
    u32 load_wave_arcs;       /* bool JAUSection::loadWaveArc(u32 bank, u32 mask): a wave bank's archives in `mask` */
    u32 wave_arc_state_offset; /* a wave archive's state (2 once it is in ARAM), and its disc entry after it */
    /* the game profile: the audio manager's BGM player. It starts music */
    u32 music_section;       /* the top byte of a music sound ID */
    u32 music_player_offset; /* the player in the audio manager. Its vtable gives start(JAISoundID*) at 8, and its
                                sound's JAISoundHandle is at +4 */
    u32 music_tables;        /* 12 bytes per BGM, by the low half of its ID: the indices of its tables of track
                                states, -1 for none. The second table's index is at +4 */
    u32 music_states;        /* the second table: 8 states of 8 bytes per index */
    u32 set_music_state;     /* void (player, int state, bool at_once): applies a state of the second table */
    /* the game profile: the chord effects. A record of the chord database starts an effect whose keys follow the
       chords of the music that plays */
    u32 chord_offset;       /* the audio manager's pointer to its chord effects */
    u32 chord_records;      /* the number of the database's records. Its count is larger */
    u32 start_chord_effect; /* bool startChordSe(chords, u32 record, void* object, u32, float volume) */
    u32 stage_offset;       /* the audio manager's pointer to its stage object */
    u32 start_stage_music;  /* void (stage): starts the music of the stage's row and lets chord effects start */
    u32 set_track_states;   /* void (player, const u8 codes[8], s32 time): a code for each track, as in a state */
    u32 rhythm_offset;      /* the audio manager's rhythm. It follows the music's beats by the time base */
    u32 rhythm_frame;       /* void (rhythm): the rhythm's frame process. An object of the game's scenes runs it */
    u32 swapped_effect;     /* an effect whose records some stages play with another effect */
    u32 swaps[SWAPS * 2];   /* each such other effect, then its stage */
    /* written by the driver */
    u32 music; /* the JAISound of the music that a chord effect plays over, or 0 */
};

__attribute__((section(".params"), used)) volatile struct Params driver_params = {
    .magic = PARAMS_MAGIC,
    .version = PARAMS_VERSION,
};

/* The tracks of a BGM that its states set. */
#define MUSIC_TRACKS 16u

#include "aram_manager.h"
#include "frames.h"

/* A wave archive of the listed `wave`: (wave bank << 16) | group. A bank's vtable gives its archive at slot 0x10 and
   the number of its archives at slot 0x14. Returns 0 if the bank or the group doesn't exist. */
static u32 wave_arc(u32 wave) {
    volatile struct Params* p = &driver_params;
    const u32 bank = word(word(p->section_heap) + p->wave_banks_offset + (wave >> 16) * 4);
    if (!bank || (wave & 0xffff) >= ((fn1)virtual_function(bank, 0x14))(bank))
        return 0;
    return ((fn2)virtual_function(bank, 0x10))(bank, wave & 0xffff);
}

/* Whether every listed wave archive is in ARAM. An archive without a file on the disc (entry number below 0) is never
   loaded. */
static int waves_loaded(void) {
    volatile struct Params* p = &driver_params;
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        const u32 arc = wave_arc(p->waves[i]);
        if (arc && (s32)word(arc + p->wave_arc_state_offset + 4) >= 0 &&
            word(arc + p->wave_arc_state_offset) != WAVES_LOADED)
            return 0;
    }
    return 1;
}

/* Whether the first state of the music `id` in the BGM player's second table turns every track off. A state holds
   a code for each track in a nibble, high nibble first. The player ignores the code's bit 3. Codes 0 and 2 turn a
   track off, codes 1 and 3 turn it on, and code 7 leaves it as it was. */
static int starts_silent(u32 id) {
    volatile struct Params* p = &driver_params;
    const s32 index = (s32)word(p->music_tables + (id & 0xffff) * 12 + 4);
    if (index < 0)
        return 0;
    const u32 state = p->music_states + (u32)index * 0x40;
    for (u32 track = 0; track < MUSIC_TRACKS; track++) {
        const u32 code = (byte(state + track / 2) >> ((track & 1) ? 0 : 4)) & 7;
        if (code != 0 && code != 2)
            return 0;
    }
    return 1;
}

/* Whether the disc has a file at the game's `path`. */
static int on_disc(u32 path) {
    return (s32)((fn1)driver_params.path_to_entry)(path) >= 0;
}

/* Loads the file at the game's `path` into a buffer from `heap`. The game's audio object loads its sound files the
   same way. */
static u32 load_into(u32 path, u32 heap) {
    volatile struct Params* p = &driver_params;
    const u32 size = ((fn2)p->file_size)(path, 0);
    const u32 buffer = size ? ((fn3)p->alloc_array_from)(size, heap, 0x20) : 0;
    if (!buffer || !((fn9)p->load_file)(path, buffer, EXPAND, 0, 0, FROM_HEAD, 0, 0, 0))
        return 0;
    return buffer;
}

/* A JKRMemArchive of the archive at `data`, from `heap`. */
static u32 mount(u32 data, u32 heap) {
    volatile struct Params* p = &driver_params;
    const u32 archive = ((fn3)p->alloc_from)(p->archive_size, heap, 0);
    if (archive)
        ((fn4)p->archive_ctor)(archive, data, 0, 0);
    return archive;
}

/* The first record of the chord database that starts effect `id`, or NO_RECORD. */
static u32 chord_record(u32 chords, u32 id) {
    volatile struct Params* p = &driver_params;
    if (!chords || !p->chord_records)
        return NO_RECORD;
    const u32 table = word(word(chords + CHORD_DATABASE) + RECORD_TABLE);
    const u32 count = word(table) < p->chord_records ? word(table) : p->chord_records;
    for (u32 record = 0; record < count; record++) {
        if (word(table + 4 + record * RECORD_SIZE + 4) == id)
            return record;
    }
    return NO_RECORD;
}

/* The first busy player of the chord effects, or 0. */
static u32 busy_player(u32 chords) {
    const u32 players = word(chords + CHORD_PLAYER_LIST);
    for (u32 i = 0; i < CHORD_PLAYERS; i++) {
        const u32 player = word(players + i * 4);
        if (player && (byte(player + PLAYER_BUSY) || byte(player + PLAYER_WAITING)))
            return player;
    }
    return 0;
}

/* The JAISound of the step that a chord effect's `player` plays, or 0 between steps. */
static u32 step_sound(u32 player) {
    const u32 object = word(player + PLAYER_SOUND_OBJECT);
    const u32 handles = word(object);
    for (u32 i = 0; i < word(object + 4); i++) {
        const u32 sound = word(handles + i * 4);
        if (sound)
            return sound;
    }
    return 0;
}

/* Calls startChordSe at `fn` for `record` of `chords`, with no object, at the volume whose bits are at `volume`. The
   game's code passes the chord effects in r3, the record in r4, the object in r5, 0 in r6 and the volume in f1. The
   driver is built without floating point. A float that it passed would go in a general register. */
__attribute__((naked)) static u32 call_start_chord(u32 fn, const u32* volume, u32 chords, u32 record) {
    __asm__ volatile("mtctr 3\n"
                     "lfs 1, 0(4)\n"
                     "mr 3, 5\n"
                     "mr 4, 6\n"
                     "li 5, 0\n"
                     "li 6, 0\n"
                     "bctr\n");
}

/* The bits of 1.0f: the full volume that the game gives its chord effects. */
static const u32 full_volume = 0x3f800000u;

/* One frame of the game with music, and the music's JAISound copied to `music`. The rhythm gives the chord effects
   the music's beats. The game's scenes run the rhythm's frame process. The driver runs it after the audio manager's. */
static void music_frame(u32 manager, u32 music_player) {
    volatile struct Params* p = &driver_params;
    ((fn1)p->audio_frame)(manager);
    ((fn1)p->rhythm_frame)(manager + p->rhythm_offset);
    p->music = word(music_player + 4);
    wait_frame();
}

/* Each track's code 2, for set_track_states: off at once. The BGM player sets the track's volume to 0 and mutes it.
   The track still runs. */
static const u8 tracks_off[MUSIC_TRACKS / 2] = {0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22};

/* Plays the chord effect of `record`. Its keys follow the chords of the music that plays. The stage's code starts the
   music of stage `stage_number`'s row. The music's tracks then go off. The music runs on in silence and gives the
   effect its chords and beats. The game plays the effect only while such music plays.

   The chord effects' code works out the keys of each step, writes them to ports of the step's sound and starts the
   sound. A step can wait for a beat of the music. The driver reports each step's JAISound, and the last one while the
   next step waits. The effect ends when no step sounds or waits. Later in a combo, the game moves some effects on by
   more steps. The player stays busy for those. The status turns 1 when the first step sounds. */
__attribute__((noreturn)) static void play_over_music(u32 manager, u32 record, u32 stage_number) {
    volatile struct Params* p = &driver_params;
    const u32 stage = word(manager + p->stage_offset);
    if (!stage)
        fail(7);
    if (stage_number) {
        *(volatile u32*)(stage + STAGE_NUMBER) = stage_number;
        *(volatile u32*)(stage + STAGE_ROW) = stage_number;
    }
    ((fn1)p->start_stage_music)(stage);
    const u32 music_player = manager + p->music_player_offset;
    p->music = word(music_player + 4);
    if (!p->music)
        fail(7);
    ((fn3)p->set_track_states)(music_player, (u32)tracks_off, (u32)-1);

    /* The effect. The chord effects' code refuses an effect until the music's chords are in. */
    const u32 chords = word(manager + p->chord_offset);
    u32 player = 0;
    for (u32 frames = 0;; frames++) {
        call_start_chord(p->start_chord_effect, &full_volume, chords, record);
        player = busy_player(chords);
        if (player)
            break;
        if (frames > SETUP_FRAMES)
            fail(7);
        music_frame(manager, music_player);
    }

    /* The first step's sound. */
    u32 sound = 0;
    for (u32 frames = 0; !(sound = step_sound(player)); frames++) {
        if (frames > SETUP_FRAMES)
            fail(7);
        music_frame(manager, music_player);
    }
    p->sound = sound;
    p->status = STATUS_PLAYING;

    for (;;) {
        music_frame(manager, music_player);
        const u32 now = step_sound(player);
        if (now)
            sound = now;
        else if (!byte(player + PLAYER_WAITING))
            sound = 0;
        p->sound = sound;
        if (p->status == STATUS_PLAYING && sound == 0)
            p->status = STATUS_FINISHED;
    }
}

__attribute__((noreturn)) void driver_main(void) {
    volatile struct Params* p = &driver_params;
    if (p->magic != PARAMS_MAGIC || p->version != PARAMS_VERSION)
        fail(1);

    /* The video interface, for the retraces. */
    ((fn0)p->vi_init)();

    /* JSystem's root heap, then the game's heaps. The game heap becomes the current heap. */
    ((fn0)p->start_jsystem)();
    ((fn0)p->create_heaps)();
    const u32 heaps = word(p->heaps);
    const u32 game_heap = heaps ? word(heaps) : 0;
    const u32 audio_heap = heaps ? word(heaps + 8) : 0;
    if (!game_heap || !audio_heap)
        fail(2);
    ((fn1)p->become_current_heap)(game_heap);

    /* The ARAM manager and the main thread's JKRThread. */
    create_aram_manager();
    const u32 thread = ((fn1)p->alloc)(p->thread_size);
    if (thread)
        ((fn3)p->thread_ctor)(thread, ((fn0)p->get_current_thread)(), 4);

    /* The sound files. The start-up archive goes to the game heap's tail, decoded. */
    if (!on_disc(p->sound_archive_path) || !on_disc(p->sequence_archive_path) || !on_disc(p->chord_archive_path) ||
        !on_disc(p->chord_database_path))
        fail(4);
    const u32 baa =
        ((fn9)p->load_file)(p->sound_archive_path, 0, EXPAND, 0, game_heap, FROM_TAIL, 0, 0, 0);
    const u32 sequences = load_into(p->sequence_archive_path, audio_heap);
    const u32 chords = load_into(p->chord_archive_path, audio_heap);
    const u32 chord_database = load_into(p->chord_database_path, audio_heap);
    if (!baa || !sequences || !chords || !chord_database)
        fail(4);

    /* The audio manager, from the audio heap. The archives' objects come from the game heap. The start-up archive's
       copy is freed once the sound library has read it. */
    const u32 last_heap = word(p->current_heap);
    ((fn1)p->become_current_heap)(audio_heap);
    const u32 sequence_archive = mount(sequences, game_heap);
    const u32 chord_archive = mount(chords, game_heap);
    if (!sequence_archive || !chord_archive)
        fail(5);
    const u32 manager =
        ((fn5)p->create_audio_manager)(audio_heap, baa, sequence_archive, chord_archive, chord_database);
    ((fn1)p->free_array)(baa);
    ((fn1)p->become_current_heap)(last_heap);
    if (!manager)
        fail(5);

    /* A chord effect plays over a stage's music (see play_over_music). The driver looks up the effect's record before
       the waves load. The work between the last frame process and the start of another sound then stays as it was.
       The timing of a start against the DSP's frames sets the samples of the output. Some stages play another effect
       in place of `swapped_effect`, with the records of `swapped_effect`. Such an effect plays at its stage. Every
       other chord effect plays at stage 0, the first row of the stage table. */
    const u32 id = p->sound_id;
    u32 effect = id;
    u32 stage_number = 0;
    for (u32 i = 0; i < SWAPS; i++) {
        if (id == p->swaps[i * 2]) {
            effect = p->swapped_effect;
            stage_number = p->swaps[i * 2 + 1];
        }
    }
    const u32 record = chord_record(word(manager + p->chord_offset), effect);

    /* The listed waves. The section loads the archives of a bank that a mask's bits pick. A group above 31 therefore
       can't load. Every wave bank of the game has one group. The frame process runs while the archives load. */
    const u32 section = word(p->section_heap);
    for (u32 i = 0; i < p->wave_count && i < MAX_WAVES; i++) {
        if ((p->waves[i] & 0xffff) < 32)
            ((fn3)p->load_wave_arcs)(section, p->waves[i] >> 16, 1u << (p->waves[i] & 0xffff));
    }
    for (u32 frames = 0; !waves_loaded(); frames++) {
        if (frames > SETUP_FRAMES)
            fail(6);
        ((fn1)p->audio_frame)(manager);
        wait_frame();
    }

    /* The sound. The BGM player starts music and sets the tracks to the music's first states. Some tracks start off.
       The game turns them on later, or never. A BGM whose first state turns every track off gets its next state at
       once. The game turns that BGM's tracks on later. The sound manager starts other sounds. Its vtable gives
       startSound(JAISoundID*, JAISoundHandle*, const TVec3*) at 0xc. The player holds its sound's handle. The driver
       copies the handle to `sound` once a frame. */
    if (record != NO_RECORD)
        play_over_music(manager, record, stage_number);

    const u32 player = (id >> 24) == p->music_section ? manager + p->music_player_offset : 0;
    if (player) {
        ((fn2)virtual_function(player, 8))(player, (u32)&id);
        p->sound = word(player + 4);
        if (!p->sound)
            fail(7);
        if (starts_silent(id))
            ((fn3)p->set_music_state)(player, 1, 1);
    } else if (!(((fn4)virtual_function(manager, 0xc))(manager, (u32)&id, (u32)&p->sound, 0) & 0xff)) {
        fail(7);
    }
    p->status = STATUS_PLAYING;

    for (;;) {
        ((fn1)p->audio_frame)(manager);
        if (player)
            p->sound = word(player + 4);
        if (p->status == STATUS_PLAYING && p->sound == 0)
            p->status = STATUS_FINISHED;
        wait_frame();
    }
}

__attribute__((naked, section(".text.entry"))) void driver_entry(void) {
    __asm__ volatile("b driver_main");
}
