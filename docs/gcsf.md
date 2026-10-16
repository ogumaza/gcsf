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

Final Fantasy Crystal Chronicles' driver is `driver/crystal_chronicles.c`. The game plays its music and its effects
through Square's Red driver, on the SDK's AX. The driver sets up the game's memory manager, sound manager and file
manager in the order of the game's system set-up. The sound manager starts Red and Red's threads. For a piece of music,
the driver then reads the piece's file and loads the wave bank that the file names. The game's scripts load that bank
before the piece. The driver loads the piece and plays it through the game's functions. For an effect, the driver loads
what the game has loaded when it plays the effect. An effect numbered below 4000 is in one of four blocks of effects.
The game loads the blocks as it starts, with the two wave banks that their effects use. Those banks take Red's first two
slots for banks. A track of any effect can pick a wave from either slot. So for every effect, the driver first calls the
game's function that loads the blocks and the two banks. An effect from 4000 on is a SEP file of its own. The driver
then reads the file and loads the wave bank that the file names. Next, the driver loads the file through the game's
function. The game's scripts load both before the effect. The driver plays the effect through the game's function, with
the pan, the volume and the fade-in that the game's code passes. The driver's thread then sleeps. The sound plays on
Red's threads. The audio interrupt drives them. The game's memory manager gives its heap the memory up to 0x81780000 and
fills it. The rip patches the manager's set-up to end the heap at 0x81700000, below the driver.

Animal Crossing's driver is `driver/animal_crossing.c`. The game's sound library is a port of Nintendo's sound library
for the Nintendo 64. The port runs the Nintendo 64's sequence player and synthesis on the CPU, inside an early JAudio1
that mixes the result with the DSP. The game calls it through C functions. The driver sets up the OS, its alarms, the
disc, the video interface and ARAM. The game's start-up does the same. The driver then starts the sound library. One of
the sound library's threads reads all of the game's sound data, `audiorom.img`, from the disc into ARAM. The driver runs
the game's sound frame once a retrace until the sound library is up. The sound library would then play the start-up
screen's sound, about 7 s of a voice and a chime. The game plays no music over it. The rip patches out the function that
starts it. The driver sets the weather and starts the piece by the game's number for it. From then on it runs the sound
frame once a frame, at 30 frames a second. In the museum, the driver sets the room after it has started the piece. The
game starts a music disc's piece at volume 0. Once a frame, the game then sets the music's volume and pan from where the
listener stands by the stereo. Before each frame, the driver places the listener at a hi-fi stereo, at a distance of 0.
The piece then plays at a volume of 0.8, with every channel in the centre.

Kapp'n sings his songs on the boat in two parts. The game starts the first part as a piece. Once he sings, the game asks
for one of ten second parts. That request writes two ports of the first part's sequence player: a player and a sequence.
About 45 s in, the first part's script starts that sequence on that player. The first part's sound has ended by then.
Without the request, the script would start the wrong sequence on the effects' player. For a first part, the driver
writes the first part's player and a sequence that ends at once to the ports. The first part's player then stops
instead. For a second part, the driver writes the second part's sequence to the game's table entry of a piece that plays
an empty sequence. The driver then starts that piece. The game plays the second part like any other piece.

For an effect, the driver waits for the sound library's sequence of effects. The game's sound frame starts that sequence
once the sound library is up. The driver then starts the effect by the game's number for it. The sound library gives the
effect one of six slots. The channel of the sequence with the slot's number plays the effect. The game starts an effect
that has no place in the scene through one function. In most scenes, that function plays effect 0x424 as effect 0x16e.
The driver therefore calls the function that this one calls, with the same values. The sound library frees the slot when
the effect ends.

The Pokémon Colosseum Bonus Disc's driver is `driver/colosseum_bonus.c`. The disc plays its sequenced sound in one
program, the one that sends Jirachi to a Game Boy Advance. That program is a disc image inside the disc, a TGC (see
"Ripping"). The program uses HAL Laboratory's sound library. The library runs a script for each effect and plays the
effects' samples on the SDK's AX. The driver sets up the video interface, the audio interface and ARAM. The program's
main does the same. The driver then starts the sound library with an allocator over the OS's arena. The program gives
the library an allocator over its heap instead. Next, the driver reads the effects' scripts (`pm6fgm.sem`) into memory
and passes them to the sound library. The sound library reads the effects' samples (`pm6fgm.ssm`) from the disc into
ARAM. The driver waits until the bank has loaded. Then the driver starts the effect by its number at the highest volume,
in the centre. The program plays all of its effects that way. The sound plays from the audio and DSP interrupts. The
sound library keeps a list of the effects that play. An effect leaves the list once its voices have stopped. Once a
retrace, the driver checks the list. The driver leaves out the program's streamed music, its graphics and its link to
the Game Boy Advance.

`driver/musyx.c` is the driver of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door, Tales of
Symphonia, Star Fox Adventures, Star Wars Rogue Squadron II: Rogue Leader, Star Wars Rogue Squadron III: Rebel Strike,
Pokémon Colosseum and Resident Evil Zero. The driver calls MusyX's functions, not a layer of the game's code over them.
Other games whose sound library is MusyX can use it too. A game's profile gives the addresses of the functions and the
values that the game passes to MusyX. The driver first calls the game's start-up functions that come before its sound,
such as the set-up of the disc. Next come the ARAM, the ARAM's queue and the audio interface. The driver then sets
MusyX's memory hooks. The hooks hand out the OS's arena from the bottom up. sndInit follows, with the game's numbers of
voices and the game's share of ARAM. Then the driver sets the game's output mode. MusyX starts each of its volume groups
at a volume of 0. A song's start sets the song's group. An effect's start leaves the effect's group alone. The start-ups
of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door and Rebel Strike call sndVolume(127, 0,
0xFF). That call gives every group its full volume. The driver makes it for these games too. The driver reads the
sound's entry from the rip's catalogue (see "MusyX catalogue" below). The entry can give words for the driver to write
into the game's memory, such as the settings of the game's reverbs. A game's profile can name aux effects for studio 0.
The driver prepares them and passes them to sndSetAuxProcessingCallbacks. Each group that the entry names is in a set of
four files in the rip: the project, the pool, the sample directory and the samples. The driver reads them and pushes the
group with sndPushGroup. Groups in a row from one set share its files. MusyX copies the samples to ARAM. A song plays
from its file through sndSeqPlayEx, with the song's group and MusyX's song ID. An effect plays through sndFXStartEx. A
volume or a pan of 0xFF there takes the effect's value from its group's table of effects. The entry can then set the
effect's MIDI controllers, through sndFXCtrl and sndFXCtrl14. Paper Mario: The Thousand-Year Door sets an effect's
volume, its pitch wheel and its send to the reverb that way. The driver turns external interrupts off from the effect's
start to its last controller. No audio frame plays the effect without its controllers. The driver's thread then sleeps.
The sound plays from the audio and DSP interrupts.

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

Crystal Chronicles' driver doesn't use the handle either. The top byte of a Crystal Chronicles sound ID gives the kind
of sound: 0 for a piece of music and 1 for an effect. Its low 24 bits give the sound's number. A piece's number is the
number in its file's name. An effect's number is the game's. Below 4000, it's the block's number times 1000 plus the
effect's place in the block. From 4000 on, it's the number in the name of the effect's file.

The MusyX driver doesn't use the handle either. A sound ID for the MusyX driver is the sound's number in the rip's
catalogue.

The game starts its music by the sequence's file name. A Pikmin 2 sound ID is 0x80000000 plus the piece's number in the
driver's table of pieces. The table gives each piece's file, how the game makes the piece's object, the conductor of a
cave's music, the object's sound information, and the part at which a boss's music starts. An effect's sound ID is its
ID in the sound table (see "Ripping").

Animal Crossing's driver writes the handle itself. For a piece, the handle is the sequence player that plays the
piece. For an effect, it is the channel that plays the effect. The driver writes 0 when the sound ends. The top byte of
an Animal Crossing sound ID gives the kind of sound: 0 for a piece of music and 1 for an effect. A piece's ID gives the
museum's room in bits 16 to 23, the weather in bits 8 to 15 and the game's number of the piece in the low byte. The
game numbers the weather 0 for fine weather, 2 for snow and 3 for cherry blossom or falling leaves. In the rain, the
game plays another piece. The 24 pieces that play the hours change with the weather: the game mutes some of their
channels by the weather and the hour. The game numbers the museum's rooms 1 for the lobby, 2 for the paintings, 3 for
the fish, 4 for the insects and 5 for the fossils. Each room mutes some of the museum piece's channels. Every other
piece ignores both. The low 16 bits of an effect's ID give the game's number for the effect: its bank in bits 8 to 15
and its place in the bank in the low byte.

The Bonus Disc's driver writes the handle too. While the effect plays, the handle is the effect's entry in the sound
library's table. The driver writes 0 when the effect ends. A Bonus Disc sound ID is the sound library's number for the
effect: the effect's group in bits 16 to 31 and its place in the group in the low 16 bits.

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

### MusyX catalogue

A rip for `driver/musyx.c` holds the game's MusyX files in the library, as `FILE` chunks in `MusyX/`. A set of four
files holds one or more groups. The files take the set's number as their name: `MusyX/0.proj` (the project),
`MusyX/0.pool`, `MusyX/0.sdir` (the sample directory) and `MusyX/0.samp` (the samples). A song's file is
`MusyX/<number>.sng`. The catalogue, `MusyX/sounds.bin`, lists the sounds. Its values are big-endian:

```
0x00  u32  magic "GCMX"
0x04  u32  version (1)
0x08  u32  the number of sounds
0x0C  u32  the offset of the sounds' entries, 20 bytes each
0x10  u32  the number of group entries
0x14  u32  the offset of the group entries, 4 bytes each
0x18  u32  the number of words to write
0x1C  u32  the offset of the words, 8 bytes each
```

A sound's entry:

```
0x00  u8   kind: 0 for a song, 1 for an effect
0x01  u8   volume, or 0xFF for MusyX's default
0x02  u8   an effect's pan, or 0xFF for MusyX's default
0x03  u8   studio
0x04  u16  MusyX's ID: a song's ID (its MIDI set-up) or an effect's ID
0x06  u16  a song's group, for sndSeqPlayEx
0x08  u16  the number of the song's file, or 0xFFFF for none
0x0A  u16  the sound's first group entry
0x0C  u16  the number of the sound's group entries
0x0E  u16  the sound's first word
0x10  u16  the number of the sound's words
0x12  u16  the number of an effect's controller settings
```

A group entry holds two 16-bit values: the group's MusyX ID and the number of the set that holds the group. A word
holds two 32-bit values: an address in the game's memory and the value to write there. The driver writes a sound's
words, then sets up the aux effects. It pushes the sound's groups in their order. Then it starts the sound. With a
volume other than 0xFF, a song's volume goes to sndSeqVolume once the song starts. An effect's controller settings
follow its words in the list, in the same form. The first value holds a MIDI controller in its low byte. With bit 8
set, sndFXCtrl14 gives the controller the second value's 14 bits. Without it, sndFXCtrl gives the controller the
second value. The driver sets them in their order once the effect has started.

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
Pikmin 2 (USA), Pac-Man Vs. (USA), the menu of The Legend of Zelda: Collector's Edition (USA), Final Fantasy Crystal
Chronicles (USA), Animal Crossing (USA), the Pokémon Colosseum Bonus Disc (USA), Metroid Prime (USA, disc version 2),
Metroid Prime 2: Echoes (USA), Paper Mario: The Thousand-Year Door (USA), Tales of Symphonia (USA), Star Fox Adventures
(USA, disc version 1), Star Wars Rogue Squadron II: Rogue Leader (USA), Star Wars Rogue Squadron III: Rebel Strike
(USA), Pokémon Colosseum (USA) and Resident Evil Zero (USA). The Bonus Disc's profile matches the program in a TGC on
the disc (see below).

The `.gcsflib` contains:

- the sections of the game's `main.dol`, with `main` patched to branch to the driver, and the profile's other patches
  (they skip the clear of the OS's arena: Mario Golf's `ClearArena` and the same code inside Mario Power Tennis's
  `OSInit`. Final Fantasy Crystal Chronicles' patch ends the memory manager's heap below the driver);
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
  chunks. For Final Fantasy Crystal Chronicles, the disc's header, every piece's file (`dvd/sound/Music/*.bgm`), the
  four blocks of effects that the game loads (blocks 0 to 3 in `dvd/sound/SE/Block/`), the effects' SEP files from
  number 4000 on (`dvd/sound/SE/Sep/*.sep`) and the wave banks that the pieces and the effects name
  (`dvd/sound/Wave/*.wd`), as `FILE` chunks. For Animal Crossing, the disc's header and the game's sound data
  (`audiorom.img`), as `FILE` chunks. For the Pokémon Colosseum Bonus Disc, the disc header of the TGC's program and the
  effects' scripts and samples (`pm6fgm.sem` and `pm6fgm.ssm`), as `FILE` chunks. For a game for `driver/musyx.c`, such
  as Metroid Prime, the disc's header, the catalogue of the game's sounds, and the MusyX files of their groups and songs
  (see "MusyX catalogue");
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

Animal Crossing keeps no names for its music. Its code holds a table of 256 numbers. Each number gives the sequence that
the game plays for it. A rip names a piece after its number, for example `bgm046`. The 24 pieces that play the hours are
ripped once for each weather that changes them, for example `bgm001-fine`, `bgm001-snow` and `bgm001-sakura`. The
museum's piece is ripped once for each room, for example `bgm088-lobby`. Where several numbers play the same sequence,
the ripper rips the first of them. Numbers 0 and 83 to 87 aren't ripped: 0 stops the music, and 83 to 87 play effects.
Numbers 92 to 111 are the first parts of Kapp'n's songs, and 112 to 121 are their second parts. A rip of a first part
ends with the first part, and each second part has a rip of its own (see "Driver"). Sequences that the table doesn't
give aren't ripped as music. They play the effects, the voices and the sounds of some furniture.

The game keeps no names for its effects either. They play from six banks in the sequence of effects. A rip names an
effect after its bank and its place in the bank, for example `se2-017`. Effect 0 of bank 0 isn't ripped: the game's
slots use the number 0 for no effect. For some effects, the game adds a random number from 0 to 1 or from 0 to 3 to the
effect's number. A rip of such an effect plays the variant that the game's first random number picks. Each variant is
also ripped under its number. The game's continuous sounds, such as rivers and insects, play in other channels of the
sequence. The ripper rips neither them nor the villagers' voices.

The Pokémon Colosseum Bonus Disc plays its sequenced sound in one program, `tgc/pokedownload.tgc`. A TGC is a disc
image inside a disc. It holds a program's disc header, its `main.dol`, its file system table and its files. gcsfrip
opens the TGC and rips the program in it as it rips a disc. The profile matches the program's game ID, POKE01, and the
SHA-1 of the program's `main.dol`. A TGC starts with the big-endian word 0xAE0F38A2. Its word at 0x08 gives the offset
of the program's disc header. The words at 0x10 and 0x14 give the offset and the size of the file system table, and the
words at 0x1C and 0x20 those of the `main.dol`. The table gives each file's offset on the program's disc. A file's
offset in the TGC is that offset, minus the word at 0x34, plus the word at 0x24.

The program names its effects in `pm6fgm.h`, a C header next to the effects' files. Each `#define` in it gives an
effect's name and its number: the group and the place in the group. A rip takes those names, for example `SFX_cancel`.
The file of scripts holds four tables. Each is a word with the number of entries, then the entries. The third table
gives each group's first effect, and the fourth gives each effect's script. The program's file of scripts holds five
effects in group 0. An effect that the header doesn't name would take its group and its place in the group, for
example `se0-004`. The TGC holds two more files of the sound library, `sfxsem.sem` and `sfxmain.ssm`, with other
effects. The program never loads them. The ripper leaves them out.

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

Final Fantasy Crystal Chronicles keeps each piece of music in a file of its own in `dvd/sound/Music/`. The file's name
is `music` and the piece's number in three digits, for example `music100.bgm`. The game's disc library matches names in
any case. The ripper lists those files: 60 pieces. A piece's name is its file's name without the extension, for example
`music100`. A `.minigcsf` selects a piece by its number (see "Driver"). `--bgm` takes every piece. A piece's file starts
with `BGM `. It gives the piece's number and its wave bank's number in big-endian halves at 4 and 6. The bank is a file
in `dvd/sound/Wave/`. Its name is `wave` and the bank's number in four digits, for example `wave0100.wd`. The pieces
name 44 banks. One piece makes no sound. Its only note plays at a volume of 0. Its other tracks stop at once. The ripper
skips it. The game streams 21 more pieces, from `dvd/sound/Stream/`. GCSF doesn't rip streams.

The game's effects are in `dvd/sound/SE/`. The game plays an effect by its number. It takes a number below 4000 as an
effect in one of four blocks of effects, numbered 0 to 3. A block is a file in `Block/`. Its name is `se` and the
block's number in three digits, with the extension `.seb`. A block's file starts with `SeBlock`. A big-endian half at 10
gives its number of places. A table of big-endian words at 16 gives each place's effect, or -1 for none. The block's
number times 1000 plus the effect's place gives the effect's number. A rip names such an effect after the block's file
and the place, for example `se000-001`. An effect from 4000 on is a SEP file in `Sep/`. Its name is `se` and the
effect's number in six digits, for example `se010001.sep`. A rip names the effect after its file, for example
`se010001`. A SEP file starts with `SeSep`. The effect's wave bank's number is a little-endian half at 0x11. An effect
of a block gives its bank the same way, at the second byte of its information. The blocks' effects use banks 0 and 500.
The game loads those two banks with the blocks, into Red's first two slots for banks. A track's command 0x21 picks a
wave from a slot. Effects of SEP files use the command too. The game can't play the five SEP files numbered below 4000.
The ripper leaves them out. It also leaves out a fifth block, `se012.seb`. The game never loads it. The ripper lists 261
effects in blocks and 2,137 in SEP files. The library holds the four blocks, the SEP files from 4000 on and the 113
banks that the effects name. The ripper skips 262 effects that make no sound. 258 of them start no note. The other four
play their notes on waves that their bank leaves empty.

Metroid Prime streams most of its music. Four songs are sequenced. `MidiData.pak` holds them as CSNG resources. The
game's packs are in Retro Studios' format, version 3.5: a list of the named resources, then a table of every resource
with its type, ID, size and offset. A resource can be compressed with zlib. A CSNG resource starts with five
big-endian words: 2, MusyX's song ID, the song's group, the ID of the audio group set (AGSC) that holds the group, and
the size of the song's file. The song's file follows. `AudioGrp.pak` holds the game's 72 audio group sets. An audio
group set starts with two strings: a folder and the set's name. MusyX's four files follow, each after a word that gives
its size: the pool, the project, the samples and the sample directory. The ripper lists the four songs in the pack's
order. A song takes the name that the pack gives its resource, for example `Main_Plaza`. `--bgm` takes every song. The
four songs use group 53, in the audio group set named `test`. Each song holds a few long notes.

The game's sound effects follow the songs, in the order of their IDs. A set's project lists its effects in a table for
each effects group. An effect's row has 10 bytes: the effect's ID in a big-endian half, the ID of its macro in another,
then a byte each for the most voices that may play the effect at once, its priority, volume, pan, key and volume group.
MusyX never starts an effect whose row allows no voices. The ripper leaves out the one such effect. A rip names an
effect after its audio group set and its ID in four decimal digits, for example `Scarab-0721`. The driver starts an
effect with a volume and a pan of 0xFF. MusyX then takes them from the effect's row. In the game, an effect's place in
the world can change its volume and pan. A rip plays every effect on studio 0. Of the 3,166 effects, 1,023 make no
sound. The ripper skips them. 932 start a sample and end their macro in the same step. MusyX frees the voice before the
sample starts. 86 scale their volume to 0. Four make a random choice that can skip their sample. In a rip, the choice
always skips it. One plays its sample at a volume of 0.

Metroid Prime 2: Echoes streams its music. Its packs have the same format as Metroid Prime's. `MidiData.pak` holds
Metroid Prime's four songs. No pack holds the audio group set that they name. The ripper leaves them out. The game's 20
packs hold 690 audio group sets. Each set but one holds one group of effects. The other holds no group. A set may be in
more than one pack. An audio group set starts with a big-endian word of 1, the set's name, and the group's ID in a
big-endian half. Four big-endian words give the sizes of MusyX's four files. The files follow: the pool, the project,
the sample directory and the samples. None of the audio resources that the ripper reads is compressed. The string table
`audio_name_lookup_STLC` in `AudioGrp.pak` names the effects. It starts with a big-endian word that gives the number of
names. The names follow. Each ends in a 0 byte. A name's place in the table is its effect's ID. A rip names an effect as
the table does, for example `SFXfnt_x_titlemusic_lp_00L_looped`. An effect without a name would take its set's name and
its ID, as in Metroid Prime. The game adds ARAM for the samples of each audio group set that it loads. A rip gives MusyX
0x400000 bytes of ARAM from the start instead. The ripper leaves out the 15 effects that allow no voices. Of the other
3,461, 160 make no sound. The ripper skips them. 147 start no sample. 10 scale their volume to 0. Of the other three,
one makes a random choice and one tests a variable of MusyX's macros. The third plays too quietly to count as sound.

Paper Mario: The Thousand-Year Door streams its music. `sound/proj/pmario.proj`, `.pool`, `.sdir` and `.samp` are
MusyX's four files. They hold the game's 12 groups. Six are effects groups, with 1,376 effects. The other six hold
songs that the game plays as jingles. A rip leaves them out. The game pushes all 12 groups at start-up and keeps them.
A rip pushes only the effect's group. The game's code lists its sounds in a table at 0x80316B28: 4,242 rows of 20
bytes. A row starts with the address of the sound's name. The next word gives the sound's kind in its top bits and
its ID in its low 16 bits: 0x20000000 for an effect of MusyX, 0x40000000 for a song and 0x80000000 for a stream. A row
of zeros is unused. The bytes at 0xC, 0xD and 0xF give an effect's volume, its pan and its send to the reverb. The
big-endian half at 0x12 bends its pitch. The game starts an effect through sndFXStartEx with a volume of 0x7F and the
row's pan. It then sets MIDI controller 7 to the row's volume, the pitch wheel to 0x2000 plus the bend, and controller
91 to the send. Controller 91 feeds aux A, MusyX's high-quality reverb of 3 seconds. Aux B holds MusyX's chorus. The
game adds its room's reverb to a send above 0. A rip plays no room. Most of the game's sounds play at a place in the
world. Their volume and pan then follow the listener. A rip plays each effect as its row gives it. Several rows can
play one effect. The ripper takes the first row with a volume above 0, or the first row if none has one. A rip names
the effect as that row does, for example `SFX_PRESS_START1`. The table plays 1,304 of the effects. The other 72 are
named after the set and their ID, for example `pmario-0051`. They play at MusyX's defaults. Every row of two effects
gives a volume of 0. They make no sound. The ripper skips them. A rip of the game holds 1,374 effects.

Tales of Symphonia's code lists its music in a table at 0x801F9830: 8 bytes for each song. An entry gives the address
of the song's file name, then MusyX's song ID and the song's kind, a big-endian half each. A song ID of 0xFFFF ends the
table. The songs' files are in `S/`, for example `S/bgm_t000.song`. The code names them in lower case, and the game's
disc library matches names in any case. A song's file is a MusyX song (an SNG) without a header. The music's group,
group 0, is in `S/inst.snd`. That file starts with the number of its parts, 4, then each part's offset and size, a
big-endian word each. The parts are MusyX's project, pool, sample directory and samples. The ripper lists the table's
111 songs in its order. A song is named after its file without the extension, for example `bgm_t000`. `--bgm` takes
every song. The game plays a song at the music's volume of its options, 127 at their default. The play function sets
the reverb of aux A for each song (see the profile in `src/rip/rip.cpp`). A song's catalogue entry holds those
settings. The disc holds six songs that the table leaves out: `bgm_etc004` and `temp1` to `temp5`. The game's code
names none of them, and the ripper leaves them out. Both discs hold the same code and the same music. A rip of either
disc gives the same set. The game's sound effects and voices aren't ripped.

Star Fox Adventures' code lists its music in a table at 0x802C5E80: 99 entries of 16 bytes. An entry gives MusyX's song
ID in a big-endian half and the song's group in the next byte. The address of the song's name follows at +4.
`audio/midi.wad` holds the songs by ID: their sizes in 100 big-endian words at its start, then the songs from 0x1A0,
each padded to 32 bytes. The music's groups are in two sets of MusyX's files in `audio/`: `starfoxm` and `starfoxs`,
each as `.pro` (the project), `.poo` (the pool), `.sdi` (the sample directory) and `.sam` (the samples). Most songs use
group 0 of `starfoxm`. Songs 0x4A to 0x63 use groups 18 to 54 of `starfoxs`. At start-up the game pushes group 0 of
`starfoxm`, then groups 1 to 55 of `starfoxs`, and keeps them all. A rip of a song pushes the same groups in the same
order. The ripper lists the 99 songs by ID. A song takes the name that the table gives it, for example `starfox_map`.
`--bgm` takes every song. The game starts its music through the triggers in `audio/data/Music.bin`, 16 bytes each. A
trigger gives its number and the song's ID in big-endian halves at its start, and the song's volume in its byte at 0xC.
A song plays at the volume that most of its triggers give it. Of volumes that tie, the first one in the file wins.
Trigger 0xEC plays a song that its caller picks. A song that no other trigger names takes that trigger's volume, 80. The
game starts a song at volume 0 and raises it to the trigger's volume over 0.5 s. A rip starts the song at the trigger's
volume. `midi.wad` holds a 100th song, 0x33. A trigger names it, but the table doesn't, and the game never plays it. The
ripper leaves it out. The game's sound effects aren't ripped.

Star Wars Rogue Squadron II: Rogue Leader packs most of its files in `Rogue2/data.dat`. `Rogue2/data.dir` lists them in
lines of text: a file's path, padded with spaces to 64 characters, then its offset in the pack, its size and two more
numbers, split by tabs. The game's sound is in `audio/data`. The words of `audio/data` are big-endian. An offset in
`audio/data` counts from the file's start. The word at 4 gives the offset of the table of banks. The table gives the
number of banks, 39, then the offset of each bank's entry. An entry gives the offsets and sizes of MusyX's four files:
the project, the pool, the sample directory and the samples. Next come the offset and size of a block before the bank's
songs, the number of the songs, the offset of their table and the offset of the bank's list of groups. A song's row in
the table gives the song's offset and size, then its group and its ID in big-endian halves. The game's music hall lists
114 songs at 0x80060C38, in entries of 16 bytes. An entry gives MusyX's song ID and group in big-endian halves, then the
address of the song's name in UTF-16. The byte at 0xA of an entry gives the volume at which the hall plays the song. The
banks hold 10 more songs. The ripper lists the hall's songs in the hall's order, then the others by bank. A song from
the hall takes the hall's name, for example `Dogfight_Action`. Another song is named after its group and ID, for example
`Song_0x45-0x0A`. `--bgm` takes every song. Most songs play samples of bank 20, the bank of the hall's first song. A rip
of a song pushes the groups of bank 20, then those of the song's bank, each in the bank's order. A song plays at the
volume that the hall gives it. The hall fades a song in over 300 ms. A rip leaves the fade-in out. The game's menus
start their songs through 0x7FD4CE78 in the game, with a song's group, its ID and a volume. They play `Song_0x6E-0x65`,
which the hall doesn't list, at volume 0x67 in two of their three calls. A rip plays it at that volume. No call in the
game's code gives a volume for the other songs that the hall doesn't list. The levels' scripts probably give them. A
rip plays them at MusyX's full volume. The game's sound effects aren't ripped.

`main.dol` is a loader. It holds the OS, MusyX and a second DOL: the game. The loader maps the game in at 0x7FC00000.
The driver runs only the loader's code. A rip skips the loader's set-up of the game's heaps and the game's static
constructors. The profile in `src/rip/rip.cpp` lists the rip's other patches.

Sixty of Rogue Leader's songs play only streams. Each of them holds a single note. The note's program plays stream
buffers: samples of one second that loop. A stream buffer has the sample type 5 in its bank's sample directory. The game
probably fills the buffers from the disc as the song plays. The numbers of the disc's `Rogue2/dg000NN.vsp` files match
those of the banks that hold the songs with streams, 21 to 28 and 30 to 37, and that of the music bank, 20. GCSF doesn't
rip streams. These songs make no sound in a rip, and the ripper skips them. Eight more songs play a stream along with
their notes: The Battle Of Yavin I and III, Hyperspace, Imperial Attack, The Asteroid Field, Celebration & End Titles,
and two songs that the hall doesn't list (group 0x4D's song 0x1A and group 0x61's song 0x3D). A rip of one of them would
play only its notes. The ripper leaves them out, as streams. The profile in `src/rip/rip.cpp` lists them.

Pokémon Colosseum keeps most of its files in FSYS archives (see `src/rip/fsys.h`). The module `common_rel` in
`common.fsys` holds the game's table of sounds: 1,236 entries of 12 bytes. An entry's first byte has bit 7 set for an
effect. A song's entry gives its group at +1, its reverb's preset at +2, the number of its file at +3 and MusyX's song
ID in a big-endian half at +4. The module's table of 79 files gives each file's FSYS ID. `bgm_archive.fsys` holds the
songs under those IDs and names them. The music's groups are in `common.fsys`: group 0 in `snd_music_proj`,
`snd_music_pool` and `snd_music_sdir`, and group 1 in the same files of `snd_music_atmos`. Their samples are
`sound/snd_music.samp` and `sound/snd_music_atmos.samp`. The profile in `src/rip/rip.cpp` gives the tables' places in
the module. The ripper lists the table's 78 songs in its order. A song is named after its file, for example
`title_song`. `--bgm` takes every song. `null_bgm_song` holds no notes. The game plays it to silence its music. The
ripper skips it. The game plays its songs at volume 127. Studio 0's aux A holds MusyX's high-quality reverb. The
module holds 12 presets of the reverb's settings. A song's entry picks one. A song's catalogue entry holds the
preset's settings. Preset 0 leaves the reverb as it is. Only `null_bgm_song` picks it. The game's sound effects aren't
ripped.

Resident Evil Zero comes on two discs. Both hold the same `main.dol`. The game's code lists its songs in a table at
0x80243BCC: 12 bytes for each song. An entry gives MusyX's song ID and the song's group in big-endian halves, then the
addresses of two names: the archive that holds the group, and the song's file. A song ID of 0xFFFF ends the table. The
files are in `bio0/sound/`. A song's file has the extension `.son`, for example `bgm_00.son`. It holds a MusyX song (an
SNG) without a header. An archive (`.arc`) starts with a word 0, the number of its parts and the size of its header.
Each part then has 0x20 bytes: its offset and size, its name in 8 bytes and its type in 16. The parts are MusyX's
project (`proj`), pool (`pool`) and sample directory (`sdir`). A file with the archive's name and the extension `.sam`
holds the samples. Every word is big-endian. The game's disc library matches names in any case. The table names
`bgm_0c`, for example, and the first disc holds `bgm_0C.son`.

The table lists 230 songs. Each disc holds the songs of its part of the game in `bio0/sound/`: 79 on the first disc and
129 on the second. 59 of them are on both discs. That makes 149 songs in all. Sixteen more songs are the music of
cutscenes. Each disc holds eight of them. Neither disc holds the other 65. A cutscene's files are in `bio0/demo/`. The
game loads a cutscene's archive from a compressed file, for example `dd00.alz`. The compressed file's bits come from
each byte's lowest bit up. Its header gives a mode in 8 bits and the archive's size in 32. Mode 0 stores the archive. In
modes 1 and 2, a 1 bit starts a literal byte in the next 8 bits, and a 0 bit starts a copy of earlier bytes. The copy
gives its distance less 1, then its length. Mode 1 gives the distance in 10 bits. Mode 2 gives it as a class and a
value. A class is the number of 0 bits before a 1 bit. Classes 0 to 3 give the value in 2, 4, 6 and 10 bits. The length
is a class and a value in both modes. The archive's part 7 is an archive too. Its parts hold the cutscene's music:
MusyX's pool, project and sample directory, then the song. Part 1 of the cutscene's samples, for example `dd00sam.arc`,
holds the music's samples. The ID of the project's first group is the group in the song's row of the table. Of the
cutscenes that hold the group, a rip takes the files of the first by name. Two cutscenes of the second disc share
`bgm_73`, and three share `bgm_7c`. Their files are the same.

The ripper lists the songs that the disc holds, in the table's order. A rip's library takes the disc's number:
`ResidentEvilZeroDisc1.gcsflib` or `ResidentEvilZeroDisc2.gcsflib`. Rips of both discs can then share a folder. A song
is named after its file without the extension, for example `bgm_00`. `--bgm` takes every song. 59 songs are music. Their
names start with `bgm_`. The other 106 are the ambiences of the game's areas. Their names start with `train_`, `inst_`,
`grou_`, `chur_`, `fact_` or `plan_`. Every note of `bgm_4b_3` has a velocity of 1. It makes no sound, and the ripper
skips it. The cutscenes' songs end. The others loop. Some of the cutscenes' songs start with a rest that times the music
to the scene. A rip plays the rest. `bgm_78` is silent for its first 52.6 s. The game pushes a song's group and plays
the song through sndSeqPlayEx. A room or a cutscene starts a song at the volume of one of the game's sound options. Each
option starts at 127. A rip plays a song at 127. A cutscene raises its music to that volume over 133 ms. A rip leaves
the fade-in out. No room plays `bgm_52`. The game's extras play it at 100/127 of the music's option, and a rip plays it
at 100. At start-up the game gives aux A and aux B MusyX's high-quality reverb. Aux A's lasts 1.5 s, and aux B's 1 s. A
song's catalogue entry holds their settings. The game also sets MusyX's master volumes to 127 at start-up. A rip leaves
them at MusyX's defaults. The output is the same. Many of the game's cutscenes stream their music and voices from
`bio0/demo/`. GCSF doesn't rip streams. The game's sound effects aren't ripped.

Star Wars Rogue Squadron III: Rebel Strike packs its files in `Rogue3/data.dat` as Rogue Leader does. `Rogue3/data.dir`
pads each number with spaces on its left. The 172 banks in `audio/data` have Rogue Leader's layout. Many banks hold
copies of the same groups, with the same effects. The banks' projects list 871 effects. Each effect ID belongs to one
group. A rip of an effect pushes the effect's group from the first bank that lists the effect. The effect plays through
sndFXStartEx with the volume and the pan of its group's table. The library holds the 45 banks that the effects take. A
rip names an effect after its ID, for example `RebelStrike-0048`. An index at the start of `audio/data` names the
game's sounds. The ripper doesn't read it. The game's songs aren't ripped.

Rebel Strike's `main.dol` is a loader. It holds the OS and the disc's library. The game's code and data, MusyX among
them, are a second DOL. `main.dol` holds that DOL packed at 0x80200000: two big-endian words, the packed size and the
size, then the DOL as zlib packs it. The loader's start-up unpacks the DOL into ARAM. The game then runs from 0x7F200000
through the CPU's page table. GCSF's CPU translates addresses only through the BATs. A rip places the game's DOL 16 MB
above its addresses, from 0x80200000, in place of the sections of `main.dol` that it overlaps. The rip replaces the
loader's start-up with code that maps 0x7F000000 to 0x7FFFFFFF there with IBAT3 and DBAT3. MusyX keeps its state in the
loader's memory. The game's memory hooks are two mallocs and a free. The rip patches sndSetHooks to take the driver's
malloc for both. Each level sets a reverb on studio 0's aux A, or none. A rip sets none.

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

A MusyX game's songs follow other rules. MusyX plays a song in one section, or in up to 16 sections. A song in sections
puts each of its tracks in one of them. Most of Star Fox Adventures' songs are in sections. Each section has a loop
point and loops apart from the others. A section loops when one of its tracks ends in a loop entry. MusyX counts each
section's loops. The ripper reads the counts. For Mario Golf: Toadstool Tour, Mario Power Tennis and WarioWare, Inc.:
Mega Party Game$!, the ripper watches MusyX's slot for each of a song's tracks instead. A slot's first word is the tick
at which its track's next region starts. The ticks of a section's tracks fall when the section loops. A looping song's
length runs until each section that loops has looped twice, plus the fade. A song that ends leaves MusyX's lists of
playing songs. The rip of such a song plays until half a second after the last sound. MusyX's output keeps a constant
level on each channel after a song has stopped. For these songs, a sample therefore counts as sound when it differs by
more than 8 from its channel's average over about the last 50 milliseconds. The rules above for loops before the first
sound, loops without a sound, 180 seconds without a sound and the 600-second limit still apply.

Final Fantasy Crystal Chronicles' pieces follow the MusyX rules, with Red's state in place of MusyX's. Red plays a
piece in one of its three music controls. A piece's loop command takes all of its tracks back to their state at the
start of the loop. The ripper watches the code that does that. A looping piece's length runs to the end of its second
loop, plus the fade. A piece that ends leaves its control. Its rip plays until half a second after the last sound.
Sound is a sample that differs by more than 8 from its channel's average over about the last 50 milliseconds.

The game's effects follow the same rules. Red plays an effect on the tracks of its fourth control. Each track loops on
its own. A track's last command can take it back to the start of a loop for ever. The ripper watches the code that does
that. The first track that loops gives the effect's loops. Another track that is still busy holds the fade back until it
has looped twice too. Red can also vary a track's steps at random. It takes the random numbers from a fixed table, one
after another. A rip plays the steps that the first numbers give. Such an effect's loops differ from pass to pass. The
ripper measures each loop as it plays.

Animal Crossing's sound library runs the Nintendo 64's sequence player. The player runs a script for a piece. Each of
the piece's channels and each of their layers runs a script too. Each script is a track for the rules above. The
player's script is the root. The channels and layers follow in the order in which they start. A counted loop counts as a
subroutine. A piece ends when its sequence player stops. An effect plays in a channel of the sequence of effects. That
channel's script is the effect's root. The channel's layers follow. An effect ends when the sound library frees its
slot.

The Bonus Disc's effects follow the MusyX rules, with the sound library's list of the effects that play in place of
MusyX's lists. An effect that ends leaves the list. Its rip plays until half a second after its last sound. AX's output
rests at 0 once an effect has stopped. Sound is therefore a sample louder than 8. The JAudio games use the same rule.
The analysis doesn't check for loops. None of the program's effects loops.

The effects of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door and Star Wars Rogue Squadron
III: Rebel Strike follow the MusyX rules too. An effect plays on MusyX's voices. Each voice runs a macro: steps that
start a sample, change the sound and wait. The ripper reads every voice once a frame. A rip sends an effect no events,
such as a key-off. A voice whose macro has ended, or waits for an event, therefore changes no more. Such a voice has
settled. The sound changes when a voice starts or stops, moves to another step, or starts to wait. A macro loops when a
LOOP step without a count jumps back. The first voice that loops gives the effect's loops. A looping effect's length
runs to that voice's second jump back, plus the fade. Another voice that is still busy holds the fade back until it has
looped twice too, or has settled. An effect whose voices have all settled holds while its samples loop. It loops at the
later of two points: 10 seconds after its sound last changed, and the second time since then that each playing sample
has gone back to the start of its loop. An effect whose voices have all settled while no sample plays has ended. The
other rules for MusyX's songs still apply. Some of Rebel Strike's macros jump back through GOTO instead of LOOP. Most of
them make a random choice or test a variable first. The ripper finds no loop in them. 19 of the game's effects play to
the 600-second limit that way.

The ripper lists and skips the sounds that never make a sound. `--no-length` leaves out the analysis. Without the
analysis, no sound is skipped and the files have no length tags.

Tags written by `gcsfrip`: `title` and `gcsf_sound` (the sound's name, or the file's name for a sequence that no sound
plays), `game`, `length` and `fade`, `gcsf_mode`, `gcsfby`, `utf8=1`, and optionally `artist`, `year` and `copyright`.

## Copyright

A `.gcsflib` contains copyrighted game code and data. Rips are for personal use.
