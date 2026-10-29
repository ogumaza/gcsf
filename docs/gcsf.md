# GCSF: GameCube Sound Format

Version 1. GCSF stores sequenced music from Nintendo GameCube games so that it can be played back as the game plays it.
A GCSF rip holds the game's code. A player runs that code on an emulated GameCube: the game's OS, its sound library and
its DSP program. The game plays the sound itself. The rip is therefore exact.

GCSF works like 2SF for the DS. A rip requires a driver profile for the game (see "Ripping"). Version 1 rips games
whose sound library is JAudio, in either of its generations: JAudio1 or JAudio2. JAudio is the sound library of
Nintendo EAD's JSystem. Version 1 also rips games on four other sound libraries: Factor 5's MusyX, Square's Red driver,
a port of Nintendo's sound library for the Nintendo 64, and HAL Laboratory's sound library.

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

Rips contain one of GCSF's drivers. Most drivers call the functions of a game's sound layer. The MusyX driver calls the
functions of MusyX itself. Several games use it (see below). The game's profile names the driver. A driver is loaded at
0x81700000. Its entry point is at the same address. The rip patches the game's `main` to branch to it. The driver calls
the game's functions to do what the game's start-up does for sound. It leaves out the graphics. Every driver includes
`driver/driver.h`. The header holds the C types, the values of the parameter block's first words (see below) and the
helpers that read the game's memory.

Four Swords Adventures' driver is `driver/four_swords.c`. It sets up the video interface, JSystem's heaps and ARAM
manager, the game's audio object and disc thread, and the game's audio set-up. It loads every wave bank into ARAM. Then
it starts the sound by its JAudio sound ID. The game's music start mutes some tracks of one piece, `JA_BGM_SELECT`:
every child of the root track's child 0 but children 2 and 7. The driver mutes the same tracks once the piece has
started. From then on it calls the game's audio frame process once per video retrace.

Twilight Princess's driver is `driver/twilight_princess.c`. It sets up the video interface, JSystem's heaps, ARAM
manager and thread switch manager, the game's heaps and disc thread, and the game's audio set-up. It loads the wave
groups of the wave list (see below) as the game's scene manager loads them. Then it starts the sound. From then on it
calls the audio manager's frame process every second video retrace. The game's frames are a 30th of a second. An entry
of its wave list holds a wave bank in its high 16 bits and a group of that bank's wave system in its low 16 bits.

The Wind Waker's driver is `driver/wind_waker.c`. It sets up the video interface, JSystem's heaps and ARAM manager, and
the game's disc thread, as the game's machine set-up does. Then it calls the game's audio process once a frame until the
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
is two video retraces, as in the game. The game starts one piece, LuigiSings, through a function of its own. That
function reads the game's state: Luigi's health, the room's lights, the room and the room's sound information. It picks
the tracks that play and sets the piece's tempo. The game's sound frame keeps them up to date. A sound ID of LuigiSings
holds a state in bits 16 to 23, from 1. The driver's game profile holds a table of the states. For a state, the driver
gives the game the state and calls that function. The function puts the piece's JAISound in a word of the game's. The
driver copies that word to the sound's handle once a frame.

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

The sound library also plays Pikmin's effects sequence and its events' sequence from its start-up. For a sound effect,
the driver loads the wave groups that the game's set-up of its first scene of play loads for the effects. Then it waits
for the tracks that play the effects. The driver sets the game's scene to the play of a stage. The game plays two of
the system's effects only there. The driver then starts the effect with the game's function for its kind: an effect of
the system, an effect of the player, or an action of an event. For an event's action, the driver first creates an event
of the action's type at the camera. The sound starts when a child of the effect's track starts to play. The handle then
points at a word that holds that child. Once a frame, the driver calls the game's update of the camera. That update
gives each event its volume and pan by its place against the camera. An event at the camera plays at full volume, in
the middle. The sound of the formation plays in a track of the effects sequence while the player moves the formation.
For that sound, the driver calls the game's function for a move of the formation once a frame. It passes the value of
port 2 of the port list as x and that of port 3 as y. The function writes x and the move's length to ports 2 and 3 of
the formation's track. The function writes 1 to port 0 as the move starts.

Donkey Kong Jungle Beat's driver is `driver/jungle_beat.c`. It sets up the video interface, JSystem's heaps and ARAM
manager, and the game's heaps. It loads the game's sound files as the game does: the start-up archive, the sequence
archive, the chord archive and the chord database. The game decodes the start-up archive from Yaz0 as it loads it. The
driver then creates the game's audio manager, loads the wave groups of the wave list and starts the sound. Music starts
in the game's BGM player. The player sets each of a piece's tracks to a state that tables in the game's code give. Some
tracks start silent. The game turns them on later, or never. A piece whose first state silences every track gets its
next state at once. The game turns that piece's tracks on later. From then on the driver calls the audio manager's frame
process once per video retrace. An entry of its wave list holds a wave bank in its high 16 bits and a group of that
bank's wave system in its low 16 bits.

A chord effect plays over a stage's music (see "Chord effects" in "Ripping"). The driver starts the music of a stage
through the game's stage object. Then the driver turns each of the music's tracks off at once with code 2 of the BGM
player's track states. The tracks still run. The driver calls the game's `startChordSe` with the effect's first record
until one of the chord players is busy. After each audio frame, the driver runs the frame process of the game's rhythm.
In the game, an object of the scenes runs that process. The status turns 1 when the first step of the effect sounds. The
handle holds the JAISound of the step that sounds. While the next step waits for a beat, the handle holds the JAISound
of the last step. The effect ends when no step sounds and none waits. The driver looks up the effect's record before
the waves load. The work between the last frame process and the start of another sound then stays as it was. The
timing of a start against the DSP's frames sets the samples of the output.

Mario Kart: Double Dash!!'s driver is `driver/double_dash.c`. It sets up the video interface, JSystem's heaps and ARAM
manager, and the game's audio heap. It loads the start-up archive as the game does and passes it to the game's audio
set-up. Then it loads every wave bank that the archive names. For a voice, the driver then loads the start-up archive in
the first one's `baac` block that holds the voice's bank. The game loads one of those archives at a time, with the same
function. The archive's wave bank goes into ARAM as the game's loader reads the archive. Then the driver starts the
sound. From then on it calls the audio frame process once per video retrace. Some effects are level sounds, such as the engines. The game's start function gives a
level sound a lifetime of one frame. The game starts the sound again each frame for as long as it should play. The
driver starts it again before each frame until the sound ends.

Mario Golf: Toadstool Tour's driver is `driver/mario_golf.c`. It sets up the video interface, the OS's arena and heap,
the ARAM and the game's heaps. The game's sound set-up then starts MusyX, and the driver loads the game's two MusyX
sound groups. Mario Power Tennis's set-up also fades MusyX's two masters from 127 to 101 over 200 ms. The game plays
no music before the fade has ended. For Mario Power Tennis, the driver waits 13 video retraces after the set-up. The
driver starts the song by its number in the game's song table, or the jingle by its number in the jingle table. The
game starts a jingle from another thread. The jingle sounds half a second later. A sound effect plays through the game's
functions for effects instead. The sound ID gives what the driver loads first: the effect's voice bank, its court's bank
or the game's narrations. Mario Golf reads a voice bank's samples from the disc in chunks. The chunks go into two
buffers in turn. MusyX queues each chunk's upload to ARAM. On the console, each upload ends before the drive has read
the next chunk. GCSF's drive answers sooner than the ARAM DMA moves a chunk. While a Mario Golf voice bank loads, the
driver therefore limits MusyX's queue to one upload. For a Mario Power Tennis effect of a group that no table of the
game lists, the driver loads the group as the game's bank loader loads its groups. The driver's thread then sleeps. The
sound plays from the audio and DSP interrupts. Mario Power Tennis plays through the same driver.

WarioWare, Inc.: Mega Party Game$!'s driver is `driver/warioware.c`. It sets up the game's heaps. The game's file loader
then reads the game's five sound files from the disc. The driver puts each one where the game's sound set-up looks for
it. The sound set-up initialises the ARAM, the audio interface and MusyX. It loads every MusyX sound group into ARAM and
sets up the reverb. The driver then plays the sound through the game's play function. It passes the mode in which the
game plays music. The mode only decides which of the function's slots a sound may take from another. The driver's
thread then sleeps.

Mario Party 4's driver is `driver/mario_party.c`. The game runs MusyX through Hudson's sound manager. The driver starts
the disc library and gives the sound manager a heap of the size that the game gives it. The sound manager's start-up
reads its file of sound data and the head of the stream file. It initialises the ARAM, the audio interface and MusyX. It
also sets up the reverbs and the stream slots. On a console set to stereo, the game sets MusyX's surround mode. The
driver sets it too. It loads the base groups that the game loads at boot. Next comes the group set that the game's
sound test loads for the song. The driver then starts the song by its number in the sound manager's table. For an
effect, the driver loads the group that holds it, unless that group is a base group. It then starts the effect through
msmSePlay, by its number in the sound manager's table of effects, without parameters. The driver's thread then sleeps.
The sound manager runs its fades and its effects from the audio interface's interrupt.

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
The driver therefore calls the function that this one calls, with the same values. That function adds a random number
from 0 to 3 or from 0 to 1 to the numbers of some effects. The game then plays the effect of the sum. The rip patches
out both adds. Each rip plays the effect that its number names. The sound library frees the slot when the effect ends.

Doubutsu no Mori+, the game's Japanese release, runs the same driver. The release's sound library is an earlier build of
the same port. The driver calls the same functions at other addresses.

Doubutsu no Mori e+, a later Japanese release of the game, runs the driver too. The release's sound library is a later
build of the port. The build numbers the pieces and their sequences with 16 bits. The game's table of pieces holds a
16-bit sequence number for each piece. The driver reads and writes the table in 16-bit entries. In this build, the
function that starts the sound library takes the start-up screen's sound, the size of that sound and the size of ARAM
for the library. The game works out the size of ARAM from the size of `audiorom.img`. The driver passes the same size.
In this release, the music discs' pieces are 208 to 367. The game mutes six channels of the island's piece unless the
islander is on the island. The driver sets whether the islander is there before it starts a piece. A port holds a byte.
The only sequence that ends at once has a number above 255. For Kapp'n's songs, the driver uses a piece whose sequence
plays no note and ends after one tick.

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
Pokémon Colosseum, Resident Evil Zero, Nintendo Puzzle Collection and the NDDEMO tech demo. The driver calls MusyX's
functions, not a layer of the game's code over them. Other games whose sound library is MusyX can use it too. A game's
profile gives the addresses of the functions and the values that the game passes to MusyX. The driver first calls the
game's start-up functions that come before its sound, such as the set-up of the disc. Next come the ARAM, the ARAM's
queue and the audio interface. The driver then sets MusyX's memory hooks. The hooks hand out the OS's arena from the
bottom up. sndInit follows, with the game's numbers of voices and the game's share of ARAM. Then the driver sets the
game's output mode. MusyX starts each of its volume groups at a volume of 0. A song's start sets the song's group. An
effect's start leaves the effect's group alone. The start-ups of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario:
The Thousand-Year Door, Tales of Symphonia, Star Fox Adventures, Rogue Leader, Rebel Strike, Resident Evil Zero,
Nintendo Puzzle Collection and the NDDEMO tech demo call
sndVolume(127, 0, 0xFF). That call gives every group its full volume. The driver makes it for these games too. The
driver reads the sound's entry from the rip's catalogue (see "MusyX catalogue" below). The entry can give words for the
driver to write into the game's memory, such as the settings of the game's reverbs. A game's profile can name aux
effects for studio 0. The driver prepares them and passes them to sndSetAuxProcessingCallbacks. The driver writes the
entry's words before it prepares the effects. A word can therefore change them in the driver's block. Yoshi's Cookie's
songs in Nintendo Puzzle Collection do that (see "Ripping"). After the words, the driver can set the volume of every
group through sndVolume, and MusyX's master volumes for music and for effects through sndMasterVolume. Both calls take a
time of 0. The profile's parameters or the entry's words give the volumes. Each group that the entry names is in a set
of four files in the rip: the project, the pool, the sample directory and the samples. The driver reads them and pushes
the group with sndPushGroup. Groups in a row from one set share its files. MusyX copies the samples to ARAM. A song
plays from its file through sndSeqPlayEx, with the song's group and MusyX's song ID. An effect plays through
sndFXStartEx. A volume or a pan of 0xFF there takes the effect's value from its group's table of effects. The entry can
then set the effect's MIDI controllers, through sndFXCtrl and sndFXCtrl14. Paper Mario: The Thousand-Year Door sets an
effect's volume, its pitch wheel and its send to the reverb that way. The driver turns external interrupts off from the
effect's start to its last controller. No audio frame plays the effect without its controllers. The driver's thread then
sleeps. The sound plays from the audio and DSP interrupts.

`driver/musyx_upload.c` is the driver of Pokémon Channel. It builds `driver/musyx.c` with one addition. The game's
sample file is larger than main memory. The driver pushes each group without samples in memory. MusyX then calls its
upload callback (sndSetSampleDataUploadCallback) for each 0x20000 bytes of the group's samples. The driver reads each
chunk from the rip's sample file into a buffer. MusyX copies the buffer to ARAM through its queue. Before the driver
reads the next chunk, it waits until the queue is empty (aramSyncTransferQueue). The driver's parameter block holds two
more words after the other ones: the addresses of sndSetSampleDataUploadCallback and aramSyncTransferQueue. Pokémon
Channel's start-up fades the master volume to 100 and every group's volume to 127 over 100 ms. The driver sets both at
once.

Every driver's parameter block is at **0x81700010**. The block's words are big-endian:

```
0x00  u32  magic "GCSD"
0x04  u32  version (1)
0x08  u32  status (written by the driver): 0 booting, 1 playing, 2 finished, 0x80000000 | step on error
0x0C  u32  sound ID: the sound to play
0x10  u32  the sound's handle (written by the game): the sound's object while it plays, then 0
0x14  ...  game profile (see the driver's source). Four Swords Adventures' profile holds the retraces between
           audio frames, the sizes that the game's start-up uses, then the addresses of the game's functions and the
           offsets of their objects
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

A driver's entry is at 0x81700000. The word at **0x81700004** holds the address of the driver's port list, or 0 for a
driver without one. The list holds the number of ports that the player sets, then 16 entries. An entry gives a port's
number in its high 16 bits and the port's value in its low 16 bits. A player fills the list from the `gcsf_var` tag
(see "Tags"). The driver gives the sound those ports. The Wind Waker's driver has a port list, after its game profile.
Port 6 holds the low 16 bits of the map information that the game passes with an effect. The driver passes port 6's
value to `startSoundVec` as that information. The driver gives the sound each other port after the start, through
`JAISound::setPortData`. The sound library writes those ports to the effect's track when the effect's sequence starts.
The game's `seStart` gives every effect port 9 that way: the reverb of the room that the effect plays in. Pikmin's
driver has a port list after its game profile too.

Pikmin 2's driver has a wave list too. Its entries have the same layout. The game has one wave system, with 49 groups.
Group 0 holds the effects. The sound system loads it at start-up, and the driver skips it. A scene loads at most two
other groups.

Donkey Kong Jungle Beat's driver has a wave list too. The last word of its game profile, at 0x1E8 of the block, is
written by the driver: the JAISound of the music that a chord effect plays over, or 0. The ripper's length analysis
leaves out the commands of that sound's tracks.

Mario Golf's driver doesn't use the handle. The top byte of a Mario Golf sound ID gives the table, and its low 16 bits
give the song's number in that table. Table 0 holds the songs, and table 1 the jingles. Mario Power Tennis has only
table 0. It holds the jingles too. The driver plays them as songs. A top byte from 0x80 to 0x83 selects a sound effect.
With 0x80, the low 16 bits give the effect's ID. Bits 16 to 23 give its voice bank's number plus 1, or 0 for an effect
of the bank that the game loads at start-up. With 0x81, the low 16 bits give a narration's number in the game's table of
narrations. With 0x82, the low 16 bits give the ID of an effect of a court's bank, and bits 16 to 23 the court's number.
With 0x83, bits 0 to 11 give the ID of an effect of a group that no table of the game lists. Bits 12 to 23 give half the
number of the group's first file.

WarioWare's driver doesn't use the handle either. A WarioWare sound ID is MusyX's song ID: the song's MusyX song
group in the top byte and its number in the low 16 bits. The driver passes that number to the game's play function. An
effect that isn't a song has bit 31 set and the play function's number in its low 16 bits: 1606 plus MusyX's ID of the
effect.

Mario Party 4's driver doesn't use the handle either. A Mario Party 4 sound ID gives the group set that the driver loads
in its top 16 bits and the song's number in the sound manager's table in its low 16 bits. A group set of 0xFFFF loads
none. An effect's ID has bit 15 set in its low 16 bits. The other 15 bits give the effect's number in the sound
manager's table of effects. The top 16 bits then give the group to load alone, with bit 15 set, or 0xFFFF for none.

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
and its place in the bank in the low byte. Doubutsu no Mori+'s sound IDs follow the same rules.

In Doubutsu no Mori e+, the game's numbers of pieces take 16 bits. A piece's ID there gives the number in the low 16
bits, the weather in bits 16 to 19 and the museum's room in bits 20 to 22. Bit 23 is set when the islander is on the
island. Only the island's piece, 376, depends on that bit. The IDs of the release's effects follow the same rules as in
Animal Crossing.

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
  bit 31 set. The low byte is the step that failed. Step 1 is the driver's check of its block. The driver's source
  lists the other steps. The player also stops with an error if the status hasn't reached 1 after 10 emulated seconds.
- **End.** Like any PSF player, a player plays for the tagged length plus fade. Without a length tag, it plays until
  half a second after the driver reports that the sound has finished (status 2). It stops sooner if a default length and
  fade end first. The player checks the status after every millisecond of emulated time.

### MusyX catalogue

A rip for `driver/musyx.c` or `driver/musyx_upload.c` holds the game's MusyX files in the library, as `FILE` chunks in
`MusyX/`. A set of four files holds one or more groups. The files take the set's number as their name: `MusyX/0.proj`
(the project), `MusyX/0.pool`, `MusyX/0.sdir` (the sample directory) and `MusyX/0.samp` (the samples). A song's file is
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

The library has a file for every set and every song. Besides the catalogue, the driver reads only the sets of a sound's
groups and the file of its song. A set or a song that none of the rip's sounds uses holds only zeros. Its files keep
their sizes. The disc's table of files and the places of the other files on the disc then stay the same. The rip's
sounds are the ones that it has a `.minigcsf` for. A rip made with `--bgm` or `--only` therefore holds the samples of
fewer sets.

## Tags

The standard PSF tags apply: `title`, `artist`, `game`, `year`, `genre`, `comment`, `copyright`, `length`, `fade`,
`volume`, `_lib`, `_libN`. `gcsfby` credits the ripper. The ripper also writes `gcsf_sound` with the sound's name from
the game's sound table, and `gcsf_mode` with the value `game`.

`length` and `fade` count time at the reported rate of 32,028 Hz: a player plays `length` × 32,028 stereo samples
before the fade.

`gcsf_var` sets ports of the sound before the sound starts. Some effects read a value that the game's code puts on a
port when it starts them, such as the material of the ground under a footstep. They play nothing, or another variant,
without it. The value is one or more assignments `N=V`, in the form of 3SF's `3sf_var`, separated by commas or white
space. Line breaks count as white space. N is a port, 0 to 15. V is −32,768 to 32,767, and the port takes its low 16
bits. Each number is decimal, or hex with a `0x` prefix, and may have a minus sign. A later assignment to the same port
wins. A player ignores the whole tag if any part of it is malformed or out of range. The player writes the ports into
the driver's port list (see "Driver") after it loads the set's MEM chunks and before the boot. A set whose driver has
no port list ignores the tag. `gcsfrip` writes the tag for each variant of such an effect, with the assignments in
increasing order of N, in decimal, separated by ", ": `gcsf_var=6=4`. The foobar2000 component keeps the tag when it
rewrites a file's tags.

## Ripping

`gcsfrip` builds sets from a GameCube disc image or an extracted disc. A game requires a driver profile. Profiles
(function addresses) are specific to each game and version. They're matched by game ID and the SHA-1 of `main.dol`.
Version 1 supports The Legend of Zelda: Four Swords Adventures (USA) and its Japanese release, The Legend of Zelda: Four
Swords Plus, The Legend of Zelda: Twilight Princess (USA), The Legend of Zelda: The Wind Waker (USA), Luigi's Mansion
(USA), Super Mario Sunshine (USA), Pikmin (USA), Donkey Kong Jungle Beat (USA) and Mario Kart: Double Dash!! (USA). It
also supports Mario Golf: Toadstool Tour (USA), Mario Power Tennis (USA), WarioWare, Inc.: Mega Party Game$! (USA),
Pikmin 2 (USA), Pac-Man Vs. (USA), the menu of The Legend of Zelda: Collector's Edition (USA), Final Fantasy Crystal
Chronicles (USA), Animal Crossing (USA), Doubutsu no Mori+ (Japan, disc version 1), Doubutsu no Mori e+ (Japan, disc
version 1), the Pokémon Colosseum Bonus Disc (USA), Metroid Prime (USA, disc version 2), Metroid Prime 2: Echoes (USA),
Paper Mario: The Thousand-Year Door (USA), Tales of Symphonia (USA), Star Fox Adventures (USA, disc version 1), Star
Wars Rogue Squadron II: Rogue Leader (USA), Star Wars Rogue Squadron III: Rebel Strike (USA), Pokémon Colosseum (USA),
Resident Evil Zero (USA), Pokémon Channel (USA), Mario Party 4 (USA, disc version 1), Nintendo Puzzle Collection
(Japan) and the NDDEMO tech demo. The Bonus Disc's profile matches the program in a TGC on the disc (see below).

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
  Tennis, the disc's header, the files of the game's two MusyX sound groups, every song's file (an SNG) and the files of
  the other groups that hold effects, four for each group, as `FILE` chunks. For Final Fantasy Crystal Chronicles, the
  disc's header, every
  piece's file (`dvd/sound/Music/*.bgm`), the
  four blocks of effects that the game loads (blocks 0 to 3 in `dvd/sound/SE/Block/`), the effects' SEP files from
  number 4000 on (`dvd/sound/SE/Sep/*.sep`) and the wave banks that the pieces and the effects name
  (`dvd/sound/Wave/*.wd`), as `FILE` chunks. For Animal Crossing, Doubutsu no Mori+ and Doubutsu no Mori e+, the disc's
  header and the game's sound data (`audiorom.img`), as `FILE` chunks. For the Pokémon Colosseum Bonus Disc, the disc
  header of the TGC's program and the effects' scripts and samples (`pm6fgm.sem` and `pm6fgm.ssm`), as `FILE` chunks.
  For a game for `driver/musyx.c` or `driver/musyx_upload.c`, such as Metroid Prime, the disc's header, the catalogue of
  the game's sounds, and the MusyX files of their groups and songs (see "MusyX catalogue");
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
returns at once, and the game plays nothing for them either. 30 others read a port that the game sets when it starts
the effect. They play nothing while the port holds 0. A rip holds their variants instead. Each variant plays the
effect with one value of the port:

- **Port 6** holds the low 16 bits of the map information that the game passes with an effect. For a step, a jump, a
  landing or a climb, the game passes the material of the ground in its collision data: 1 to 27. For a hit, it passes
  the material of the surface, or a code from 32 up for the kind of thing hit. These effects read port 6: se2-0003 to
  0005, se2-0051 to 0052, se2-0085, se2-0111, se2-0122 to 0123, se3-0000 to 0003, se3-0010 to 0012, se3-0021,
  se3-0024, se3-0038 to 0041, se3-0043 to 0048 and se5-0301.
- **Port 9** holds the reverb of the room that the effect plays in. The game's `seStart` gives every effect that port.
  Rips play effects without it. se3-0050, a Stalfos's step, plays nothing while port 9 holds 0. A rip holds a variant
  for each reverb of the rooms that place a Stalfos: 40, 80, 100, 110, 120 and 127.

A variant's `gcsf_var` tag holds its port's value, and its title is the effect's name, a space and the tag's value, such
as `se3-0000 6=4`. Its file shares the effect's number. A user can play another value by changing the tag. The profile
lists each effect's values (`GameProfile::port_effects`). Values that play the same samples share one variant. The list
holds the lowest of them. Four effects that read port 9 make no sound with any value: se3-0049, se4-0254, se4-0255 and
se4-0327. The ripper skips them.

Donkey Kong Jungle Beat has effects like these too. 133 are empty. 68 read port 0xb, and some of them also ports 0xc to
0xf. Most take the key of a note from those ports. They play nothing while port 0xb holds 0. The game's chord code
writes the ports of the effects that the chord database names. A rip plays those effects through that code (see
"Chord effects"). Port 0xb holds 0 for the others. Two more make no sound in a rip. `gcsfrip` names each effect that it
skips.

Luigi's Mansion plays one piece, `LuigiSings`, by the game's state. Four of its tracks hold the tune. One of them
plays at a time. Luigi's health picks one of three: above 70 of his 100 points, above 40, or 40 and below. The fourth
plays alone while the room's lights are on, unless the room is outdoors. Other tracks accompany the tune by the room:
one set in the hallways, one outdoors and one in the other rooms. Rooms 19, 32 and 65 of the game's table of rooms have
no accompaniment. Luigi's health also sets the tempo: 0.6 + 0.4 × the health / 100. With the lights on, the time
between Luigi's steps scales it, to 0.9 while he stands still. A rip holds the piece once for each state, in place of
the piece: the hallways, the other rooms, outdoors and the rooms without accompaniment, each at 100, 70 and 40 points of
health, and a lit room at 100 points while Luigi stands still. A state's title is the piece's name followed by the
place and Luigi's health. The lit room's title gives the place alone. Each state's file shares the piece's number.
The profile lists the states (`GameProfile::singing_states`).

Mario Kart: Double Dash!! keeps its music's sequences in the start-up archive. It has no sequence archive. Its voices
play the banks of three more start-up archives in the first one's `baac` block. The game loads one of those at a time.
The ripper walks each effect's sequence for the banks that it selects. A voice's ID gives the archive that holds its
bank, plus 1, in bits 28 to 31. The library holds the wave archives of the archives that its sounds load. The ripper
skips the effects whose volume in the sound table is 0, and the empty ones.

Animal Crossing keeps no names for its music. Neither do Doubutsu no Mori+ and Doubutsu no Mori e+. Each release's code
holds a table of the game's numbers for its pieces: 256 in Animal Crossing and Doubutsu no Mori+, and 384 in Doubutsu no
Mori e+. Each number gives the sequence that the game plays for it. A rip names a piece after its number, for example
`bgm046`. The 24 pieces that play the hours are ripped once for each weather that changes them, for example
`bgm001-fine`, `bgm001-snow` and `bgm001-sakura`. The museum's piece is ripped once for each room, for example
`bgm088-lobby`. The island's piece in Doubutsu no Mori e+ has two rips: `bgm376-islander` with the islander on the
island, and `bgm376-no-islander` without. Where several numbers play the same sequence, the ripper rips the first of
them. Number 0 isn't ripped: it stops the music. In Animal Crossing and Doubutsu no Mori e+, numbers 83 to 87 play
effects. The rips of those two releases leave the numbers out. Doubutsu no Mori+ plays them as pieces of music. Doubutsu
no Mori+'s rips include them. Numbers 92 to 111 are the first parts of Kapp'n's songs, and 112 to 121 are their second
parts. A rip of a first part ends with the first part, and each second part has a rip of its own (see "Driver").
Sequences that the table doesn't give aren't ripped as music. They play the effects, the voices and the sounds of some
furniture.

The game keeps no names for its effects either. They play from six banks in the sequence of effects. A rip names an
effect after its bank and its place in the bank, for example `se2-017`. Effect 0 of bank 0 isn't ripped: the game's
slots use the number 0 for no effect. For 17 effects, the game adds a random number from 0 to 3 to the effect's number.
For five or six others, by release, the game adds 0 or 1. The game then plays the effect of the sum. A rip of such an
effect plays the effect that its number names (see "Driver"). The effects that the sum can name have rips of their own.
The game's continuous sounds, such as rivers and insects, play in other channels of the sequence. The ripper rips
neither them nor the villagers' voices.

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
"Driver"). `--bgm` takes every song and jingle. A song's name is its file's name without the extension. Both games' code
lists their sound effects too. Mario Golf's lists 737: the ranges of IDs of the bank that the game loads at start-up, 37
voice banks and 51 narrations. Mario Power Tennis's lists 924: the range of its start-up bank, 18 voice banks, 106
narrations in one group and the banks of 10 courts. 37 more groups of Mario Power Tennis follow each other on the disc.
They hold the game's other 56 effects. A group's four files are its pool, its project, its sample directory and its
samples. The ripper lists the effects after the songs and jingles. An effect's name is its voice bank's name, `se` for
the start-up bank and the other groups, or `court` for a court's bank, then a hyphen and its ID in four digits, for
example `mario-0154`. A narration's name is `narration-` and its effect's ID. A song reads none of the files of the
effects' groups. A rip without an effect, such as a rip of the music alone, holds zeros in place of those files. Each
file keeps its size. The disc's table of files and the places of the other files on the disc then stay the same.

Pikmin's code holds the index of its sequences: a BARC of 22 entries that gives each sequence's name and its offset and
size in `pikiseq.arc`. The first entry is the effects sequence, and the second plays the system's events. Five entries
are empty. The other 15 are pieces of music. A `.minigcsf` selects a sound by its ID: 0x80000000 | group << 24 | wave
system << 16 | part << 8 | piece. The piece is the sequence's number in the index. The driver loads the group of the
wave system that the ID gives. A piece's ID gives part 0 and group 0 of the wave system with the piece's number. The
piece's name is its file's name without the extension, for example `select`. `--bgm` takes every piece. One piece plays
an empty bank. That piece makes no sound, and the ripper skips it. The music of the game's cutscenes is one piece in
parts, `demobgm`. The ripper lists the parts that the game plays in place of that piece. A part's ID gives the part and
the wave group that the game loads for it. A part's name is the piece's name, a hyphen and the part's number, for
example `demobgm-5`. The game plays 13 more pieces, most of them in cutscenes, from streams (`.stx` files).

Pikmin's code also holds a catalogue of its sound effects, with their names. A row gives how the game starts the
effect: as an effect of the system, as an effect of the player, or as an action of an event of a given type. An
effect's ID is 0x80000000 | kind << 24 | number << 8 | sequence. The kind is 1 for an effect of the system, 2 for one
of the player, 3 for an event's action and 4 for the sound of the formation. The number is the effect's number, or the
event's type << 12 | the action. The sequence is 0, the effects sequence, or 1, the events' sequence. An effect takes
its name from the catalogue, for example `SE_FLOG_JUMP`. Where the actions of several events play the same effect, the
ripper lists it once, with the first row's name. The game's code plays a few effects that the catalogue doesn't list:
effects of the system in the demo of a found ship part, a voice of the player and the sound of the formation. Their
names are their IDs in hex. A rip of the formation holds one variant, `84000000 3=127`: a full move straight ahead (x 0
and y 127, see "Driver"). The ripper leaves out four effects of
the system. One stops the effects, one plays nothing, one plays a part of `demobgm`, and one plays a stream. An event of
the ship's type starts the ship's idling as the game creates it. A rip plays the ship's actions on an event of another
type. That event reaches the same entries in the table of actions.

A Pikmin rip's library holds zeros in place of the wave archives that none of its sounds loads. At start-up, the sound
library loads group 0 of wave system 0, the effects' first group. The driver loads the group that a piece's ID gives.
For an effect, the driver loads groups 1 to 3 of wave system 0 and group 0 of wave systems 14, 15 and 2. The game's
set-up of its first scene of play loads the same groups. The groups that none of the rip's sounds loads keep their
sizes, but hold zeros. The disc's table of files and the places of the other files on the disc then stay the same.

WarioWare, Inc.: Mega Party Game$!'s code names its music in the table of its Sound Room: 231 pieces. A `.minigcsf`
selects a piece by its MusyX song ID (see "Driver"). Each piece keeps the name that the Sound Room gives it. `--bgm`
takes every piece. Two of the pieces only count beats for the game. They have no notes, and the ripper skips them. The
game plays most of its sound effects as MusyX songs too. Its play function (0x800348B8) plays a song for a number below
1606. A table of 933 songs that play as effects (0x801B4918) holds 8 bytes for each: MusyX's song group in the first
byte, the song's number in a big-endian half at +2 and its volume at +4. A number from 1606 on starts one of MusyX's
effects: the effect whose ID is the number less 1606. The table at 0x801B6640 holds 6 bytes for each of the 145 effects
that the function can start: the function's number in a big-endian half, then the volume at +2, the send to the reverb
at +4 and the send to the chorus at +5. The function starts the effect through sndFXStartEx at that volume, with the
pan of the effect's row in its group and studio 0. It then sets MIDI controllers 91 and 93 to the sends. The ripper
lists the songs of the first table that the Sound Room leaves out, then the effects of the second, in the tables'
order. Each one is named after the play function's number, for example `se-0013`. For WarioWare, the `.gcsflib` holds
the disc's header and the game's five sound files as `FILE` chunks: MusyX's project, pool, sample directory and
samples, and one file with every song.

Mario Party 4's sound test lists 76 songs and 14 streams by name. The ripper lists them in the sound test's order. A
`.minigcsf` selects a song by its group set and its number (see "Driver"). Each song keeps the name that the sound test
gives it. `--bgm` takes every song. The streams aren't ripped. The sound manager's table holds two more songs. Song 0 is
a dummy. Song 42 is song 41 in another group. The ripper leaves both out. For Mario Party 4, the `.gcsflib` holds the
disc's header and two sound files as `FILE` chunks: the sound manager's file of sound data, and the stream file up to
the streams' samples. The sound manager reads that part at start-up.

The game's sound effects and voices follow the songs. The sound manager's file holds a table of 2,308 effects at the
offset that its header gives at 0x30, 16 bytes for each. An entry gives the MusyX ID of the effect's group and MusyX's
ID of the effect in big-endian halves. The volume, the pan, the pitch bend in a big-endian half, the surround pan, the
sends to the reverb and the chorus, a flag for an emitter and the emitter's compression follow. An entry whose effect ID
is 0xFFFF is empty. The ripper leaves it out. The table of groups lies at the offset that the header gives at 0x20, 32
bytes for each group. A group's entry starts with its MusyX ID. An effect's group is the first group from 1 on with the
effect's group ID. An effect is named after its number in the table, for example `se-0042`. The Option Room's list of
character voices names some of them. The list follows the sound test's entries in `dll/option.rel`: 123 entries of the
same form, at 0xB518. The ripper adds such a name, for example `se-0054 Toad Happy`. The game plays an effect through
msmSePlay. It loads an effect's group with a scene's group set, or alone through its function at 0x800336D4. A rip loads
the effect's group alone, unless it's one of the base groups. The rip then starts the effect without parameters: with no
position, at the volume, the pan and the sends of its entry.

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

A rip's library holds zeros in place of the files that none of its sounds reads. A piece reads its file and its bank.
An effect reads the four blocks and banks 0 and 500. An effect of a SEP file also reads that file and its bank. A rip
of the music alone therefore holds zeros in place of the blocks, the SEP files and the banks that only the effects
name. Each file keeps its size. The disc's table of files and the places of the other files on the disc then stay the
same.

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
MusyX's four files. They hold the game's 12 groups. Six are effects groups, with 1,376 effects. The other six are song
groups. The game plays their 143 songs as sounds. The game pushes all 12 groups at start-up and keeps them. A rip pushes
only the sound's group. The game's code lists its sounds in a table at 0x80316B28: 4,242 rows of 20 bytes. A row starts
with the address of the sound's name. The next word gives the sound's kind in its top bits and its ID in its low 16
bits: 0x20000000 for an effect of MusyX, 0x40000000 for a song and 0x80000000 for a stream. A row of zeros is unused.
The bytes at 0xC, 0xD and 0xF give an effect's volume, its pan and its send to the reverb. The big-endian half at 0x12
bends its pitch. The game starts an effect through sndFXStartEx with a volume of 0x7F and the row's pan. It then sets
MIDI controller 7 to the row's volume, the pitch wheel to 0x2000 plus the bend, and controller 91 to the send.
Controller 91 feeds aux A, MusyX's high-quality reverb of 3 seconds. Aux B holds MusyX's chorus. The game adds its
room's reverb to a send above 0. A rip plays no room. Most of the game's sounds play at a place in the world. Their
volume and pan then follow the listener. A rip plays each effect as its row gives it. Several rows can play one effect.
The ripper takes the first row with a volume above 0, or the first row if none has one. A rip names the effect as that
row does, for example `SFX_PRESS_START1`. The table plays 1,304 of the effects. The other 72 are named after the set and
their ID, for example `pmario-0051`. They play at MusyX's defaults. Every row of two effects gives a volume of 0. They
make no sound. The ripper skips them.

The game reads its songs from `sound/proj/pmario.slib` at start-up. The file holds them one after another. A song's
record starts with its size in a big-endian word, its 32 bytes of header included. The song follows the header. A size
of 0xFFFFFFFF ends the file. A song's row gives the song's group in bits 16 to 23 of its second word, MusyX's song ID in
bits 8 to 15, and the song's record in bits 0 to 7, counted from 0. The game plays the song through sndSeqPlayEx with
the group, the ID and the record's song. It then sets the song's volume to the row's volume through sndSeqVolume. The
row's pan, pitch bend and send play no part. 168 rows play 139 of the songs. For a song too, the ripper takes the first
row with a volume above 0. A rip names the song as that row does, for example `SFX_SE2_EVT_STARSTONE_GET1`.
`pmario.stbl` names the songs: 32 bytes for each record, with the song's name, ended by a byte 0, then the song's group
at 0x1E and its ID at 0x1F. The other four songs take their names, groups and IDs from there, for example `SE1_TEST`.
They play at MusyX's default volume. The ripper counts the songs as music. `--bgm` rips them alone. A rip of the game
holds 1,374 effects and 143 songs.

Tales of Symphonia's code lists its music in a table at 0x801F9830: 8 bytes for each song. An entry gives the address
of the song's file name, then MusyX's song ID and the song's kind, a big-endian half each. A song ID of 0xFFFF ends the
table. The songs' files are in `S/`, for example `S/bgm_t000.song`. The code names them in lower case, and the game's
disc library matches names in any case. A song's file is a MusyX song (an SNG) without a header. The music's group,
group 0, is in `S/inst.snd`. That file starts with the number of its parts, 4, then each part's offset and size, a
big-endian word each. The parts are MusyX's project, pool, sample directory and samples. The ripper lists the table's
111 songs in its order. A song is named after its file without the extension, for example `bgm_t000`. `--bgm` takes
every song. The game plays a song at the music's volume of its options, 127 at their default. The play function sets
the reverb of aux A for each song (see the profile in `src/rip/profiles.cpp`). A song's catalogue entry holds those
settings. The disc holds six songs that the table leaves out: `bgm_etc004` and `temp1` to `temp5`. The game's code
names none of them, and the ripper leaves them out. Both discs hold the same code and the same music. A rip of either
disc gives the same set.

The game's sound effects and voices follow the songs. Each of the other `.snd` files in `S/` holds one group of effects:
`se.snd` the game's common effects, `se_ev00.snd` to `se_ev07.snd` the effects of events, and `v_01_llo.snd` to
`v_70_bh18.snd` the voices in battle. The effects' IDs rise from file to file in the order of the files' names. The
ripper lists the effects in that order. The game's code names the first nine of those files. The code also names nine
voice files of the party in `BTL/`. They hold the same bytes as the first nine voice files in `S/`. Another file in
`BTL/` holds the samples of the other voice files, in the order of the files. The ripper reads every group from `S/`.
One voice file holds no pool and only 256 bytes of samples. MusyX can't push that file's group. A rip leaves out the
file's effects. The game's start-up (0x800708C8) pushes the music's group, then the common effects' group. The game
keeps both. The common effects take some samples and tables from the music's group. The effects of events take samples,
macros and tables from the common effects' group. A rip of an effect therefore pushes those two groups first, then the
effect's group. The game starts an effect through sndFXStartEx with a volume of 127, a pan of 64 and studio 0. Every row
of the groups' tables of effects gives the same volume and pan. The game can later set an effect's pan from the place of
the sound's source. A rip plays every effect in the centre. Nothing names the effects. A rip names an effect after its
file and its ID in four decimal digits, for example `se-0012` or `v_01_llo-0501`.

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
ripper leaves it out.

The game's sound effects follow the songs, in the order of their IDs: every effect of the effects groups that the game
pushes. All of them are in `starfoxs`. A rip of an effect pushes the same groups as a rip of a song. The game starts
its effects through the rows of `audio/data/Sfx.bin`, 32 bytes each. A row gives the effect's volume in its byte at 2.
Six effect IDs follow from 0xA, in big-endian halves. The top four bits of the row's last byte give how many of them
the row uses. The game picks one of those at random. An ID of 0 picks no effect. The game passes the row's volume, a
pan of 0x40 and studio 0 to sndFXStartEx. An effect plays at the volume that most of its rows with a volume above 0
give it. Of volumes that tie, the first one in the file wins. An effect whose rows all give 0 plays at 0 and makes no
sound. The ripper skips it. An effect that no row names takes the volume and the pan of its row in its group's table of
effects. In some places of the game's world, the game sends its effects to aux A's reverb through controller 0x5B. A
rip plays the effects without that send. Everywhere else, the game does the same. The game's code names none of its
effects. The disc's `audio/starfox.h.bak` names the effects of an earlier build. The IDs in that header don't match the
game's project. A rip names an effect after its set and its ID in four decimal digits, for example `starfoxs-0420`.

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
rip plays them at MusyX's full volume.

Banks 0 to 19 hold the game's sound effects. The ripper lists them after the songs, bank by bank. Each effect's ID
belongs to one bank. Some effects of banks 1 to 19 play samples of bank 0. Bank 0 shares no ID of a macro, a table or
a sample with those banks. A rip of an effect pushes the groups of bank 0, then those of the effect's bank, each in the
bank's order. The game's table of sounds, at 0x7FDDBF30 in the game, holds 522 rows of 12 bytes. A row gives an
effect's ID in a big-endian half and the effect's volume in the row's byte at 3. An ID of 0xFFFF marks an empty row.
Several rows can give one effect, at different volumes. To play a row, the game calls sndFXStartEx with the row's
volume, a pan from the caller and studio 0. Three functions scale the volumes of rows 489 to 491 by a setting. Another
starts a row's effect with values for two controllers. A rip leaves both out. A rip plays an effect at the volume that
most of its rows give it, in the centre. Of volumes that tie, the first row wins. An effect that no row names takes
the volume and the pan of its row in its group's table of effects. A rip names an effect after its ID in four decimal
digits, for example `RogueLeader-0048`.

`main.dol` is a loader. It holds the OS, MusyX and a second DOL: the game. The loader maps the game in at 0x7FC00000.
The driver runs only the loader's code. A rip skips the loader's set-up of the game's heaps and the game's static
constructors. The profile in `src/rip/profiles.cpp` lists the rip's other patches.

Sixty of Rogue Leader's songs play only streams. Each of them holds a single note. The note's program plays stream
buffers: samples of one second that loop. A stream buffer has the sample type 5 in its bank's sample directory. The game
probably fills the buffers from the disc as the song plays. The numbers of the disc's `Rogue2/dg000NN.vsp` files match
those of the banks that hold the songs with streams, 21 to 28 and 30 to 37, and that of the music bank, 20. GCSF doesn't
rip streams. These songs make no sound in a rip, and the ripper skips them. Eight more songs play a stream along with
their notes: The Battle Of Yavin I and III, Hyperspace, Imperial Attack, The Asteroid Field, Celebration & End Titles,
and two songs that the hall doesn't list (group 0x4D's song 0x1A and group 0x61's song 0x3D). A rip of one of them would
play only its notes. The ripper leaves them out, as streams. The profile in `src/rip/profiles.cpp` lists them.

Pokémon Colosseum keeps most of its files in FSYS archives (see `src/rip/fsys.h`). The module `common_rel` in
`common.fsys` holds the game's table of sounds: 1,236 entries of 12 bytes. An entry's first byte has bit 7 set for an
effect. A song's entry gives its group at +1, its reverb's preset at +2, the number of its file at +3 and MusyX's song
ID in a big-endian half at +4. The module's table of 79 files gives each file's FSYS ID. `bgm_archive.fsys` holds the
songs under those IDs and names them. The music's groups are in `common.fsys`: group 0 in `snd_music_proj`,
`snd_music_pool` and `snd_music_sdir`, and group 1 in the same files of `snd_music_atmos`. Their samples are
`sound/snd_music.samp` and `sound/snd_music_atmos.samp`. The profile in `src/rip/profiles.cpp` gives the tables' places
in the module. The ripper lists the table's 78 songs in its order. A song is named after its file, for example
`title_song`. `--bgm` takes every song. `null_bgm_song` holds no notes. The game plays it to silence its music. The
ripper skips it. The game plays its songs at volume 127. Studio 0's aux A holds MusyX's high-quality reverb. The
module holds 12 presets of the reverb's settings. A song's entry picks one. A song's catalogue entry holds the
preset's settings. Preset 0 leaves the reverb as it is. Only `null_bgm_song` picks it.

Pokémon Colosseum's effects follow the songs, in the table's order. An effect's entry gives its group at +1 and MusyX's
ID of the effect in the big-endian half at +4. The effects' groups are in `common.fsys` too, one for each of the sets
`snd_se`, `snd_se_battle`, `snd_se_motion`, `snd_se_nakigoe` and `snd_se_nakigoe2`. Their samples are in `sound/`. An
effect is named after its set and its ID, for example `snd_se_battle-0202`. An effect whose row in its group's project
allows no voices is left out. MusyX never starts it. The game's play function (0x80166AB8) starts an effect through
sndFXStartEx at volume 0x7F, pan 0x40 and studio 0. It then sets the effect's MIDI volume to 0x7F. MusyX starts an
effect at that MIDI volume. A rip starts the effect in the same way and leaves out the controller. The game's set-up
calls sndVolume(100, 0, SND_FX_VOLGROUPS). MusyX puts every effect in volume group 31. That call sets the group's volume
to 100/127. An effect's catalogue entry writes the group's volume into MusyX's fader. A rip of an effect uses preset 0's
reverb.

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
`bio0/demo/`. GCSF doesn't rip streams.

The game's sound effects follow the songs. Each of the other archives in `bio0/sound/` holds one group of effects. Its
samples are in the file of the same name with the extension `.sam`. `all.arc` holds the effects that the whole game
plays. The others hold the effects of rooms, doors, enemies, weapons and the extras, and the voices. The first disc
holds 137 such archives and the second 192. 100 of them are on both discs, with the same files. The ripper lists the
effects archive by archive, in the order of the archives' names. The game's start-up pushes the group of `all.arc`, then
the groups of ten archives of weapons, and keeps them. A table in the game's code at 0x80255D68 lists the ten. A row of
8 bytes gives a group's ID in a big-endian word and the address of the archive's name. A negative ID ends the table.
Those groups share no ID of a macro, a table, a sample or an effect with the other archives. The effects of each archive
start only macros and samples of that archive. A rip of an effect pushes the group of `all.arc`, then the effect's
group. The game starts an effect through sndFXStartEx with a volume and a pan from the caller. The game then sets the
effect's send to aux B. Most calls give a volume of 0xFF. MusyX then takes the volume of the effect's row in its group's
table of effects. 98% of the rows give a pan of 64. A rip plays an effect at the volume and the pan of its row, without
the send. 2,002 rows on the first disc and 1,436 on the second give a volume of 0. Their effects make no sound at that
volume. In all but two archives, those rows start the same macro. The tables of most archives hold 50 rows. The rows of
volume 0 look like the tables' unused rows. The ripper leaves them out. A rip names an effect after its archive and its
ID in four decimal digits, for example `sh00-1467`. The archives of the cutscenes in `bio0/demo/` hold the cutscenes'
effects too. The ripper leaves those out.

Star Wars Rogue Squadron III: Rebel Strike packs its files in `Rogue3/data.dat` as Rogue Leader does. `Rogue3/data.dir`
pads each number with spaces on its left. The 172 banks in `audio/data` have Rogue Leader's layout. Many banks hold
copies of the same groups, with the same effects. The banks' projects list 871 effects. Each effect ID belongs to one
group. A rip of an effect pushes the effect's group from the first bank that lists the effect. The effect plays through
sndFXStartEx with the volume and the pan of its group's table. The library holds the 45 banks that the effects take. A
rip names an effect after its ID, for example `RebelStrike-0048`. An index at the start of `audio/data` names the
game's sounds. The ripper doesn't read it. The game streams all of its music. 90 of the banks hold its songs. Each song
plays one note. The note's program plays stream buffers: a pair of short samples for the left and the right channel,
with the sample type 6 or 5 in the bank's sample directory. The game probably fills the buffers from the disc as a song
plays. For each bank that holds songs, the disc holds a file named after the bank's number, such as `Rogue3/dg00005.vsp`
for bank 5. GCSF doesn't rip streams. The ripper leaves the songs out.

Rebel Strike's `main.dol` is a loader. It holds the OS and the disc's library. The game's code and data, MusyX among
them, are a second DOL. `main.dol` holds that DOL packed at 0x80200000: two big-endian words, the packed size and the
size, then the DOL as zlib packs it. The loader's start-up unpacks the DOL into ARAM. The game then runs from 0x7F200000
through the CPU's page table. GCSF's CPU translates addresses only through the BATs. A rip places the game's DOL 16 MB
above its addresses, from 0x80200000, in place of the sections of `main.dol` that it overlaps. The rip replaces the
loader's start-up with code that maps 0x7F000000 to 0x7FFFFFFF there with IBAT3 and DBAT3. MusyX keeps its state in the
loader's memory. The game's memory hooks are two mallocs and a free. The rip patches sndSetHooks to take the driver's
malloc for both. Each level sets a reverb on studio 0's aux A, or none. A rip sets none.

Pokémon Channel plays its music as MusyX effects. `en/pch/pchsound.proj`, `.pool`, `.sdir` and `.samp` are MusyX's four
files. They hold every group of the game, with 1,159 effects. Each piece of music is a layer of two voices. The voices
play one stereo recording, panned hard left and right. The game's music function (0x80015398) plays an effect of its
table at 0x8015EB50: 38 rows of 8 bytes. A row holds two big-endian words: the effect's ID, and the number of a sound
whose group the game pushes. That group is the one that the project gives the effect. The ripper lists the table's
effects first, in its order, as music. `--bgm` takes them. The project's other effects follow, in the order of their
IDs. A rip of a sound pushes the sound's group alone. The game starts every sound through 0x80015570: sndFXStartEx with
a volume of 0x7F and the default pan, then MIDI controller 7 with the sound's volume, and the pan through controllers
0x83 and 10. A rip plays every sound with controller 7 at 127 and the pan in the centre (0x40). The game plays its music
that way. The game's scripts play the TV programmes' themes as effects. A rip lists them with the effects. The script of
the game's sound viewer, `pch/scripts/sndviewer.pkc`, names the sounds. Its names are strings between tabs. They start
at offset 0x197C3. The script's code from offset 0x4552 gives each name's effect (see `ViewerNames` in
`src/rip/musyx_catalogue.cpp`). A rip names a sound as the viewer does, for example `SND_SUOU`. A sound that the viewer
doesn't name is named after the set and its ID, for example `pchsound-1064`. The game's sample file is larger than main
memory. The game's loader hands MusyX the samples through MusyX's upload callback. `driver/musyx_upload.c` does the
same.

Nintendo Puzzle Collection (Japan) holds a menu and three games: Dr. Mario, Panel de Pon and Yoshi's Cookie. `main.dol`
holds the OS, the disc's library and MusyX. `main.dol` loads the menu and each game as a module of code (a REL) from a
file that Yay0 packs: `menu_relsamp.szp`, `Dr_MARIO.szp`, `panepon.szp` and `ycookie.szp`. The game's code names some
files in another case than the disc does. The disc's library matches names in any case. A Yay0 file starts with
"Yay0", the unpacked size, and the offsets of a table of copies and a table of bytes, in big-endian words. Masks of 32
bits follow. Each bit of a mask, from the top, takes the next byte or copies earlier output. A copy's big-endian half
gives the distance back, less 1, in its low 12 bits and the length, less 2, in its top 4. A length of 0 there takes the
length, less 18, from the table of bytes. The ripper finds the songs and MusyX's files through a module's relocations
against the module. An object runs up to the next place in its section that a relocation points at.

The menu's file unpacks to four parts: the menu's module, then the samples of groups 2, 0 and 4. The file's first four
words give the parts' offsets. The module holds the menu's songs, and the project, the pool and the sample directory of
groups 0 to 5. `title/yoshi_title.samp`, `title/agb_title.samp` and `title/grand_staff.samp` hold the samples of groups
1, 3 and 5. The module's section 4 holds a table of the groups' tables of songs at 0x448. A rip of a menu song pushes
group 0, the menu's effects, then the song's group. The menu's code gives each song a volume from another table in
section 4, by the song's group and ID. Dr. Mario's and Panel de Pon's modules each hold their songs and one set of
MusyX's files. `dr_test.samp` and `panepon.samp` hold the samples. The two games push groups 1 and 0, and play their
songs in group 1. Dr. Mario plays its songs at full volume during play. The game lowers a song's volume during a pause,
in its manual and in some other states. A rip plays each song at full volume. Panel de Pon's code gives each song a
volume in a table. Yoshi's Cookie's module holds a table of the items in `ycookie.bin`: the files of three groups, then
the songs, then the groups' samples. The game pushes groups 0 and 1 at start-up, and group 2 before a song of that
group. Yoshi's Cookie sets MusyX's master volumes for music and for effects from two levels in the game's options.
Tables at 0x179B74 and 0x179B7F in the module's section 5 give the volume of each level, for music and for effects. A
rip takes the options' default levels: a song plays with the master for music at 58, and an effect with the master for
effects at 105.

The ripper lists the menu's 9 songs, then Dr. Mario's 24, Panel de Pon's 59 and Yoshi's Cookie's 26. A song's name
starts with its module's name. The menu's songs take their IDs, for example `menu-01`. Dr. Mario's and Panel de Pon's
take the games' names for them, for example `panepon-flower_01`. Yoshi's Cookie's take the names of their files, for
example `ycookie-sng05`. `--bgm` takes every song. The effects follow the songs: the menu's 9, then Dr. Mario's 106,
Panel de Pon's 527 and Yoshi's Cookie's 65. An effect's name is its module's name, `-se-` and the effect's ID in four
digits, for example `panepon-se-0012`. `dr_mario-se-0105` makes no sound. A rip leaves it out.

The menu's effects are in group 0, Dr. Mario's and Panel de Pon's in group 0 of their sets, and Yoshi's Cookie's in
group 1. A rip of an effect pushes the groups that the module pushes at start-up. The menu and Dr. Mario start an effect
through sndFXStartEx with a volume and a pan of 0xFF and studio 0. MusyX then takes both from the group's table of
effects. Panel de Pon starts most of its effects through a table at 0x165678 in its module's section 5. The table gives
each effect 16 bytes, in the order of the effects' IDs: the volume at +4 and the pan at +5. A volume of 0xFF there gives
110, and a pan of 0xFF the group's pan. A rip does the same. Other calls in Panel de Pon's code start an effect with a
volume and a pan of 0xFF. Yoshi's Cookie's module holds a table of effects at 0x1797B4 in section 5, with a row of 12
bytes for each: the effect's ID in a big-endian word, then the effect's volume, pan, surround pan, motion and reverb
send. A motion of 0 keeps the pan still, and every row holds 0. The game starts an effect through sndFXStartEx with the
row's volume and pan and studio 0. The game then sets controller 0x5B to the reverb send and controller 0x83 to the
surround pan. In stereo, the game gives every effect a surround pan of 64. A rip does the same. The table holds 80 rows.
The project lacks the effects of the last 15.

Each module sets up studio 0's aux effects for its sounds. The menu, Dr. Mario and Panel de Pon give aux A MusyX's
high-quality reverb, with the same settings, and aux B MusyX's chorus. The profile names those effects. A sound's
catalogue entry holds the settings of the reverb and the chorus. Yoshi's Cookie gives aux A MusyX's standard reverb and
aux B no effect. The words of a Yoshi's Cookie sound replace aux A's callback and preparation in the driver's block, and
clear aux B's. Yoshi's Cookie also limits MusyX to 40 voices for music and 24 for effects with sndSetMaxVoices. The
sound's words write those limits into MusyX's state. The menu, Dr. Mario and Panel de Pon call sndVolume(127, 0, 0xFF).
The menu and Dr. Mario then call sndVolume(110, 0, 0xFF). A song's start sets the song's group again. A rip of a song
leaves the second call out. The effects keep 110. A rip of an effect makes the second call too. Yoshi's Cookie calls
sndVolume(127, 0, 0xFD) and sndVolume(127, 0, 0xFE), then sets the master volumes. A rip's sndVolume(127, 0, 0xFF) gives
the same volumes. The rip then sets the master that the sound plays under. The game's options set the output to mono,
stereo or surround. A rip plays in stereo, the options' default.

The NDDEMO tech demo is a GameCube disc that never went on sale. Its disc ID is `00\0E01`: a game code of two zeros
and a NUL, then E, and the maker code 01. `gcsfrip` prints the NUL as `\x00`. The disc's header leaves out the
GameCube's magic word. The ripper reads such a disc when its file system table and `main.dol` are sound. The demo's
code lists its nine songs at 0x80065394. Each entry is the address of a song's path. A song's place in the list is its
song ID. The songs' files are in `audio/` and have the extension `.sng`. The demo's one set of MusyX's files is in
`audio/` too: `nddemo.prj`, `nddemo.pol`, `nddemo.sdr` and `nddemo.smp`. At start-up the demo pushes group 0, the
songs' instruments, and group 1, the effects. A rip of a song pushes both. The demo plays a song through sndSeqPlayEx
with group 0 and studio 0. The demo then sets the song's volume to 127. A rip does the same. The demo calls
sndMasterVolume only to mute its music and to fade it. A rip doesn't call it. At start-up the demo also gives studio
0's aux A MusyX's high-quality reverb of 5 s and aux B MusyX's chorus. A song's catalogue entry holds the reverb's
settings and the chorus's. A rip names a song after its file, for example `coins`. `--bgm` takes every song. All nine
loop.

The demo's four sound effects are in group 1. The group's table of effects in `nddemo.prj` lists them in rows of 10
bytes, the rows of Metroid Prime's projects (see above). A row starts with the effect's ID in a big-endian half. The
demo starts each effect through sndFXStartEx (0x80047DD4) with the effect's ID, a volume and a pan of 0xFF, and studio
0. The group's table then gives the volume and the pan. Four functions of the demo start one effect each: 0x800272A0
effect 0, 0x80027270 effect 1, 0x80027240 effect 2 and 0x80027210 effect 3. A fifth, at 0x800272D0, starts effect 0 and
then sets two of its MIDI controllers through sndFXCtrl14 (0x80047D74): 0x84 and the pitch wheel, 0x80, each to 0x1000.
A rip plays each effect as the first four functions start it. A rip of an effect pushes both groups first. The rip lists
the effects after the songs, in the order of the table. The disc's header `audio/nddemo.h` names them. It holds a line
`#define SFX<name> <ID>` for each effect. A rip names an effect as that line does, for example `SFXcoin_oneshot`.

The demo's sound library is an early MusyX. Its sndInit takes no number of studios. Its sequencer runs five times in
each frame, in steps of 1 ms. A step adds floor(floor(tempo × 3,072 / 60) × 65 / 8) / 65,536 ticks. The build takes a
millisecond as 65 / 65,536 of a second, not 1 / 1,000. The demo's songs therefore play 0.8% slower than their tempo
says. A rip plays them at the demo's speed. The disc's library takes only names in the 8.3 form unless `bi2.bin` allows
long names. The demo's `bi2.bin` doesn't. A rip patches the library to take the driver's long names. The demo has no
DVDReadAsyncPrio. The driver reads the disc through DVDReadPrio instead.

### Chord effects

Donkey Kong Jungle Beat plays many of its instruments' effects (`JA_SE_IST_*`) in the chords of the music. The sequence
of such an effect reads its keys from ports 0xb to 0xf of its tracks. While port 0xb holds 0, the sequence plays
nothing. The game's chord code works out the keys and writes the ports as it starts each step of the effect. A record of
the chord database (`ChordSeDataBase.csd`) starts an effect. The chord archive (`JaiChord.arc`) holds a chord file for
each stage's music. The addresses below are in the USA release's main.dol (`GYBE01`).

- `startChordSe` (0x8024c4c8) starts a record. It takes the audio manager's chord effects (the pointer at +0xd08 of the
  manager), the record, an object, 0, and the volume in f1. The game passes a volume of 1.0. The function refuses a
  record until the music's chords are in. Some states of the game make it refuse records too.
- `startChordSe` swaps two effects by stage. The stage object, at +0xd0c of the audio manager, holds the stage's number
  at +4. At stage 0x2c, a record of `JA_SE_IST_AUTO_COMP_ALTO_SAX` (0x50042) or `JA_SE_IST_AUTO_COMP_TRUMPET` (0x50045)
  plays `JA_SE_IST_AUTO_COMP_E_PIANO_T` (0x50063) in its place. At stages 0x35 and 0x41, such a record plays
  `JA_SE_IST_AUTO_COMP_E_PIANO` (0x50062).
- 0x8024c28c takes one of the chord system's 64 players, at +0xc of the chord effects. A player's byte at +0x18 is set
  from its start until the game stops it. Its byte at +0x19 is set while a step of its effect waits for a beat. Its
  sound object at +0x34 holds the steps' JAISoundHandles at +0 and their number at +4.
- 0x8025049c starts the music of the stage object's row (+0xc) of the stage table (0x803b3978, 16 bytes a row) in the
  BGM player. It then lets chord effects start (the stage object's byte at +0x34). The row is normally the stage's
  number. The first row holds `JA_BGM_MAIN_01`.
- The rhythm, at +0x810 of the audio manager, follows the music's beats. Its frame process (0x80248784) runs from an
  object of the game's scenes. The audio manager's frame process doesn't call it.
- 0x80253684 (player, codes, time) sets each of a piece's 16 tracks by a code, as a state of the BGM player does. Code 2
  sets a track's volume to 0 and mutes the track at once (`JASTrack::mute`, 0x800088dc). A muted track runs on. It
  starts no notes: `JASTrack::noteOn` (0x8000805c) returns at once while the track or one of its parents is muted.

The chord database's words are big-endian. "CDSE" is at +4, the number of entries at +8 and the offset of the table of
records at +0xc. The table holds a count, then 16 bytes for each record: the entry, the sound ID and two more words. The
table's count can be larger than the file holds. The file holds 164 records and one of zeros. The count says 218. The
driver reads only the records that the file holds.

**The state of a rip.** The game plays a chord effect only over a stage's music. Its keys depend on the stage, on the
chord of the moment and on earlier keys. A rip plays one state: the state that the game plays first or most, by its
code. The driver starts the music of stage 0, the first row of the stage table. The game plays stage 0 for any scene
that the table leaves out. The driver then turns each of the music's tracks off at once with code 2. The tracks still
run and give the effect its chords and beats. Their notes take no channels and look up no waves. The driver starts each
effect through `startChordSe` with the lowest-numbered record that names it. The key memory and the random state are
those of the game after its start-up. `JA_SE_IST_AUTO_COMP_E_PIANO_T` plays at stage 0x2c and
`JA_SE_IST_AUTO_COMP_E_PIANO` at stage 0x35. Both play the first record of `JA_SE_IST_AUTO_COMP_ALTO_SAX`. A step that
waits for a beat waits for the music's first beat of its kind. The records name 72 effects. With the two that some
stages play in their place, a rip covers 74. In this state, 12 of them play no notes. The ripper skips them.

**The end of an effect.** A player stays busy after its last step until the game stops it. Later in a combo, the game
moves some effects on by more steps. A rip plays the steps that start by themselves: the first step and the steps that
wait for a beat. The effect ends when no step sounds and no step waits for a beat. Some effects repeat their steps until
the game stops them. Their rips play to the length analysis's limit. The driver writes the music's JAISound to its block
(see "Driver"). The length analysis leaves out the commands of that sound's tracks. A track belongs to the music if the
music's root track is the track or one of its parents. A JASTrack holds its parent at +0x12c.

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
The Wind Waker, Luigi's Mansion, Super Mario Sunshine, Pikmin, Pikmin 2 and Pac-Man Vs., the effects sequence plays all
along. Each effect plays in one of its tracks. Pikmin's events' sequence plays all along too. There the ripper takes
only the sound's root track and the tracks below it: a piece of music's tracks, or the effect's track and the tracks
that the effect opens.

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
  limit. Pikmin's events call each action as a subroutine. An action that loops plays until the limit too, unless it
  holds. In two of Mario Kart: Double Dash!!'s pieces, a note holds a looping wave while the first tracks wait 133
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
slot. Doubutsu no Mori+ and Doubutsu no Mori e+ follow the same rules.

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
the 600-second limit that way. Pokémon Channel's effects, its music included, follow the same rules. So do the effects
of Tales of Symphonia, Star Fox Adventures, Rogue Leader, Pokémon Colosseum, Resident Evil Zero, Mario Party 4, Mario
Golf: Toadstool Tour, Mario Power Tennis, Nintendo Puzzle Collection and the NDDEMO tech demo, and the effects that
WarioWare, Inc.: Mega Party Game$! starts through sndFXStartEx. WarioWare's effects that are songs follow the rules for
its songs. Two of Tales of Symphonia's effects play to the limit too. In one, the macro waits a random time before it
jumps back. In the other, the macro waits 100 minutes before it holds. Three of Rogue Leader's effects reach the limit
as well. Their macro tests a variable, plays a sample and jumps back through GOTO.

The ripper lists and skips the sounds that never make a sound. It also skips a sound that stops the emulated machine
with an error before the sound makes a sound. The ripper's message then gives the error. Luigi's Mansion's se2-0239 is
one: the effect reads an instrument from past the end of its bank's table. `--no-length` leaves out the analysis.
Without the analysis, no sound is skipped and the files have no length tags.

Tags written by `gcsfrip`: `title` and `gcsf_sound` (the sound's name, or the file's name for a sequence that no sound
plays), `game`, `length` and `fade`, `gcsf_mode`, `gcsfby`, `utf8=1`, and optionally `artist`, `year` and `copyright`.

## Copyright

A `.gcsflib` contains copyrighted game code and data. Rips are for personal use.
