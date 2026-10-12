# GCSF: GameCube Sound Format

Version 1. GCSF stores sequenced music from Nintendo GameCube games so that it can be played back as the game plays it.
A GCSF rip holds the game's code. A player runs that code on an emulated GameCube: the game's OS, its sound library and
its DSP program. The game plays the sound itself. The rip is therefore exact.

GCSF works like 2SF for the DS. A rip requires a driver profile for the game (see "Ripping"). Version 1 rips games
whose sound library is JAudio, in either of its generations: JAudio1 or JAudio2. JAudio is the sound library of
Nintendo EAD's JSystem. Version 1 also rips games whose sound library is MusyX, Factor 5's sound library.

## Files

| Extension | Contents |
|---|---|
| `.gcsflib` | Shared data for one game: the game's code, the driver, the disc's header and the game's sound files. |
| `.minigcsf` | One sound: a small chunk that selects the sound, plus tags. It references a `.gcsflib` with `_lib`. |
| `.gcsf` | A whole set in one file. The ripper doesn't write these. Players play them like `.minigcsf` files. |

All of them use the PSF container.

## PSF container

```
0x00  3 bytes  "PSF"
0x03  u8       version: 0x2A for GCSF (see "Version byte" below)
0x04  u32      R: size of the reserved area
0x08  u32      N: size of the compressed program
0x0C  u32      CRC-32 of the compressed program
0x10  R bytes  reserved area (uncompressed)
      N bytes  program, zlib-compressed
      "[TAG]" followed by tags (optional)
```

All integers in the container, the chunk headers and the descriptor are little-endian. The CRC-32 is the one used by
zlib. Data that the GameCube itself reads, such as the driver's parameter block, keeps the GameCube's big-endian byte
order.

### Version byte

GCSF uses **0x2A**. Known PSF version bytes are 0x01 (PSF1), 0x02 (PSF2), 0x11 (SSF), 0x12 (DSF), 0x13 (Sega Genesis),
0x21 (USF), 0x22 (GSF), 0x23 (SNSF), 0x24 (2SF), 0x25 (NCSF), 0x26 (3SF), 0x27 (WSF), 0x28 (CSF), 0x29 (HSF) and 0x41
(QSF). The high nibble 2 marks Nintendo systems. 0x2A is the next free value. The value is defined once, in
`src/gcsf/format.h`.

## Program: chunks

The decompressed program is a sequence of chunks. Each chunk is an 8-byte header followed by its payload. The payload is
padded with zeros to a multiple of 4 bytes:

```
u32  type (four ASCII characters, stored as bytes in order)
u32  size of the payload in bytes (excluding padding)
...  payload
```

| Type | Payload | Meaning |
|---|---|---|
| `MEM ` | `u32 address`, then data | Load the data into main memory at `address`. The address is a cached address in the game's view of memory: 0x80000000-0x817FFFFF. |
| `FILE` | `u16 path length`, the UTF-8 path (no leading `/`, `/` separators), zeros to a multiple of 4, then the file data | A file on the game's disc (for example `AudioRes/JaiInit.baa`). |

Unknown chunk types are skipped. Chunks are applied in order. For the same address or path, later data replaces earlier
data.

### Loading order

As in other PSF formats, `_lib` is loaded recursively before the file that references it. Its path is relative to that
file. After the referencing file, `_lib2`, `_lib3` and so on are loaded in numeric order. Loading stops at the first
missing number. An empty `_lib` or `_libN` names no file. Chunks are applied in this loading order. A `.minigcsf`
usually contains one `MEM` chunk that patches the driver's parameter block.

## Reserved area: the process descriptor

The reserved area of at least one loaded file holds a process descriptor. The last descriptor in loading order takes
effect.

```
0x00  u32  magic "GCSP"
0x04  u32  descriptor version (1)
0x08  u32  entry point: main.dol's, a cached address
0x0C  u32  address of the driver's status word (0 if none)
0x10  u32  flags: bit 0 = the DSP is required; version 1 has no other flags
```

A version 1 player rejects process descriptors with a different version or flags other than 1 (only bit 0 set).

## The emulated system

A version 1 player provides:

- **The CPU.** Gekko at 486 MHz, one cycle per instruction. Its time base and decrementer count at a quarter of the 162
  MHz bus clock. The CPU starts at the descriptor's entry point with floating point enabled and address translation
  on. Block address translation registers map the first 256 MiB of the physical address space cached at 0x80000000 and
  uncached at 0xC0000000. The uncached mapping also reaches the hardware registers at 0xCC000000.
- **Memory.** Main memory (24 MiB at physical 0) and ARAM (16 MiB). Both are zero-filled. Main memory is then loaded
  with the `MEM` chunks. Before the game starts, low memory holds what the IPL and the apploader leave: the disc
  header's first 32 bytes at 0x80000000, the boot magic 0x0D15EA5E at 0x80000020, version 1 at 0x80000024, the memory
  size, console type 3 (a retail console), an arena from 0 to 0x81700000, the disc's file system table at the top of
  main memory (its address at 0x80000038 and its size at 0x8000003C), and the bus and CPU clocks (0x800000F8 and
  0x800000FC).
- **The disc.** A disc made of the set's files: `sys/boot.bin`, the disc's 0x440-byte header, then a file system table
  generated from the other `FILE` chunks' paths, and their data. The game reads it through the disc interface's
  registers (physical 0x0C006000). The drive answers the SDK's read, disc ID, seek, inquiry, request error, stop motor
  and audio commands. It answers each command 20,000 CPU cycles after the command starts. A read's data arrives at that
  point. An inquiry returns revision 2 and the date 2002-04-02. A read past the disc's end fails. Request error then
  reports 0x052100. An unknown command fails with 0x052000. The cover stays closed.
- **The processor interface.** Its interrupt cause and mask registers drive the CPU's external interrupt. The causes are
  the disc interface, the external interface, the DSP interface and the video interface. The reset switch reads as not
  pressed.
- **The video interface.** Every NTSC field (1001/60000 s) raises one of the two display interrupts that the OS sets:
  DI1 in one field and DI0 in the next. An interrupt that the OS hasn't enabled isn't raised. The game's code clears
  them. The video interface's other registers keep what's written to them.
- **The DSP interface** (physical 0x0C005000). It has the mailboxes, the DSP control register, the ARAM DMA and the
  audio interface's DMA. The ARAM DMA moves data between main memory and ARAM. It takes 246 CPU cycles for each 32
  bytes. A transfer that starts past the end of ARAM moves nothing. A console without an ARAM expansion does the same.
  The DSP runs one slice behind the CPU. Every 12,600 CPU cycles, it runs the cycles of the previous slice. It runs one
  DSP cycle per 6 CPU cycles. A CPU read of a mailbox's high half first gives the DSP 12 cycles.
- **The DSP.** The game's DSP program runs at the instruction level. Its DMA reaches main memory. Its accelerator reads
  samples from ARAM and writes to ARAM.
- **The audio interface** (physical 0x0C006C00). Its sample counter counts at the streaming rate while playback is on:
  108 MHz / 2,248 or 108 MHz / 3,372. It starts a period after playback starts or the rate changes.
- **The external interface.** Device 1 on channel 0 holds the clock (at 0) and the console settings in SRAM. The
  settings are the defaults, apart from the sound. The sound is set to stereo (bit 2 of the byte at 0x13). Other
  devices are missing.
- **The serial interface.** Nothing is connected. A transfer finishes at once.
- **Output.** The audio interface's DMA, 16-bit stereo: 8 samples from main memory every 121,392 CPU cycles. That's 108
  MHz / 3,372: 32,028.47 Hz. The player reports the samples as they come. It reports a rate of 32,028 Hz.

Other registers keep what's written to them. A player may skip the iterations of a three-instruction loop that loads a
word from memory, compares it and branches back while it's unchanged. It may skip them up to the next interrupt check.
Nothing else changes memory while the CPU runs. The result is therefore the same.

## Driver

Rips contain one of GCSF's drivers. Each driver calls the functions of one game's sound layer. The game's profile names
the driver. Four Swords Adventures' driver is `driver/driver.c`. Mario Golf: Toadstool Tour's driver is
`driver/mario_golf.c`. A driver is loaded at 0x81700000. Its entry point is at the same address. The rip patches the
game's `main` to branch to it. The driver calls the game's functions to do what the game's start-up does for sound. It
leaves out the graphics. Four Swords Adventures' driver sets up the video interface, JSystem's heaps and ARAM manager,
the game's audio object and disc thread, and the game's audio set-up. It loads every wave bank into ARAM. Then it starts
the sound by its JAudio sound ID. From then on it calls the game's audio frame process once per video retrace.

Twilight Princess's driver is `driver/twilight_princess.c`. It sets up the video interface, JSystem's heaps, ARAM
manager and thread switch manager, the game's heaps and disc thread, and the game's audio set-up. It loads the wave
groups of the wave list (see below) as the game's scene manager loads them. Then it starts the sound. From then on it
calls the audio manager's frame process every second video retrace. The game's frames are a 30th of a second. An entry
of its wave list holds a wave bank in its high 16 bits and a group of that bank's wave system in its low 16 bits.

The Wind Waker's driver is `driver/zel.c`. It sets up the video interface, JSystem's heaps and ARAM manager, and the
game's disc thread, as the game's machine set-up does. Then it calls the game's audio process once a frame until the
sound library is up. The library loads its start-up archive and the waves that stay loaded. It starts the effects
sequence. The driver then loads the waves that the `.minigcsf` names. It gives the sound library back its default
camera, which stays still. The game's sound set-up clears the camera, and the game's camera process sets it once a
frame. The sound library reads it for an effect with a Doppler shift. Then the driver starts the sound by its JAudio
sound ID. From then on it calls the audio process once a frame. A frame is two video retraces, as in the game. The
game gives the sound library 0xA00000 bytes of ARAM and graphics 0x5CE000. The driver gives the sound library all but
64 KiB of both, 0xFBE000 bytes. A rip draws nothing. The Wind Waker HD's waves that stay loaded take more than the
game's share for sound.

Mario Golf: Toadstool Tour's driver sets up the video interface, the OS's arena and heap, the ARAM and the game's heaps.
The game's sound set-up then starts MusyX, and the driver loads the game's two MusyX sound groups. The driver starts the
song by its number in the game's song table, or the jingle by its number in the jingle table. The game starts a jingle
from another thread. The jingle sounds half a second later. The driver's thread then sleeps. The song plays from the
audio and DSP interrupts.

Its parameter block is at **0x81700010**. The block's words are big-endian:

```
0x00  u32  magic "GCSD"
0x04  u32  version (1)
0x08  u32  status (written by the driver): 0 booting, 1 playing, 2 finished, 0x80000000 | step on error
0x0C  u32  sound ID: the sound to play
0x10  u32  the sound's handle (written by the game): the sound's object while it plays, then 0
0x14  ...  game profile: retraces between audio frames, the sizes that the game's start-up uses, then the
           addresses of the game's functions and the offsets of their objects (see driver.c)
```

A game's waves may not fit in the audio memory. A driver for such a game loads only the waves that the sound uses. Its
block has a wave list after the handle, and its game profile after the list:

```
0x14   u32  the number of waves that the sound uses, at most 64
0x18   u32  the waves: 64 entries. The driver's source says what an entry means
0x118  ...  game profile (see the driver's source)
```

The Wind Waker's driver has a wave list. An entry gives a wave system's number in its high 16 bits and a group of the
system in its low 16 bits. The driver skips a wave system that the sound library has loaded already. Each of the
game's wave systems has one group.

Mario Golf's driver doesn't use the handle. The top byte of a Mario Golf sound ID gives the table, and its low 16 bits
give the song's number in that table. Table 0 holds the songs, and table 1 the jingles.

A `.minigcsf` patches the sound ID: a 4-byte `MEM` chunk at 0x8170001C. For a driver with a wave list, it also patches
the list: a `MEM` chunk at 0x81700024 with the number of waves and the waves. A sequence that no sound plays also
requires a `FILE` chunk (see "Ripping"). After applying all `MEM` chunks, a version 1 player checks the driver block. If
the block has the expected magic, the player rejects versions other than 1. Every driver's block is version 1, whatever
its layout after the status word. A player reads only the magic, the version and the status word. The driver reads the
rest. A version 1 player therefore plays the rips of every game, including games that later releases add.

The descriptor's status address points at the status word (0x81700018). A player uses it at both ends of the sound:

- **Start.** The player runs the rip from boot and discards the output until the status first reads 1 or 2. The boot
  and the loading of the sound then don't count towards its length. The player stops with an error if the status has
  bit 31 set (the low byte is the step that failed). It also stops with an error if the status hasn't reached 1 after
  10 emulated seconds.
- **End.** Like any PSF player, a player plays for the tagged length plus fade. Without a length tag, it plays until
  half a second after the driver reports that the sound has finished (status 2). It stops sooner if a default length and
  fade end first. The player checks the status after every millisecond of emulated time.

## Tags

The standard PSF tags apply: `title`, `artist`, `game`, `year`, `genre`, `comment`, `copyright`, `length`, `fade`,
`volume`, `_lib`, `_libN`. `gcsfby` credits the ripper. The ripper also writes `gcsf_sound` with the sound's name from
the game's sound table, and `gcsf_mode` with the value `game`.

`length` and `fade` count time at the reported rate of 32,028 Hz: a player plays `length` × 32,028 stereo samples
before the fade.

## Ripping

`gcsfrip` builds sets from a GameCube disc image or an extracted disc. A game requires a driver profile. Profiles
(function addresses) are specific to each game and version. They're matched by game ID and the SHA-1 of `main.dol`.
Version 1 supports The Legend of Zelda: Four Swords Adventures (USA) and its Japanese release, The Legend of Zelda:
Four Swords Plus, The Legend of Zelda: Twilight Princess (USA) and The Legend of Zelda: The Wind Waker (USA). It also
supports Mario Golf: Toadstool Tour (USA).

The `.gcsflib` contains:

- the sections of the game's `main.dol`, with `main` patched to branch to the driver, and the profile's other patches
  (Mario Golf's `ClearArena` returns at once);
- the driver and its parameter block with the game profile;
- for a JAudio game, the disc's header (`sys/boot.bin`), the sound library's start-up archive (a BAA, or an AAF in The
  Wind Waker), the sequence archive, and wave archives (`.aw`), as `FILE` chunks. For Mario Golf, the disc's header, the
  files of its two MusyX sound groups and every song's file (an SNG), as `FILE` chunks;
- a process descriptor.

The wave archives are every one that the start-up archive's wave systems name, unless the game loads its wave groups as
its scenes require them. Twilight Princess and The Wind Waker do. The Wind Waker's library also holds the archives that
the sound library loads at start-up. A wave system lists a wave bank's waves in groups, and each group is one wave
archive. A wave can be in several groups. For such a game, the library holds the archives of the groups that the
rip's sounds use, and each `.minigcsf` lists its sound's groups (see "Wave groups").

Each `.minigcsf` selects one sound by its JAudio sound ID. The sound table in the start-up archive lists the sounds and
names them. In a BAA's table, a sound's ID gives its section in the top byte, its group in the next byte and its
index in the low 16 bits. `--bgm` selects the sounds of the section named BGM. `--only` selects the sounds whose names
match a regular expression.

The Wind Waker's start-up archive is JAudio1's AAF. Its sound table lists the effects in categories, then the music. It
names no sound. An effect is named after its category and index, for example `se4-0123`. A piece of music is named after
its file in the sequence archive. An effect's ID gives its category in bits 12 to 19 and its index in the low 10 bits.
Its bit 11 is set. The game sets that bit for most effects. With it, the sound library plays the effect to its end. A
piece of music's ID is 0x80000000 plus its index. The table's first piece is the effects sequence. The sound library
plays it all along. The ripper leaves it out.

Some of The Wind Waker's effects make no sound in a rip, and the ripper skips them. Most are empty: their code
returns at once, and the game plays nothing for them either. 34 others read a port that the game sets when it starts
the effect, and they play nothing while the port holds 0:

- **Port 6** holds the low 16 bits of the map information that the game passes with the effect, such as the material
  of the ground under a footstep. The driver passes 0. These effects read it: se2-0003 to 0005, se2-0051 to 0052,
  se2-0085, se2-0111, se2-0122 to 0123, se3-0000 to 0003, se3-0010 to 0012, se3-0021, se3-0024, se3-0038 to 0041,
  se3-0043 to 0048 and se5-0301.
- **Port 9** holds a value that the game gives the effect. These effects read it: se3-0049 to 0050, se4-0254 to 0255
  and se4-0327.

The sequence archive can hold sequences that no sound of the table plays. The game never plays them. The ripper rips
each one after the table's sounds, as music, named after its file without the extension. Its `.minigcsf` selects the
table's first sound that plays a file of the sequence archive. It also holds a `FILE` chunk with a copy of the start-up
archive. The copy replaces the library's. In the copy, that sound plays the sequence instead, at the volume that most of
the table's music has.

The sound table lays out a sound's data in one of two ways. A type with bit 0x40 set has the later layout (Twilight
Princess). The data's second byte is the sound's volume, and its 16 bits at +2 give a resource. A sound of type 0x60
to 0x6f plays the sequence archive's file with that id. In the earlier layout (Four Swords Adventures), the data's
third byte is the volume. A sound of type 0x11 plays a file of the sequence archive, and the low 16 bits of its data's
word at +8 give the file's id. An AAF's entry gives the file's id at +6 and the volume at +0xc. The Wind Waker's table
plays every file of its sequence archive. A sequence archive can be compressed with Yaz0. Twilight Princess's is.

Mario Golf's code lists its songs in two tables: 38 songs and 9 jingles. Each entry names the song's file. A `.minigcsf`
selects a song by its table and number (see "Driver"). `--bgm` takes every song and jingle. A song's name is its file's
name without the extension.

### Wave groups

For a game that loads its wave groups as its scenes require them, the ripper finds the groups that each sound requires.
It plays the sound with no group loaded. Each note still looks up its wave in its wave bank. The ripper watches the
lookups: the bank and the wave's id. It plays the sound as far as the length analysis would (see "Lengths"), with a
lookup counting as a sound. Then it picks groups from the bank's wave system. The group that holds the most of the
sound's waves comes first, then the group that holds the most of the rest, and so on. Of two groups that hold as many,
the lower group goes first. The `.minigcsf` lists the groups. The driver loads them before it starts the sound. The
length analysis then plays the sound with them loaded. The ripper finds the groups with `--no-length` too.

### Another release's sound files

`--sound-files DIR` makes the ripper read three kinds of file from another release of the game: the start-up archive,
the sequence archive and the wave archives. Everything else comes from the input, the game's code and the disc's header
included. The ripper looks for a file with the name of the profile's start-up archive (`Z2Sound.baa`) anywhere in
`DIR`. The folder that holds it stands for the folder of the profile's start-up archive (`Audiores/`). The sequence
archive and the wave archives are read from their places relative to it. A `DIR` with no such file, or with more than
one, is an error. A sound file that `DIR` lacks belongs to the other release. The ripper doesn't read the disc's file
in its place.

The sound table and the wave systems are then the other release's. A wave group whose archive `DIR` lacks is taken out
of the sounds' lists of groups, with a warning. Those sounds play without the group's waves. The library's `FILE`
chunks keep the disc's paths. A rip from another release's files therefore plays like any other.

A sequence archive that `DIR` lacks can come from a sequence table: one `.bdt` file in the folder where the archive
would be. The Wind Waker HD keeps its sequences that way (`Seqs/JaiSeq.bdt`). The table is a 32-bit count, zeros up to
0x20, and then a 32-bit offset and size for each sequence. A sequence's index in the table is its file's id in the
GameCube's archive. The ripper reads the disc's archive and replaces each file's data with the table's entry for the
file's id. The game's code finds a sequence by its file's id. The archive therefore keeps the disc's names and ids.
Each file's data starts on a 32-byte boundary.

### Lengths

The ripper plays each sound in game mode to work out its length. Sound means a sample louder than 8 of 32,767 (-72 dB).
The ripper watches the sound library's sequence parser. It sees each command that each of the sound's tracks runs. In
The Wind Waker, the effects sequence plays all along. Each effect plays in one of its tracks. There the ripper
takes only the sound's root track and the tracks below it: a piece of music's tracks, or the effect's track and the
tracks that the effect opens.

- A sound ends when the game releases it. Its notes can sound on after that: the output runs behind the game, and a
  release fades on. A sound that ends plays until half a second after its last sound.
- A sound loops when one of its tracks jumps back to a command it has already played, twice to the same command. A
  jump inside a subroutine or a counted loop doesn't count. A looping sound's length is the time of the second jump
  back. Its fade is 10 seconds. A pass runs from the track's last visit to the command that the jump goes to. A loop
  inside the pass is part of the pass. Its jumps back don't start the outer loop's count again.
- A pass that changes nothing doesn't count: it plays no note, changes none of the track's parameters, and opens and
  closes no track. Such a pass only waits, or polls the game with conditional commands. The track idles in that loop.
  A pass that also plays notes counts, whatever it polls. Loops before the first sound don't count either. A loop that
  completes without a sound ends the sound at its last sound instead.
- The tracks are taken in the order in which they start. A track's loop counts only while each track before it idles,
  has ended, or has run no command for 10 seconds with no ticks left to wait. Many sound effects work this way: the
  first track polls until the game stops the sound, and another track loops.
- A sound holds when one of its tracks idles, every track has settled as above, and for 10 seconds no track has run a
  command outside the loop it idles in. A note on a looping wave that nothing releases holds like this. A sound that
  has sounded in the last second loops where it holds, with the fade. Any other sound ended at its last sound.
- A sound that stays busy without a sound for 180 seconds ended at its last sound.
- A sound still playing at 600 seconds gets a length of 600 seconds and the fade.

A MusyX game's songs follow other rules. MusyX keeps a slot for each of a song's tracks. A slot's first word is the tick
at which its track's next region starts. That tick falls when the song loops. A looping song's length runs to the end of
its second loop, plus the fade. A song that ends leaves MusyX's lists of playing songs. The rip of such a song plays
until half a second after the last sound. MusyX's output keeps a constant level on each channel after a song has
stopped. For these songs, a sample therefore counts as sound when it differs by more than 8 from its channel's average
over about the last 50 milliseconds. The rules above for loops before the first sound, loops without a sound, 180
seconds without a sound and the 600-second limit still apply.

The ripper lists and skips the sounds that never make a sound. `--no-length` leaves out the analysis. Without the
analysis, no sound is skipped and the files have no length tags.

Tags written by `gcsfrip`: `title` and `gcsf_sound` (the sound's name, or the file's name for a sequence that no sound
plays), `game`, `length` and `fade`, `gcsf_mode`, `gcsfby`, `utf8=1`, and optionally `artist`, `year` and `copyright`.

## Copyright

A `.gcsflib` contains copyrighted game code and data. Rips are for personal use.
