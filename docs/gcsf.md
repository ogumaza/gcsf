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

Luigi's Mansion's driver is `driver/luigis_mansion.c`. It sets up the video interface, the disc with long file names,
JSystem's heaps and ARAM manager, and the game's sound set-up. It loads the wave systems that the game loads before its
first frame. Then it calls the game's sound frame until the sound library is up and every wave system that stays loaded
is in ARAM. Then it starts the sound by its JAudio sound ID. From then on it calls the sound frame once a frame. A frame
is two video retraces, as in the game.

Super Mario Sunshine's driver is `driver/super_mario_sunshine.c`. It sets up the video interface, the disc with long
file names, JSystem's heaps and ARAM manager, and the game's sound object. The sound object sets up the sound library.
The game hands its sound object the start-up archive. The driver hands it none. The sound library then reads the archive
from a file of the rip (see "Ripping"). Next come the three wave groups that the game loads at start-up and before its
title. The driver calls the game's sound frame until the sound library is up and those groups are in ARAM. Then it loads
the scene's group that the `.minigcsf` names. That group goes at the top of ARAM. The game's scene loader puts it there
too. The sound then starts by its JAudio sound ID. A piece of music gets the volume that the game gives its music, 0.75.
From then on the driver calls the sound frame once a frame. As in the game, a frame is two video retraces.

Pikmin's driver is `driver/pikmin.c`. The game's sound library is an early JAudio1. The game calls it through C
functions, without a sound table. The driver sets up the OS, the disc with long file names, the video interface and
ARAM. Then the driver starts the sound library with a block of the arena and of ARAM. The sound library loads its bank
file and starts two threads. Those threads play the music. The game then gives the sound library a buffer for the disc,
from two words of its system object. Only the game's set-up of that object fills them in. The driver doesn't run that
set-up. It passes the two words as they are, 0 and 0. The sound library's disc thread keeps its two 32 KB buffers. Then
the driver reads the piece's sequence and loads the wave group that the sound ID gives (see "Ripping"). The piece plays
in the slot that the game plays its music in. The piece gets the music's volume from the game's options. Without a save,
that volume is full. The driver leaves it there. The driver writes the sound's handle itself: the address of a word that
holds the piece's root track. The driver checks for the sound's end once a video retrace. At the end, the driver sets
the handle to 0.

The music of Pikmin's cutscenes is one piece in parts. The sound library plays that piece from its start-up. The piece's
root track polls a port. A cutscene writes the number of a part to that port. The root track then opens a track that
plays the part. For a part, the driver loads the part's wave group and writes the part's number to the port. The sound
starts at that write. The handle then points at a word that holds the track that plays the part.

Donkey Kong Jungle Beat's driver is `driver/jungle_beat.c`. It sets up the video interface, JSystem's heaps and ARAM
manager, and the game's heaps. It loads the game's sound files as the game does: the start-up archive, the sequence
archive, the chord archive and the chord database. The game decodes the start-up archive from Yaz0 as it loads it. The
driver then creates the game's audio manager, loads the wave groups of the wave list and starts the sound. Music starts
in the game's BGM player. The player sets each of a piece's tracks to a state that tables in the game's code give. Some
tracks start silent. The game turns them on later, or never. A piece whose first state silences every track gets its
next state at once. The game turns that piece's tracks on later. From then on the driver calls the audio manager's frame
process once per video retrace. An entry of its wave list holds a wave bank in its high 16 bits and a group of that
bank's wave system in its low 16 bits.

Mario Kart: Double Dash!!'s driver is `driver/double_dash.c`. It sets up the video interface, JSystem's heaps and ARAM
manager, and the game's audio heap. It loads the start-up archive as the game does and passes it to the game's audio
set-up. Then it loads every wave bank that the archive names and starts the sound. From then on it calls the audio frame
process once per video retrace. Some effects are level sounds, such as the engines. The game's start function gives a
level sound a lifetime of one frame. The game starts the sound again each frame for as long as it should play. The
driver starts it again before each frame until the sound ends.

Mario Golf: Toadstool Tour's driver sets up the video interface, the OS's arena and heap, the ARAM and the game's heaps.
The game's sound set-up then starts MusyX, and the driver loads the game's two MusyX sound groups. The driver starts the
song by its number in the game's song table, or the jingle by its number in the jingle table. The game starts a jingle
from another thread. The jingle sounds half a second later. The driver's thread then sleeps. The song plays from the
audio and DSP interrupts. Mario Power Tennis plays through the same driver.

WarioWare, Inc.: Mega Party Game$!'s driver is `driver/warioware.c`. It sets up the game's heaps. The game's file loader
then reads the game's five sound files from the disc. The driver puts each one where the game's sound set-up looks for
it. The sound set-up initialises the ARAM, the audio interface and MusyX. It loads every MusyX sound group into ARAM and
sets up the reverb. The driver then plays the song through the game's play function. It passes the mode in which the
game plays music. The driver's thread then sleeps.

Pikmin 2's driver is `driver/pikmin2.c`. It sets up the video interface, JSystem's heaps and ARAM manager, and the
game's sound system: the start-up archive, the game's sound layer, and the global scene with the effects sequence. The
game plays each piece of music through an object of its sound layer, in a sound scene. The driver makes a scene and the
piece's object in the way the game makes them. It loads the scene's resources: the piece's sequence, the conductor of a
cave's music and the wave groups of the wave list. Then the scene starts the piece. The game starts a boss's music when
the boss appears, at a part of the music that the boss picks. The driver starts the bosses' music at those parts. From
then on it calls the sound layer's frame work once a frame. A frame is two video retraces. The driver's scene has no
game objects. The music therefore plays as it does when nothing happens in the game.

The global scene's effects sequence plays the effects. The global scene also loads wave group 0. That group holds the
waves of every effect. The driver starts an effect as the game starts the effects of its menus: through the sound
system, with no position in the game's world. Some effects ask the game about its state. The effects of the ship and the
pod ask whether the debt is paid. The driver gives them the game's play data with every flag clear. That is the state at
the start of the story. The effects of the captains' marching horns ask for the horn's place in its tune and for the
wait between its notes. The driver's horns start at the tune's beginning, with the shortest wait that the game sets: the
wait for a stick tilted all the way.

Pac-Man Vs.'s driver is `driver/pacman_vs.c`. The game plays its sound through a thin layer in its code over JAudio1's
`JAIBasic`. The driver sets up the game's heaps and the video interface. The game runs its other steps on its task
thread. The driver does the same. The game's reader of disc files stops on a thread whose priority number is below 17.
The steps set up the disc, the ARAM manager, the start-up archive, the sequence archive and the sound library. The sound
library loads both of the game's wave systems and starts the effects sequence. The driver then starts the effect through
the game's layer without a position. From then on the driver calls the layer's frame work once a frame.

The Legend of Zelda: Collector's Edition's driver is `driver/collectors_edition.c`. It covers the disc's menu. The
driver sets up the video interface, JSystem's heaps and ARAM manager, and the menu's audio heap. The driver mounts the
sequence archive and reads the start-up archive. The menu's loader thread does the same. The menu's audio set-up then
reads the start-up archive into the sound library and loads the wave archive that the start-up archive marks. The set-up
returns the sound manager. The driver waits until the wave archive is in ARAM. Then the driver starts the sound through
the sound manager without a position. The menu starts its effects that way. From then on the driver calls the sound
manager's frame work once a frame.

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

Pikmin 2's driver has a wave list too. Its entries have the same layout. The game has one wave system, with 49 groups.
Group 0 holds the effects. The sound system loads it at start-up, and the driver skips it. A scene loads at most two
other groups.

Mario Golf's driver doesn't use the handle. The top byte of a Mario Golf sound ID gives the table, and its low 16 bits
give the song's number in that table. Table 0 holds the songs, and table 1 the jingles. Mario Power Tennis has only
table 0. It holds the jingles too. The driver plays them as songs.

WarioWare's driver doesn't use the handle either. A WarioWare sound ID is MusyX's song ID: the song's MusyX song
group in the top byte and its number in the low 16 bits.

The game starts its music by the sequence's file name. A Pikmin 2 sound ID is 0x80000000 plus the piece's number in the
driver's table of pieces. The table gives each piece's file, how the game makes the piece's object, the conductor of a
cave's music, the object's sound information, and the part at which a boss's music starts. An effect's sound ID is its
ID in the sound table (see "Ripping").

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
Version 1 supports The Legend of Zelda: Four Swords Adventures (USA) and its Japanese release, The Legend of Zelda: Four
Swords Plus, The Legend of Zelda: Twilight Princess (USA), The Legend of Zelda: The Wind Waker (USA), Luigi's Mansion
(USA), Super Mario Sunshine (USA), Pikmin (USA), Donkey Kong Jungle Beat (USA) and Mario Kart: Double Dash!! (USA). It
also supports Mario Golf: Toadstool Tour (USA), Mario Power Tennis (USA), WarioWare, Inc.: Mega Party Game$! (USA),
Pikmin 2 (USA), Pac-Man Vs. (USA) and the menu of The Legend of Zelda: Collector's Edition (USA).

The `.gcsflib` contains:

- the sections of the game's `main.dol`, with `main` patched to branch to the driver, and the profile's other patches
  (they skip the clear of the OS's arena: Mario Golf's `ClearArena` and the same code inside Mario Power Tennis's
  `OSInit`);
- the driver and its parameter block with the game profile;
- for a JAudio game, the disc's header (`sys/boot.bin`), the sound library's start-up archive (a BAA, or an AAF in The
  Wind Waker, Luigi's Mansion, Super Mario Sunshine, Pikmin 2 and Pac-Man Vs.), the sequence archive if the game has
  one, the game's other sound files (Donkey Kong Jungle Beat's chord archive and chord database), and wave archives
  (`.aw`), as `FILE` chunks. Super Mario Sunshine keeps its start-up archive, `msound.aaf`, in `data/nintendo.szs`, an
  archive compressed with Yaz0. The library holds it unpacked, as `AudioRes/mSound.aaf`. The sound library reads that
  file when the game hands it no archive. Pikmin has no start-up archive. Its library holds the disc's header, the bank
  file (`pikibank.bx`), the sequence archive (`pikiseq.arc`) and every wave archive that the bank file's wave systems
  name. Pikmin 2's library also holds the other files that the game's sound layer reads: the conductors
  (`Conductor.arc`), `Key.arc`, the lists of music and streams, and the track maps. For Mario Golf and Mario Power
  Tennis, the disc's header, the files of the game's two MusyX sound groups and every song's file (an SNG), as `FILE`
  chunks;
- a process descriptor.

The wave archives are every one that the start-up archive's wave systems name, unless the game loads its wave groups as
its scenes require them. Twilight Princess, The Wind Waker, Super Mario Sunshine, Donkey Kong Jungle Beat and Pikmin 2
do. The Wind Waker's and Super Mario Sunshine's libraries also hold the archives that the sound library loads at
start-up, and Pikmin 2's holds group 0's. A wave system lists a wave bank's waves in groups, and each group is one wave
archive. A wave can be in several groups. For such a game, the library holds the archives of the groups that the rip's
sounds use, and each `.minigcsf` lists its sound's groups (see "Wave groups").

Each `.minigcsf` selects one sound by its JAudio sound ID. The sound table in the start-up archive lists the sounds and
names them. In a BAA's table, a sound's ID gives its section in the top byte, its group in the next byte and its index
in the low 16 bits. `--bgm` selects the sounds of the section named BGM. Mario Kart: Double Dash!! names it TYPE_BGM.
`--only` selects the sounds whose names match a regular expression.

The Wind Waker's start-up archive is JAudio1's AAF. Its sound table lists the effects in categories, then the music. It
names no sound. An effect is named after its category and index, for example `se4-0123`. A piece of music is named after
its file in the sequence archive. An effect's ID gives its category in bits 12 to 19 and its index in the low 10 bits.
Its bit 11 is set. The game sets that bit for most effects. With it, the sound library plays the effect to its end. A
piece of music's ID is 0x80000000 plus its index. The table's first piece is the effects sequence. The sound library
plays it all along. The ripper leaves it out. Luigi's Mansion's start-up archive is an AAF too, and its sounds follow
the same rules. So do Super Mario Sunshine's, but that game names its sounds in `AudioRes/mSound.asn`: a 16-byte header,
then 32 bytes for each name, with the name in up to 28 bytes and then a word. The first 18 name the sound table's
categories. Each one after them names a sound, and its word is the sound's ID. A rip takes those names, for example
`MSD_BGM_DOLPIC`. The table gives a piece of music the ID 0x80010000 plus its index, and the driver starts it by that
ID.

Some of The Wind Waker's effects make no sound in a rip, and the ripper skips them. Most are empty: their code
returns at once, and the game plays nothing for them either. 34 others read a port that the game sets when it starts
the effect, and they play nothing while the port holds 0:

- **Port 6** holds the low 16 bits of the map information that the game passes with the effect, such as the material
  of the ground under a footstep. The driver passes 0. These effects read it: se2-0003 to 0005, se2-0051 to 0052,
  se2-0085, se2-0111, se2-0122 to 0123, se3-0000 to 0003, se3-0010 to 0012, se3-0021, se3-0024, se3-0038 to 0041,
  se3-0043 to 0048 and se5-0301.
- **Port 9** holds a value that the game gives the effect. These effects read it: se3-0049 to 0050, se4-0254 to 0255
  and se4-0327.

Donkey Kong Jungle Beat has effects like these too. 133 are empty. 68 read port 0xb, and some of them also ports 0xc to
0xf. Most take the key of a note from those ports. They play nothing while port 0xb holds 0, and the driver leaves it
0. Two more make no sound in a rip. `gcsfrip` names each effect that it skips.

Mario Kart: Double Dash!! keeps its music's sequences in the start-up archive. It has no sequence archive. The ripper
skips 882 of its effects. 872 are voices. Their waves are in three more start-up archives inside the first one. The game
loads one of those at a time. A rip loads none of them. 8 more effects have a volume of 0 in the sound table. 2 are
empty.

The sequence archive can hold sequences that no sound of the table plays. The game never plays them. The ripper rips
each one after the table's sounds, as music, named after its file without the extension. Its `.minigcsf` selects the
table's first sound that plays a file of the sequence archive. It also holds a `FILE` chunk with a copy of the start-up
archive. The copy replaces the library's. In the copy, that sound plays the sequence instead, at the volume that most of
the table's music has.

The sound table lays out a sound's data in one of three ways. A type with bit 0x40 set has the later layout (Twilight
Princess). The data's second byte is the sound's volume, and its 16 bits at +2 give a resource. A sound of type 0x60 to
0x6f plays the sequence archive's file with that id. In the earlier layout (Four Swords Adventures), the data's third
byte is the volume. A sound of type 0x11 plays a file of the sequence archive, and the low 16 bits of its data's word at
+8 give the file's id. An AAF's entry gives the file's id at +6 and the volume at +0xc. That holds when bit 0 of the
table's second byte, the music's format, is set. The Wind Waker's table sets it. Without the bit, a piece of music plays
the file whose id is its index, as in Luigi's Mansion. The Wind Waker's table plays every file of its sequence archive.
A sequence archive can be compressed with Yaz0. Twilight Princess's is. So can a start-up archive. Donkey Kong Jungle
Beat's is (`Donkey.szs`). Luigi's Mansion and Super Mario Sunshine keep their sequences in one file without an archive's
directory. A block of the AAF (chunk type 4, `BARC`) lists each sequence's name, offset and size in that file. The first
layout (Mario Kart: Double Dash!!) came before the other two. Mario Kart: Double Dash!!'s profile selects it. A sound of
type 0x10 plays a sequence of the start-up archive's sequence collection. Type 0x20 plays the start-up archive's
sequence block with the sound's ID. Type 0x30 plays a stream. The data's ninth byte is the volume.

Mario Golf's code lists its songs in two tables: 38 songs and 9 jingles. Mario Power Tennis's code lists 111 songs and
jingles in one table. Each entry names the song's file. A `.minigcsf` selects a song by its table and number (see
"Driver"). `--bgm` takes every song and jingle. A song's name is its file's name without the extension.

Pikmin's code holds the index of its sequences: a BARC of 22 entries that gives each sequence's name and its offset and
size in `pikiseq.arc`. The first entry is the effects sequence, and the second plays the system's events. Five entries
are empty. The other 15 are pieces of music. A `.minigcsf` selects a sound by its ID: 0x80000000 | group << 24 | wave
system << 16 | part << 8 | piece. The piece is the sequence's number in the index. The driver loads the group of the
wave system that the ID gives. A piece's ID gives part 0 and group 0 of the wave system with the piece's number. The
piece's name is its file's name without the extension, for example `select`. `--bgm` takes every piece. One piece plays
an empty bank. That piece makes no sound, and the ripper skips it. The music of the game's cutscenes is one piece in
parts, `demobgm`. The ripper lists the parts that the game plays in place of that piece. A part's ID gives the part and
the wave group that the game loads for it. A part's name is the piece's name, a hyphen and the part's number, for
example `demobgm-5`. The game plays 13 more pieces, most of them in cutscenes, from streams (`.stx` files). The game's
sound effects aren't ripped.

WarioWare, Inc.: Mega Party Game$!'s code names its music in the table of its Sound Room: 231 pieces. A `.minigcsf`
selects a piece by its MusyX song ID (see "Driver"). Each piece keeps the name that the Sound Room gives it. `--bgm`
takes every piece. Two of the pieces only count beats for the game. They have no notes, and the ripper skips them. The
game plays its sound effects as MusyX songs too. They aren't ripped. For WarioWare, the `.gcsflib` holds the disc's
header and the game's five sound files as `FILE` chunks: MusyX's project, pool, sample directory and samples, and one
file with every song.

Pikmin 2's start-up archive is an AAF too. Its sound table lists the effects sequence and 24 other sequences. The game's
sound layer starts each piece of music by its file. The ripper leaves out the table's sequences. It takes every file of
the sequence archive but the effects sequence, `se.bms`: 48 pieces. A piece is named after its file without the
extension. The game never plays 14 of them. One of those, `result`, makes no sound, and the ripper skips it. `--bgm`
takes every piece. The game picks a conductor for each floor of a cave. The conductor picks the phrases that the tracks
of the cave's music play, at random. A rip plays a cave's piece with the first conductor of its family, such as
`caveconc_00_0.cnd` for `caveconc`. The game's code names that conductor itself, for a boss's floor. A cave's piece
never repeats. It plays to the ripper's limit of 600 seconds, then fades (see "Lengths").

Pikmin 2's sound table lists 1,038 effects in categories 0 to 5. Their IDs follow The Wind Waker's rules. The game names
its sounds in `AudioRes/PSound.asn`: a 16-byte header, then 32 bytes for each name, with the name in up to 28 bytes and
then a word. The first 18 name the sound table's categories. Each one after them names a sound, and its word is the
sound's ID. A rip of an effect takes that name, for example `PSSE_SY_MENU_CURSOR`. The ripper lists the pieces first,
then the effects. Four effects make no sound, and the ripper skips them. The code of `PSSE_PL_DEMOE00_POD02` returns at
once. The other three play only some of the times that the game starts them: `PSSE_EN_FROG_VOICE`,
`PSSE_EN_OOPAN_BOUND2` and `PSSE_EN_OOPAN_VOICE_DOWN`. Each one draws a random number and ends at once on some draws.
The sound library's random numbers start from a fixed seed. In a rip, each of the three draws a number that ends it.

Pac-Man Vs.'s start-up archive is an AAF too. Its sound table lists the effects sequence and 76 effects in categories 0
to 2. The effects' IDs follow The Wind Waker's rules. The game names none of its sounds. A rip of an effect is named
after its category and index. 17 effects make no sound. The ripper skips them. The code of 5 returns at once: se0-0000,
se1-0007, se1-0008, se1-0012 and se1-0013. 11 narration effects play a note that has no sample in the instrument:
se2-0000, se2-0009, se2-0015, se2-0017, se2-0028 to 0030 and se2-0035 to 0038. The sound table gives se2-0020 a volume
of 0.

Collector's Edition's menu has a BAA with the later layout. Its sound table lists 23 effects, one piece of music and
one stream. A rip keeps the table's names, such as `JA_SE_TR_OC_CURSOR_L`. The start-up archive's sequence collection
(`bsc`) holds the effects' sequences. The sequence archive is compressed with Yaz0. It holds one file, `zeldamain.bms`.
The piece, `JA_BGM_DUMMY`, plays it. The menu's code never starts the piece. The disc's other programs are out of
scope: the four games that the disc emulates, and a demo of The Wind Waker.

### Wave groups

For a game that loads its wave groups as its scenes require them, the ripper finds the groups that each sound requires.
It plays the sound with no group loaded. Each note still looks up its wave in its wave bank. The ripper watches the
lookups: the bank and the wave's id. It plays the sound as far as the length analysis would (see "Lengths"), with a
lookup counting as a sound. Then it picks groups from the bank's wave system. The group that holds the most of the
sound's waves comes first, then the group that holds the most of the rest, and so on. Of two groups that hold as many,
the lower group goes first. The `.minigcsf` lists the groups. The driver loads them before it starts the sound. The
length analysis then plays the sound with them loaded. The ripper finds the groups with `--no-length` too. Super Mario
Sunshine's scene groups share some waves, with the same samples, and the game holds one of them in ARAM at a time. Its
driver loads the first group of the list that the start-up doesn't load.

For a piece of Pikmin 2 that the game plays, the ripper lists the groups that the game's scene loads for the piece
instead. A note whose wave lies in another group is silent in the game, and in the rip too. Every effect of Pikmin 2
finds its waves in group 0.

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
The Wind Waker, Luigi's Mansion, Super Mario Sunshine, Pikmin 2 and Pac-Man Vs., the effects sequence plays all along.
Each effect plays in one of its tracks. There the ripper takes only the sound's root track and the tracks below it: a
piece of music's tracks, or the effect's track and the tracks that the effect opens.

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
- A track can run an interrupt. Pikmin 2's root tracks run one every tick. The interrupt's commands count as a
  subroutine's.
- In Pikmin 2 and Pac-Man Vs., a track can also idle in a loop inside a subroutine. That holds only when the loop's
  passes change nothing. Many of Pikmin 2's root tracks call a routine that polls the game once a beat and never
  returns. The caves' music is the exception. Its conductors pick the phrases at random. A cave's piece never repeats.
  The analysis counts its root track's polls as moves, and the piece plays to the limit. In one of Pac-Man Vs.'s effects
  (se1-0006), the effect's track polls in such a routine while a child track loops.
- The tracks are taken in the order in which they start. A track's loop counts only while each track before it idles,
  has ended, or has run no command for 10 seconds with no ticks left to wait. Many sound effects work this way: the
  first track polls until the game stops the sound, and another track loops. In Donkey Kong Jungle Beat, the first track
  polls in a subroutine. That loop doesn't count. The game's effects that loop therefore play until the 600-second
  limit. In two of Mario Kart: Double Dash!!'s pieces, a note holds a looping wave while the first tracks wait 133
  seconds between their jumps back. Those pieces play for 266.665 seconds, two of the first track's loops. 32 of the
  game's level sounds play until the 600-second limit.
- A sound holds when one of its tracks idles, every track has settled as above, and for 10 seconds no track has run a
  command outside the loop it idles in. A note on a looping wave that nothing releases holds like this. In Luigi's
  Mansion, a track with at least 0x2000 ticks left to wait has settled too. Some of its effects hold a note with a wait
  of 16,000 or 32,000 ticks until the game stops them. In Super Mario Sunshine the threshold is 0x7000 ticks. Some of
  its effects hold a note of 32,767 ticks. In that game, the commands of a track's interrupt routine aren't the
  track's moves. Some of its effects hold a note while a timer routine polls the game every 5 ticks. A sound that has
  sounded in the last second loops where it holds, with the fade. Any other sound ended at its last sound.
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
