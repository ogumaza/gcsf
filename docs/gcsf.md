# GCSF: GameCube Sound Format

Version 1. GCSF stores sequenced music from Nintendo GameCube games so that it can be played back as the game plays it.
A GCSF rip contains the game's code. A player runs that code on an emulated GameCube: the OS, the sound library and the
DSP program of the game. The game plays the sound itself. The rip is therefore exact.

A rip requires a driver profile for the game (see "Ripping"). Version 1 rips games whose sound library is JAudio, in
either of its generations: JAudio1 or JAudio2. JAudio is the sound library of Nintendo EAD's JSystem. Version 1 also
rips games on four other sound libraries: MusyX by Factor 5, the Red driver by Square, a port of the Nintendo 64 sound
library by Nintendo, and the sound library of HAL Laboratory.

## Files

| Extension | Contents |
|---|---|
| `.gcsflib` | Shared data for one game: the game code, the driver, the disc header and the sound files of the game. |
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
zlib. Data that the GameCube itself reads, such as the parameter block of the driver, keeps the big-endian byte order of
the GameCube.

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

The reserved area of at least one loaded file contains a process descriptor. The last descriptor in loading order takes
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
  MHz bus clock. The CPU starts at the descriptor's entry point with floating point enabled and address translation on.
  Block address translation registers map the first 256 MiB of the physical address space cached at 0x80000000 and
  uncached at 0xC0000000. The uncached mapping also reaches the hardware registers at 0xCC000000.
- **Memory.** Main memory (24 MiB at physical 0) and ARAM (16 MiB). Both are zero-filled. Main memory is then loaded
  with the `MEM` chunks. Before the game starts, low memory contains what the IPL and the apploader leave: the first 32
  bytes of the disc header at 0x80000000, the boot magic 0x0D15EA5E at 0x80000020, version 1 at 0x80000024, the memory
  size, console type 3 (a retail console), an arena from 0 to 0x81700000, the file system table of the disc at the top
  of main memory (its address at 0x80000038 and its size at 0x8000003C), and the bus and CPU clocks (0x800000F8 and
  0x800000FC).
- **The disc.** A disc made of the files of the set: `sys/boot.bin`, the 0x440-byte header of the disc, then a file
  system table generated from the paths of the other `FILE` chunks, and their data. The game reads it through the
  registers of the disc interface (physical 0x0C006000). The drive answers the SDK's read, disc ID, seek, inquiry,
  request error, stop motor and audio commands. It answers each command 20,000 CPU cycles after the command starts. The
  data of a read arrives at that point. An inquiry returns revision 2 and the date 2002-04-02. A read past the end of
  the disc fails. Request error then reports 0x052100. An unknown command fails with 0x052000. The cover stays closed.
- **The processor interface.** Its interrupt cause and mask registers drive the external interrupt of the CPU. The
  causes are the disc interface, the external interface, the DSP interface and the video interface. The reset switch
  reads as not pressed.
- **The video interface.** Every NTSC field (1001/60000 s) raises one of the two display interrupts that the OS sets:
  DI1 in one field and DI0 in the next. An interrupt that the OS hasn't enabled isn't raised. The game's code clears
  them. The other registers of the video interface keep what's written to them.
- **The DSP interface** (physical 0x0C005000). It contains the mailboxes, the DSP control register, the ARAM DMA and the
  DMA of the audio interface. The ARAM DMA moves data between main memory and ARAM. It takes 246 CPU cycles for each 32
  bytes. A transfer that starts past the end of ARAM moves nothing. A console without an ARAM expansion does the same.
  The DSP runs one slice behind the CPU. Every 12,600 CPU cycles, it runs the cycles of the previous slice. It runs one
  DSP cycle per 6 CPU cycles. A CPU read of the high half of a mailbox first runs the DSP for 12 cycles.
- **The DSP.** The DSP program of the game runs at the instruction level. The DMA of the DSP reaches main memory. The
  accelerator reads samples from ARAM and writes to ARAM.
- **The audio interface** (physical 0x0C006C00). Its sample counter counts at the streaming rate while playback is on:
  108 MHz / 2,248 or 108 MHz / 3,372. It starts a period after playback starts or the rate changes.
- **The external interface.** Device 1 on channel 0 contains the clock (at 0) and the console settings in SRAM. The
  settings are the defaults, apart from the sound. The sound is set to stereo (bit 2 of the byte at 0x13). Other devices
  are missing.
- **The serial interface.** Nothing is connected. A transfer finishes at once.
- **Output.** The DMA of the audio interface, 16-bit stereo: 8 samples from main memory every 121,392 CPU cycles. That's
  108 MHz / 3,372: 32,028.47 Hz. The player reports the samples as they come. It reports a rate of 32,028 Hz.

Other registers keep what's written to them. A player may skip the iterations of a three-instruction loop that loads a
word from memory, compares it and branches back while it's unchanged. It may skip them up to the next interrupt check.
Nothing else changes memory while the CPU runs. The result is therefore the same.

## Driver

Rips contain one of the GCSF drivers. Most drivers call the functions of a game's sound layer. The MusyX driver calls
the functions of MusyX itself. Several games use it (see below). The game profile names the driver. A driver is loaded
at 0x81700000. Its entry point is at the same address. The rip patches the game's `main` to branch to it. The driver
calls functions of the game that do what the game start-up does for sound. It leaves out the graphics. Every driver
includes `driver/driver.h`, and that header includes ntdlib's `drivers/guest.h`. The two contain the C types, the values
of the first words of the parameter block (see below) and the helpers that read the game memory.

The driver for Four Swords Adventures is `driver/four_swords.c`. It sets up the video interface, the heaps and the ARAM
manager of JSystem, the audio object and the disc thread of the game, and the audio set-up of the game. It loads every
wave bank into ARAM. Then it starts the sound by its JAudio sound ID. The game's music start mutes some tracks of one
piece, `JA_BGM_SELECT`: every child of child 0 of the root track but children 2 and 7. The driver mutes the same tracks
once the piece has started. The game plays the piece with a second set of tracks too. On the screen where the players
join and on the map where they pick a stage, every child of track 0 but child 2 plays. The rip `JA_BGM_SELECT map`
selects that set by the ID 0x8100001A, the ID of the piece with bit 31 set. The IDs of the game leave that bit clear.
For that ID, the driver starts the piece and mutes child 2. From then on it calls the game's audio frame process once
per video retrace. The European release keeps the audio object in another place among the variables of the game. The
profile of that release patches the load of the object in the audio set-up to read the American place. The driver keeps
the object there.

The driver for Twilight Princess is `driver/twilight_princess.c`. It sets up the video interface, the heaps, ARAM
manager and thread switch manager of JSystem, the heaps and the disc thread of the game, and the audio set-up of the
game. It loads the wave groups of the wave list (see below) as the game's scene manager loads them. Then it starts the
sound. As the game's `Z2SeqMgr::bgmStart` does for five pieces, it then mutes the children of track 0 that the block
names (see below). It also sets other children to the volumes that the block names, and writes the status of the piece
to port 9 of the root track of the sound. The game's scenes and `changeBgmStatus` do the same. From then on it calls the
frame process of the audio manager every second video retrace. A frame of the game is a 30th of a second. An entry of
the wave list contains a wave bank in its high 16 bits and a group of the wave system of that bank in its low 16 bits.
The European and Japanese releases run the same driver. Their profiles list the same functions at other addresses. The
Japanese machine set-up makes a larger archive heap.

The driver for The Wind Waker is `driver/wind_waker.c`. It sets up the video interface, the heaps and ARAM manager of
JSystem, and the disc thread of the game. The machine set-up of the game does the same. Then the driver calls the audio
process of the game once a frame until the sound library is up. The library loads its start-up archive and the waves
that stay loaded. It starts the effects sequence. The driver then loads the waves that the `.minigcsf` names. It
restores the default camera of the sound library. That camera stays still. In the game, the sound set-up clears the
camera. The camera process of the game sets it once a frame. The sound library reads it for an effect with a Doppler
shift. Then the driver starts an effect by its JAudio sound ID. From then on it calls the audio process once a frame. A
frame is two video retraces. The game uses the same frame. The driver starts a piece of music as the game's sound layer,
JAIZelBasic, starts it. `bgmStart` starts the main music without a fade. The game's scenes start it the same way.
`subBgmStart` starts the music that plays over it: JA_BGM_MBOSS, JA_BGM_MBOSS_S, JA_BGM_SEA_ENEMY, JA_BGM_P_GANON_1 and
JA_BGM_P_GANON_2. `bgmNowBattle` starts the battle music, JA_BGM_BATTLE_NORM. The starters mute some tracks of a piece
as they start it, by the state of the game. Two of them fade some pieces in over 60 frames. Bits 16 to 19 of the ID of a
piece contain that state: bit 16 for the sail of the boat up, 17 for Link on board, 18 for Link's sword out and 19 for
an enemy near Link. The driver passes them to the setters of the sound layer before the start. The game calls
`bgmNowBattle` once a frame while a fight goes on. The sound layer stops the battle music 30 frames after the last call.
The driver calls it right after `subBgmStart` too, and then once a frame for the music over the main music. The enemy is
at a distance of 0 for bit 19 and of 10,000 otherwise. The call sets the countdown of the sound layer. A piece that
`subBgmStart` starts with the countdown at 0, such as JA_BGM_SEA_ENEMY, stops in the first frame. The sound layer counts
an enemy within 30 as near. The driver first marks a scene as started. The game's scenes do the same. Otherwise
`bgmNowBattle` does nothing. The game gives the sound library 0xA00000 bytes of ARAM and graphics 0x5CE000. The driver
gives the sound library all but 64 KiB of both, 0xFBE000 bytes. A rip draws nothing. In The Wind Waker HD, the waves
that stay loaded take up more than the ARAM that the game sets aside for sound. The Japanese release runs the same
driver. The profile of that release lists the same functions at other addresses, and the smaller archive and game heaps
of its machine set-up. Three fields of the American sound layer are missing from the sound layer of this release. The
profile therefore places the handle of the main music at 0x64 in the layer, not 0x68, and the byte that marks a started
scene at 0x229, not 0x22D.


The driver for Luigi's Mansion is `driver/luigis_mansion.c`. It sets up the video interface, the disc with long file
names, the heaps and ARAM manager of JSystem, and the sound set-up of the game. It loads the wave systems that the game
loads before its first frame. Then it calls the game's sound frame until the sound library is up and every wave system
that stays loaded is in ARAM. Then it starts the sound. From then on it calls the sound frame once a frame. A frame is
two video retraces. The game uses the same frame. An effect starts through the sound library by its JAudio sound ID. A
piece of music starts through the game's start of its music (0x80188760). That function reads the flags of the piece's
entry in the sound table. The flags can send the piece to a second slot, and can make the start set the piece's first
state. The start refuses a piece whose flags keep it out, and the driver then starts that piece through the sound
library. The start sends one piece, LuigiSings, to a function of its own. That function reads the state of the game:
Luigi's health, the lights of the room, the room and its sound information. It picks the tracks that play and sets the
tempo of the piece. The sound frame of the game keeps them up to date. A sound ID of a piece can contain a row of the
profile's table of states in bits 16 to 23, from 1. For a state of LuigiSings, the driver passes the state to the game
before the start. For a state in which a poison mushroom has shrunk Luigi, the driver then calls the game's setter that
raises the tune by an octave. For a state in which a message window mutes Luigi's humming, it calls the game's setter
for that. For a state of another piece, the row gives the game's function that sets the state, and the driver calls it
after the start. The start stores the JAISound of the piece in a word of the game. The driver copies that word to the
handle of the sound once a frame.

The driver for Super Mario Sunshine is `driver/super_mario_sunshine.c`. It sets up the video interface, the disc with
long file names, the heaps and ARAM manager of JSystem, and the sound object of the game. The sound object sets up the
sound library. The game hands its sound object the start-up archive. The driver hands it none. The sound library then
reads the archive from a file of the rip (see "Ripping"). Next come the three wave groups that the game loads at
start-up and before its title. The driver calls the game's sound frame until the sound library is up and those groups
are in ARAM. Then it loads the group of the scene that the `.minigcsf` names. That group goes at the top of ARAM. The
game's scene loader puts it there too. The sound then starts by its JAudio sound ID. A piece of music plays at the
volume that the game sets for its music, 0.75. From then on the driver calls the sound frame once a frame. As in the
game, a frame is two video retraces.

The driver for Pikmin is `driver/pikmin.c`. The game's sound library is an early JAudio1. The game calls it through C
functions, without a sound table. The driver sets up the OS, the disc with long file names, the video interface and
ARAM. Then the driver starts the sound library with a block of the arena and of ARAM. The sound library loads its bank
file and starts two threads. Those threads play the music. The game then gives the sound library a buffer for the disc,
from two words of its system object. Only the set-up of that object in the game fills them in. The driver doesn't run
that set-up. It passes the two words as they are, 0 and 0. The disc thread of the sound library keeps its two 32 KB
buffers. Then the driver reads the sequence of the piece and loads the wave group that the sound ID names (see
"Ripping"). The piece plays in the slot that the game plays its music in. The piece plays at the music volume of the
game options. Without a save, that volume is full. The driver leaves it there. The driver writes the handle of the sound
itself: the address of a word that contains the root track of the piece. Once a video retrace, the driver checks whether
the sound has ended. At the end, the driver sets the handle to 0.

The cutscene music of Pikmin is one piece in parts. The sound library plays that piece from its start-up. The root track
of the piece polls a port. A cutscene writes the number of a part to that port. The root track then opens a track that
plays the part. For a part, the driver loads the wave group of the part and writes the number of the part to the port.
The sound starts at that write. The handle then points at a word that contains the track that plays the part.

The sound library also plays the effects sequence and the events sequence of Pikmin from its start-up. For a sound
effect, the driver loads the wave groups that the game loads for the effects when it sets up its first scene of play.
Then it waits for the tracks that play the effects. The driver sets the scene of the game to the play of a stage. The
game plays two of the system effects only there. The driver then starts the effect with the function of the game for its
kind: an effect of the system, an effect of the player, or an action of an event. For the action of an event, the driver
first creates an event of the type of the action at the camera. The sound starts when a child of the track of the effect
starts to play. The handle then points at a word that contains that child. Once a frame, the driver calls the game's
update of the camera. That update sets the volume and pan of each event by its place against the camera. An event at the
camera plays at full volume, in the middle. The sound of the formation plays in a track of the effects sequence while
the player moves the formation. For that sound, the driver calls the game's function for a move of the formation once a
frame. It passes the value of port 2 of the port list as x and that of port 3 as y. The function writes x and the length
of the move to ports 2 and 3 of the formation track. The function writes 1 to port 0 as the move starts.

The driver for Donkey Kong Jungle Beat is `driver/jungle_beat.c`. It sets up the video interface, the heaps and ARAM
manager of JSystem, and the heaps of the game. It loads the sound files as the game does: the start-up archive, the
sequence archive, the chord archive and the chord database. The game decodes the start-up archive from Yaz0 as it loads
it. The driver then creates the game's audio manager, loads the wave groups of the wave list and starts the sound. Music
starts in the BGM player of the game. The player sets each track of a piece to a state from tables in the game code.
Some tracks start silent. The game turns them on later, or never. A piece whose first state silences every track gets
its next state at once. The game turns the tracks of that piece on later. From then on the driver calls the frame
process of the audio manager once per video retrace. An entry of the wave list contains a wave bank in its high 16 bits
and a group of the wave system of that bank in its low 16 bits.

A variant of a piece of music contains the states that the game sets for the piece during play, in bits 16 to 23 of its
sound ID (see "Ripping"). Bits 16 and 17 set the layer of a combo, bits 18 to 20 a state of the second table of the BGM
player, and bit 21 a flight with a Helibird. The driver starts the piece with those bits 0. It then sets the states
through the game's functions (`jungle_beat_play_states`). For a flight, it sets the flags of the player to 4 and calls
the state function of the player for the flags. For a combo, it sets the number of the stage object to that of
MorningField. The game's function for the stage counts then returns the count at which the combo reaches the layer. The
driver calls the combo function of the player with that count. For a state of the second table, it calls the function of
the player for that table. The game fades to each state. The driver sets each one at once. The last five words of the
parameter block contain the functions, the stage and the offset of the flags.

A chord effect plays over the music of a stage (see "Chord effects" in "Ripping"). The driver starts the music of a
stage through the stage object of the game. Then the driver turns each track of the music off at once with code 2 of the
track states of the BGM player. The tracks still run. The driver calls the game's `startChordSe` with the first record
of the effect until one of the chord players is busy. In each frame, the driver runs the rhythm frame process of the
game before the audio frame. The scenes of the game use the same order. The calls of `startChordSe` by the driver come
between the two. The sound of a step then starts in the audio frame of the frame in which the step falls. The status
turns 1 when the first step of the effect sounds. The sound of a first step that waits for a beat starts in the rhythm
frame process. The status turns 1 before the audio frame of that frame. The notes of the step start after it. The ripper
therefore finds the waves of the notes. The handle contains the JAISound of the step that sounds. While the next step
waits for a beat, the handle contains the JAISound of the last step. The effect ends when no step sounds and none waits.
The driver looks up the record of the effect before the waves load. The work between the last frame process and the
start of another sound then stays as it was. The timing of a start against the DSP frames sets the samples of the
output.

The driver for Mario Kart: Double Dash!! is `driver/double_dash.c`. It sets up the video interface, the heaps and ARAM
manager of JSystem, and the audio heap of the game. It loads the start-up archive as the game does and passes it to the
game's audio set-up. Then it loads every wave bank that the archive names. For a voice, the driver then loads the
start-up archive, in the `baac` block of the first one, that contains the bank of the voice. The game loads one of those
archives at a time, with the same function. The wave bank of the archive goes into ARAM as the loader of the game reads
the archive. Then the driver starts the sound. From then on it calls the audio frame process once per video retrace.
Some effects are level sounds, such as the engines. The start function of the game sets a lifetime of one frame for a
level sound. The game starts the sound again each frame for as long as it should play. The driver starts it again before
each frame until the sound ends.

The driver for Mario Golf: Toadstool Tour is `driver/mario_golf.c`. It sets up the video interface, the arena and heap
of the OS, the ARAM and the game's heaps. The sound set-up of the game then starts MusyX. The driver loads the two MusyX
sound groups of the game. The set-up of Mario Power Tennis also fades the two masters of MusyX from 127 to 101 over 200
ms. The game plays no music before the fade has ended. For Mario Power Tennis, the driver waits 13 video retraces after
the set-up. The driver starts the song by its number in the game's song table, or the jingle by its number in the jingle
table. The game starts a jingle from another thread. The jingle sounds half a second later. A sound effect plays through
the game's functions for effects instead. The sound ID selects what the driver loads first: the voice bank of the
effect, the bank of its court or the narrations of the game. Mario Golf reads the samples of a voice bank from the disc
in chunks. The chunks go into two buffers in turn. MusyX queues the upload of each chunk to ARAM. On the console, each
upload ends before the drive has read the next chunk. The GCSF drive answers sooner than the ARAM DMA moves a chunk.
While a Mario Golf voice bank loads, the driver therefore limits MusyX's queue to one upload. For a Mario Power Tennis
effect of a group that no table of the game lists, the driver loads the group as the bank loader of the game loads its
groups. The driver thread then sleeps. The sound plays from the audio and DSP interrupts. Mario Power Tennis plays
through the same driver.

The driver for WarioWare, Inc.: Mega Party Game$! is `driver/warioware.c`. It sets up the heaps of the game. The file
loader of the game then reads the five sound files from the disc. The driver puts each one where the sound set-up of the
game reads it. The sound set-up initialises the ARAM, the audio interface and MusyX. It loads every MusyX sound group
into ARAM and sets up the reverb. The driver then plays the sound through the game's play function. It passes the mode
in which the game plays music. The mode only sets which slots of the function a sound may take from another sound. The
driver thread then sleeps.

The driver for Mario Party 4 is `driver/mario_party.c`. The game runs MusyX through Hudson's sound manager. The driver
starts the disc library and gives the sound manager a heap of the size that the game gives it. At start-up, the sound
manager reads its file of sound data and the head of the stream file. It initialises the ARAM, the audio interface and
MusyX. It also sets up the reverbs and the stream slots. On a console set to stereo, the game sets the surround mode of
MusyX. The driver sets it too. It loads the base groups that the game loads at boot. Next comes the group set that the
sound test of the game loads for the song. The driver then starts the song by its number in the table of the sound
manager. For an effect, the driver loads the group that contains it, unless that group is a base group. It then starts
the effect through msmSePlay, by its number in the effects table of the sound manager, without parameters. The driver
thread then sleeps. The sound manager runs its fades and its effects from the interrupt of the audio interface.

The driver for Pikmin 2 is `driver/pikmin2.c`. It sets up the video interface, the heaps and the ARAM manager of
JSystem, and the sound system of the game: the start-up archive, the sound layer, and the global scene with the effects
sequence. The game plays each piece of music through an object of its sound layer, in a sound scene. The driver makes a
scene and the object of the piece in the way the game makes them. It loads the resources of the scene: the sequence of
the piece, the conductor of the cave music and the wave groups of the wave list. Then the scene starts the piece. The
game starts the music of a boss when the boss appears, at a part of the music that the boss picks. The driver starts the
boss music at those parts. From then on it calls the frame work of the sound layer once a frame. A frame is two video
retraces. The scene of the driver has no game objects. The music therefore plays as it does when nothing happens in the
game.

The effects sequence of the global scene plays the effects. The global scene also loads wave group 0. That group
contains the waves of every effect. The driver starts an effect as the game starts the effects of its menus: through the
sound system, with no position in the game's world. Some effects read the state of the game. The effects of the ship and
the pod read whether the debt is paid. The driver passes them play data of the game with every flag clear. That is the
state at the start of the story. The effects of the captains' marching horns read the place of a horn in its tune and
the wait between its notes. The driver starts the horns at the beginning of the tune, with the shortest wait that the
game sets: the wait for a stick tilted all the way.

The driver for Pac-Man Vs. is `driver/pacman_vs.c`. The game plays its sound through a thin layer in its code over
JAudio1's `JAIBasic`. The driver sets up the heaps of the game and the video interface. The game runs its other steps on
its task thread. The driver does the same. The disc file reader of the game stops on a thread whose priority number is
below 17. The steps set up the disc, the ARAM manager, the start-up archive, the sequence archive and the sound library.
The sound library loads both wave systems of the game and starts the effects sequence. The driver then starts the effect
through the layer of the game without a position. From then on the driver calls the frame work of the layer once a
frame.

The driver for The Legend of Zelda: Collector's Edition is `driver/collectors_edition.c`. It covers the menu of the
disc. The driver sets up the video interface, the heaps and ARAM manager of JSystem, and the audio heap of the menu. The
driver mounts the sequence archive and reads the start-up archive. The loader thread of the menu does the same. The
audio set-up of the menu then reads the start-up archive into the sound library and loads the wave archive that the
start-up archive marks. The set-up returns the sound manager. The driver waits until the wave archive is in ARAM. Then
the driver starts the sound through the sound manager without a position. The menu starts its effects that way. From
then on the driver calls the frame work of the sound manager once a frame.

The driver for Final Fantasy Crystal Chronicles is `driver/crystal_chronicles.c`. The game plays its music and its
effects through the Red driver by Square, on the SDK's AX. The driver sets up the memory manager, sound manager and file
manager of the game in the order of its system set-up. The sound manager starts Red and the threads of Red. For a piece
of music, the driver then reads the file of the piece and loads the wave bank that the file names. The scripts of the
game load that bank before the piece. The driver loads the piece and plays it through the game's functions. For an
effect, the driver loads what the game has loaded when it plays the effect. An effect numbered below 4000 is in one of
four blocks of effects. The game loads the blocks as it starts, with the two wave banks that their effects use. Those
banks fill the first two bank slots of Red. A track of any effect can pick a wave from either slot. So for every effect,
the driver first calls the game's function that loads the blocks and the two banks. An effect from 4000 on is a SEP file
of its own. The driver then reads the file and loads the wave bank that the file names. Next, the driver loads the file
through the game's function. The scripts of the game load both before the effect. The driver plays the effect through
the function of the game, with the pan, the volume and the fade-in that the game code passes. The driver thread then
sleeps. The sound plays on the threads of Red. The audio interrupt drives them. The memory manager of the game gives its
heap the memory up to 0x81780000 and fills it. The rip patches the set-up of the manager to end the heap at 0x81700000,
below the driver.

The driver for Animal Crossing is `driver/animal_crossing.c`. The sound library of the game is a port of the Nintendo 64
sound library by Nintendo. The port runs the sequence player and synthesis of the Nintendo 64 on the CPU, inside an
early JAudio1 that mixes the result with the DSP. The game calls it through C functions. The driver sets up the OS, its
alarms, the disc, the video interface and ARAM. The game's start-up does the same. The driver then starts the sound
library. A thread of the sound library reads all the sound data of the game, `audiorom.img`, from the disc into ARAM.
The driver runs the game's sound frame once a retrace until the sound library is up. The sound library would then play
the sound of the start-up screen, about 7 s of a voice and a chime. The game plays no music over it. The rip patches out
the function that starts it. The driver sets the weather and starts the piece by the game's number for it. From then on
it runs the sound frame once a frame, at 30 frames a second. In the museum, the driver sets the room after it has
started the piece. The game starts the piece of a music disc at volume 0. Once a frame, the game then sets the volume
and pan of the music from where the listener stands by the stereo. Before each frame, the driver places the listener at
a hi-fi stereo, at a distance of 0. The piece then plays at a volume of 0.8, with every channel in the centre.

Kapp'n sings his songs on the boat in two parts. The game starts the first part as a piece. Once he sings, the game
requests one of ten second parts. That request writes two ports of the sequence player of the first part: a player and a
sequence. About 45 s in, the script of the first part starts that sequence on that player. The sound of the first part
has ended by then. Without the request, the script would start the wrong sequence on the effects player. For a first
part, the driver writes to the ports the player of the first part and a sequence that ends at once. That player then
stops instead. For a second part, the driver writes the sequence of the second part to the entry, in the table of the
game, of a piece that plays an empty sequence. The driver then starts that piece. The game plays the second part like
any other piece.

For an effect, the driver waits for the effects sequence of the sound library. The sound frame of the game starts that
sequence once the sound library is up. The driver then starts the effect by the game's number for it. The sound library
assigns the effect one of six slots. The channel of the sequence with the number of the slot plays the effect. The game
starts an effect that has no place in the scene through one function. In most scenes, that function plays effect 0x424
as effect 0x16e. The driver therefore calls the function that this one calls, with the same values. That function adds a
random number from 0 to 3 or from 0 to 1 to the numbers of some effects. The game then plays the effect of the sum. The
rip patches out both adds. Each rip plays the effect that its number names. The sound library frees the slot when the
effect ends.

Doubutsu no Mori+, the Japanese release of the game, runs the same driver. The sound library of this release is an
earlier build of the same port. The driver calls the same functions at other addresses.

Doubutsu no Mori e+, a later Japanese release of the game, runs the driver too. The sound library of this release is a
later build of the port. The build numbers the pieces and their sequences with 16 bits. The piece table of the game
contains a 16-bit sequence number for each piece. The driver reads and writes the table in 16-bit entries. In this
build, the function that starts the sound library takes the sound of the start-up screen, the size of that sound and the
size of ARAM for the library. The game works out the size of ARAM from the size of `audiorom.img`. The driver passes the
same size. In this release, the pieces of the music discs are 208 to 367. The game mutes six channels of the island's
piece unless the islander is on the island. The driver sets whether the islander is there before it starts a piece. A
port contains a byte. The only sequence that ends at once has a number above 255. For Kapp'n's songs, the driver uses a
piece whose sequence plays no note and ends after one tick.

The driver for the Pokémon Colosseum Bonus Disc is `driver/colosseum_bonus.c`. The disc plays its sequenced sound in one
program, the one that sends Jirachi to a Game Boy Advance. That program is a disc image inside the disc, a TGC (see
"Ripping"). The program uses the sound library of HAL Laboratory. The library runs a script for each effect and plays
the samples of the effects on the SDK's AX. The driver sets up the video interface, the audio interface and ARAM. The
program's main does the same. The driver then starts the sound library with an allocator over the arena of the OS. The
program gives the library an allocator over its heap instead. Next, the driver reads the effect scripts (`pm6fgm.sem`)
into memory and passes them to the sound library. The sound library reads the samples of the effects (`pm6fgm.ssm`) from
the disc into ARAM. The driver waits until the bank has loaded. Then the driver starts the effect by its number at the
highest volume, in the centre. The program plays all of its effects that way. The sound plays from the audio and DSP
interrupts. The sound library keeps a list of the effects that play. An effect leaves the list once its voices have
stopped. Once a retrace, the driver checks the list. The driver leaves out the streamed music of the program, its
graphics and its link to the Game Boy Advance.

`driver/musyx.c` is the driver of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door, Tales of
Symphonia, Star Fox Adventures, Star Wars Rogue Squadron II: Rogue Leader, Star Wars Rogue Squadron III: Rebel Strike,
Pokémon Colosseum, Resident Evil, Resident Evil Zero, Nintendo Puzzle Collection and the NDDEMO tech demo. The driver
calls the functions of MusyX, not a layer of the game code over them. Other games whose sound library is MusyX can use
it too. The profile of a game lists the addresses of the functions and the values that the game passes to MusyX. The
driver first calls the game's start-up functions that come before its sound, such as the set-up of the disc. Next come
the ARAM, the queue of the ARAM and the audio interface. The driver then sets the memory hooks of MusyX. The hooks hand
out the arena of the OS from the bottom up. sndInit follows, with the numbers of voices and the share of ARAM that the
game gives it. Then the driver sets the game's output mode. MusyX starts each of its volume groups at a volume of 0. The
start of a song sets the group of the song. The start of an effect leaves the group of the effect alone. The start-ups
of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door, Tales of Symphonia, Star Fox Adventures,
Rogue Leader, Rebel Strike, Resident Evil Zero, Nintendo Puzzle Collection and the NDDEMO tech demo call sndVolume(127,
0, 0xFF). That call sets every group to full volume. The driver makes it for these games too. The driver reads the entry
of the sound from the catalogue of the rip (see "MusyX catalogue" below). The entry can contain words for the driver to
write into the game memory, such as the reverb settings of the game. A game's profile can name aux effects for studio 0.
The driver prepares them and passes them to sndSetAuxProcessingCallbacks. The driver writes the words of the entry
before it prepares the effects. A word can therefore change the effects in the block of the driver. The songs of Yoshi's
Cookie in Nintendo Puzzle Collection do that (see "Ripping"). After the words, the driver can set the volume of the
groups through sndVolume, and the master volumes of MusyX for music and for effects through sndMasterVolume. Both calls
take a time of 0. The parameters of the profile or the words of the entry contain the volumes, and the group that
sndVolume sets. Each group that the entry names is in a set of four files in the rip: the project, the pool, the sample
directory and the samples. The driver reads them and pushes the group with sndPushGroup. Groups in a row from one set
share its files. MusyX copies the samples to ARAM. A song plays from its file through sndSeqPlayEx, with the group of
the song and its MusyX song ID. The parameters of the profile or the words of the entry can set a fade-in for the song.
The driver then starts the song at a volume of 0 through the parameters of sndSeqPlayEx. It then calls sndSeqVolume with
the volume of the song and the time of the fade-in. MusyX raises the volume over that time. Star Fox Adventures and the
cutscenes of Resident Evil Zero start their songs that way. An effect plays through sndFXStartEx. A volume or a pan of
0xFF there takes the value of the effect from the effects table of its group. The entry can then set the MIDI
controllers of the effect, through sndFXCtrl and sndFXCtrl14. Paper Mario: The Thousand-Year Door sets an effect's
volume, its pitch wheel and its send to the reverb that way. The driver turns external interrupts off from the start of
the effect to its last controller. No audio frame plays the effect without its controllers. The driver thread then
sleeps. The sound plays from the audio and DSP interrupts.

`driver/musyx_song_controls.c` is the driver of Resident Evil. It builds `driver/musyx.c` with one addition. The code of
a room sets the levels and the pans of the channels of a song once the song plays. It calls sndSeqCntrl for each
channel, with CC7 and CC10. The catalogue entry of a song can contain such settings. The driver then sets an OS alarm as
the song starts. The handler of the alarm sets each controller through sndSeqCntrl, with external interrupts off. The
parameter block of the driver contains four more words after the other ones: the addresses of sndSeqCntrl, OSCreateAlarm
and OSSetAlarm, and the time of the alarm in ticks of the time base. For Resident Evil, the time is 250 ms. That comes
after the controllers of the first pattern of a song and before its first note.

`driver/musyx_upload.c` is the driver of Pokémon Channel. It builds `driver/musyx.c` with one addition. The game's
sample file is larger than main memory. The driver pushes each group without samples in memory. MusyX then calls its
upload callback (sndSetSampleDataUploadCallback) for each 0x20000 bytes of the samples of the group. The driver reads
each chunk from the sample file of the rip into a buffer. MusyX copies the buffer to ARAM through its queue. Before the
driver reads the next chunk, it waits until the queue is empty (aramSyncTransferQueue). The parameter block of the
driver contains two more words after the other ones: the addresses of sndSetSampleDataUploadCallback and
aramSyncTransferQueue. At start-up, Pokémon Channel fades the master volume to 100 and the volume of every group to 127
over 100 ms. The driver sets both at once.

The parameter block of every driver is at **0x81700010**. Its words are big-endian:

```
0x00  u32  magic "GCSD"
0x04  u32  version (1)
0x08  u32  status (written by the driver): 0 booting, 1 playing, 2 finished, 0x80000000 | step on error
0x0C  u32  sound ID: the sound to play
0x10  u32  the sound's handle (written by the game): the sound's object while it plays, then 0
0x14  ...  game profile (see the driver's source). The profile of Four Swords Adventures contains the retraces
           between audio frames, the sizes that the game's start-up uses, then the addresses of the game's functions
           and the offsets of their objects
```

The waves of a game may not fit in the audio memory. A driver for such a game loads only the waves that the sound uses.
The block of such a driver contains a wave list after the handle, and the game profile after the list:

```
0x14   u32  the number of waves that the sound uses, at most 64
0x18   u32  the waves: 64 entries. The driver's source says what an entry means
0x118  ...  game profile (see the driver's source)
```

The driver for The Wind Waker has a wave list. An entry contains the number of a wave system in its high 16 bits and a
group of the system in its low 16 bits. The driver skips a wave system that the sound library has loaded already. Each
wave system of the game has one group.

The driver for The Wind Waker reads bits 20 to 27 of the sound ID as a count of random numbers. Before the audio set-up
of the game, the driver draws that many from the generator of the sound library (`JASystem::Player::getRandomS32`). The
first draw of the sound then comes later in the run of the generator. The sound starts at the same time as without the
draws. The other bits contain the JAudio sound ID. The sound IDs of the game leave bits 20 to 29 clear.

The entry of a driver is at 0x81700000. The word at **0x81700004** contains the address of its port list, or 0 for a
driver without one. The list contains the number of ports that the player sets, then 16 entries. An entry contains the
number of a port in its high 16 bits and its value in its low 16 bits. A player fills the list from the `gcsf_var` tag
(see "Tags"). The driver sets those ports for the sound. The driver for The Wind Waker has a port list, after its game
profile. Port 6 contains the low 16 bits of the map information that the game passes with an effect. The driver passes
the value of port 6 to `startSoundVec` as that information. The driver sets each other port of the sound after the
start, through `JAISound::setPortData`. The sound library writes those ports to the track of the effect when its
sequence starts. The game's `seStart` sets port 9 of every effect that way: the reverb of the room that the effect plays
in. Port 9 of a piece of music contains the status that the actors of the game set for the piece while it plays. The
root track of a piece clears its ports in its first commands. The driver therefore writes the ports of a piece to the
root track after the first tick of the sequence, through `JASystem::TTrack::writePortAppDirect`, from a callback that it
registers with the sound library. The sequence reads the value at its next poll of the port after the first tick. The
driver for Pikmin has a port list after its game profile too.

The driver for Pikmin 2 has a wave list too. Its entries have the same layout. The game has one wave system, with 49
groups. Group 0 contains the effects. The sound system loads it at start-up, and the driver skips it. A scene loads at
most two other groups. Like the driver for The Wind Waker, the driver reads bits 20 to 27 of the ID of an effect as a
count of random numbers. It draws them from the generator of the sound library (`JASPlayer::getRandomS32`) before the
set-up of the game. The effect IDs of the game leave bits 16 to 30 clear.

The driver for Donkey Kong Jungle Beat has a wave list too. The driver writes the word at 0x1E8 of the block: the
JAISound of the music that a chord effect plays over, or 0. The ripper leaves the commands of the tracks of that sound
out of its length analysis. The game profile goes on after that word: the combo effect (see "Chord effects"), then the
effects that read a value at port 0xb (see "Effects that the game starts with a count").

The driver for Twilight Princess has a wave list too. Its game profile ends with the address of the variable that
contains the music manager of the game, the address of `Z2SeqMgr::setChildTrackVolume` and the address of
`Z2SoundStarter::setPortData`. The state of the piece follows the profile. The word at 0x1F8 of the block marks the
children of track 0 of the sound that the driver mutes after it starts the sound, bit n for child n. The word at 0x1FC
marks the children 0 to 15 that play at a volume between 0 and 1, bit n for child n. The 16 words from 0x200 contain
their volumes, the volume of child n at 0x200 + 4n, as the bits of a float. The word at 0x240 contains the status of the
piece: 0x10000 plus the value that the driver writes to port 9 of the root track of the sound, or 0 for none. The driver
sets the volumes at once, as the game does. It then writes the status.

The driver for Mario Golf doesn't use the handle. The top byte of a Mario Golf sound ID selects the table. The low 16
bits contain the number of the song in that table. Table 0 contains the songs, and table 1 the jingles. Mario Power
Tennis has only table 0. That table contains the jingles too. The driver plays them as songs. A top byte from 0x80 to
0x83 selects a sound effect. With 0x80, the low 16 bits contain the ID of the effect. Bits 16 to 23 contain the number
of its voice bank plus 1, or 0 for an effect of the bank that the game loads at start-up. With 0x81, the low 16 bits
contain the number of a narration in the narration table of the game. With 0x82, the low 16 bits contain the ID of an
effect in the bank of a court, and bits 16 to 23 the number of the court. With 0x83, bits 0 to 11 contain the ID of an
effect of a group that no table of the game lists. Bits 12 to 23 contain half the number of the first file of the group.

The driver for WarioWare doesn't use the handle either. A WarioWare sound ID is the MusyX song ID: the MusyX song group
of the song in the top byte and its number in the low 16 bits. The driver passes that number to the game's play
function. An effect that isn't a song has bit 31 set and the number for the play function in its low 16 bits: 1606 plus
the MusyX ID of the effect.

The driver for Mario Party 4 doesn't use the handle either. A Mario Party 4 sound ID contains the group set that the
driver loads in its top 16 bits. Its low 16 bits contain the number of the song in the table of the sound manager. A
group set of 0xFFFF loads none. The ID of an effect has bit 15 set in its low 16 bits. The other 15 bits contain the
number of the effect in the effects table of the sound manager. The top 16 bits then contain the group to load alone,
with bit 15 set, or 0xFFFF for none.

The driver for Crystal Chronicles doesn't use the handle either. The top byte of a Crystal Chronicles sound ID selects
the kind of sound: 0 for a piece of music and 1 for an effect. The low 24 bits contain the number of the sound. The
number of a piece is the number in the name of its file. The number of an effect is the number that the game uses. Below
4000, it's the number of the block times 1000 plus the place of the effect in the block. From 4000 on, it's the number
in the file name of the effect.

The MusyX driver doesn't use the handle either. A sound ID for the MusyX driver is the number of the sound in the
catalogue of the rip.

The game starts its music by the file name of the sequence. A Pikmin 2 sound ID is 0x80000000 plus the number of the
piece in the piece table of the driver. The table contains at most 96 pieces: the 48 pieces, then their variants. It
lists the file of each piece, how the game makes the object of the piece, the conductor of the cave music, the sound
information of the object, and the part at which the music of a boss starts. The sound ID of an effect is its ID in the
sound table (see "Ripping").

The driver for Animal Crossing writes the handle itself. For a piece, the handle is the sequence player that plays the
piece. For an effect, it is the channel that plays the effect. The driver writes 0 when the sound ends. The top byte of
an Animal Crossing sound ID selects the kind of sound: 0 for a piece of music and 1 for an effect. The ID of a piece
contains the room of the museum in bits 16 to 23, the weather in bits 8 to 15 and the number that the game gives the
piece in the low byte. The game numbers the weather 0 for fine weather, 2 for snow and 3 for cherry blossom or falling
leaves. In the rain, the game plays another piece. The 24 pieces that play the hours change with the weather: the game
mutes some of their channels by the weather and the hour. The game numbers the rooms of the museum 1 for the lobby, 2
for the paintings, 3 for the fish, 4 for the insects and 5 for the fossils. Each room mutes some channels of the museum
piece. Every other piece ignores both. The low 16 bits of the ID of an effect contain the number that the game uses for
the effect: its bank in bits 8 to 15 and its place in the bank in the low byte. The sound IDs of Doubutsu no Mori+
follow the same rules.

In Doubutsu no Mori e+, the numbers that the game uses for pieces take 16 bits. The ID of a piece there contains the
number in the low 16 bits, the weather in bits 16 to 19 and the room of the museum in bits 20 to 22. Bit 23 is set when
the islander is on the island. Only the island piece, 376, depends on that bit. The effect IDs of the release follow the
same rules as in Animal Crossing.

The driver for the Bonus Disc writes the handle too. While the effect plays, the handle is the entry of the effect in
the table of the sound library. The driver writes 0 when the effect ends. A Bonus Disc sound ID is the number that the
sound library uses for the effect: its group in bits 16 to 31 and its place in the group in the low 16 bits.

A `.minigcsf` patches the sound ID: a 4-byte `MEM` chunk at 0x8170001C. For a driver with a wave list, it also patches
the list: a `MEM` chunk at 0x81700024 with the number of waves and the waves. A rip of a piece to which Twilight
Princess gives a state patches the state: a `MEM` chunk at 0x81700208 with the words of the state, up to the last that
isn't 0. A state that only mutes children takes 4 bytes. A sequence that no sound plays also requires a `FILE` chunk
(see "Ripping"). After applying all `MEM` chunks, a version 1 player checks the driver block. If the block has the
expected magic, the player rejects versions other than 1. The block of every driver is version 1, whatever its layout
after the status word. A player reads only the magic, the version and the status word. The driver reads the rest. A
version 1 player therefore plays the rips of every game, including games that later releases add.

The status address in the descriptor points at the status word (0x81700018). A player uses it at both ends of the sound:

- **Start.** The player runs the rip from boot and discards the output until the status first reads 1 or 2. The boot and
  the loading of the sound then don't count towards its length. The player stops with an error if the status has bit 31
  set. The low byte is the step that failed. Step 1 is the check of the block by the driver. The driver source lists the
  other steps. The player also stops with an error if the status hasn't reached 1 after 10 emulated seconds.
- **End.** Like any PSF player, a player plays for the tagged length plus fade. The GCSF players count them from the
  first sound (see "Tags"). Without a length tag, a player plays until the driver reports that the sound has finished
  (status 2). The GCSF players then add their silence after a track, 1 second by default. A player stops sooner if a
  default length and fade end first. The player checks the status after every millisecond of emulated time.

### MusyX catalogue

A rip for `driver/musyx.c`, `driver/musyx_upload.c` or `driver/musyx_song_controls.c` contains the game's MusyX files in
the library, as `FILE` chunks in `MusyX/`. A set of four files contains one or more groups. The name of each file is the
number of the set: `MusyX/0.proj` (the project), `MusyX/0.pool`, `MusyX/0.sdir` (the sample directory) and
`MusyX/0.samp` (the samples). The file of a song is `MusyX/<number>.sng`. The catalogue, `MusyX/sounds.bin`, lists the
sounds. Its values are big-endian:

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

The entry of a sound:

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
0x12  u16  the number of the sound's controller settings
```

A group entry contains two 16-bit values: the MusyX ID of the group and the number of the set that contains it. A word
contains two 32-bit values: an address in the game memory and the value to write there. The driver writes the words of a
sound, then sets up the aux effects. It pushes the groups of the sound in their order. Then it starts the sound. With a
volume other than 0xFF, or with a fade-in, the driver passes the volume of a song to sndSeqVolume once the song starts.
A fade-in with a volume of 0xFF raises the song to 127, the full volume of MusyX. The controller settings of an effect
follow its words in the list, in the same form. The first value contains a MIDI controller in its low byte. With bit 8
set, sndFXCtrl14 sets the controller to the 14 bits of the second value. Without it, sndFXCtrl sets the controller to
the second value. The driver sets them in their order once the effect has started. Only `driver/musyx_song_controls.c`
reads the controller settings of a song. Bits 16 to 23 of the first value then contain the MIDI channel. An OS alarm
sets them through sndSeqCntrl, in their order, at the time after the start of the song that the parameters of the driver
set.

The library has a file for every set and every song. Besides the catalogue, the driver reads only the sets of the groups
of a sound and the file of its song. A set or a song that no sound of the rip uses contains only zeros. Its files keep
their sizes. The file table of the disc and the places of the other files on the disc then stay the same. The sounds of
a rip are the ones that it has a `.minigcsf` for. A rip made with `--bgm` or `--only` therefore contains the samples of
fewer sets.

## Tags

The standard PSF tags apply: `title`, `artist`, `game`, `year`, `genre`, `comment`, `copyright`, `length`, `fade`,
`volume`, `_lib`, `_libN`. `gcsfby` credits the ripper. The ripper also writes `gcsf_sound` with the name of the sound
from the sound table of the game, and `gcsf_mode` with the value `game`.

`length` and `fade` count time at the reported rate of 32,028 Hz: a player plays `length` × 32,028 stereo samples
before the fade. They count from the first sound: the first stereo sample of the player's output that isn't 0 on
either channel. A player that skips the silence before the first sound starts there.

The ripper writes `length` as `m:ss.mmm` and `fade` as whole seconds. A sound that loops gets `length` at the end of its
second pass and `fade=7`, with `loop_start` and `loop_length`: the time to the start of the loop and the length of one
pass, both `m:ss.mmm` and counted from the first sound. `length` is `loop_start` plus two times `loop_length`. A loop
that starts before the first sound gets `loop_start=0:00.000`, and `length` is then shorter by the time before the first
sound. So does a loop with no intro whose first pass is up to 50 ms shorter than the next. Its `loop_length` is the
second pass. The later passes repeat that pass. A sound whose tracks loop at different lengths, and a sound that holds,
get the fade of a loop without the loop tags. A sound that ends gets `length` at its last sound and `fade=1`. A sound
still playing at the ripper's limit gets `length=10:00.000` and the fade of a loop. A player can read the loop tags to
play another number of passes.

`gcsf_var` sets ports of the sound before the sound starts. Some effects read a value that the game code puts on a port
when it starts them, such as the material of the ground under a footstep. They play nothing, or another variant, without
it. The value is one or more assignments `N=V`, separated by commas or white space. Line breaks count as white space. N
is a port, 0 to 15. V is −32,768 to 32,767, and the port takes its low 16 bits. Each number is decimal, or hex with a
`0x` prefix, and may have a minus sign. A later assignment to the same port wins. A player ignores the whole tag if any
part of it is malformed or out of range. The player writes the ports into the driver's port list (see "Driver") after it
loads the MEM chunks of the set and before the boot. A set whose driver has no port list ignores the tag. `gcsfrip`
writes the tag for each variant of such an effect, with the assignments in increasing order of N, in decimal, separated
by ", ": `gcsf_var=6=4`. The foobar2000 component keeps the tag when it rewrites the tags of a file.

## Ripping

`gcsfrip` builds sets from a GameCube disc image or an extracted disc. A game requires a driver profile. Profiles
(function addresses) are specific to each game and version. They're matched by game ID and the SHA-1 of `main.dol`.
Version 1 supports The Legend of Zelda: Four Swords Adventures (USA), its European release and its Japanese release, The
Legend of Zelda: Four Swords Plus, The Legend of Zelda: Twilight Princess (USA) and its European and Japanese releases,
The Legend of Zelda: The Wind Waker (USA) and its Japanese release, Zelda no Densetsu: Kaze no Takuto, Luigi's Mansion
(USA), Super Mario Sunshine (USA), Pikmin (USA), Donkey Kong Jungle Beat (USA) and Mario Kart: Double Dash!! (USA). It
also supports Mario Golf: Toadstool Tour (USA), Mario Power Tennis (USA), WarioWare, Inc.: Mega Party Game$! (USA),
Pikmin 2 (USA), Pac-Man Vs. (USA), the menu of The Legend of Zelda: Collector's Edition (USA), Final Fantasy Crystal
Chronicles (USA), Animal Crossing (USA), Doubutsu no Mori+ (Japan, disc versions 0 and 1), Doubutsu no Mori e+ (Japan,
disc versions 0 and 1), the Pokémon Colosseum Bonus Disc (USA), Metroid Prime (USA, disc version 2), Metroid Prime 2:
Echoes (USA), Paper Mario: The Thousand-Year Door (USA), Tales of Symphonia (USA), Star Fox Adventures (USA, disc
version 1), Star Wars Rogue Squadron II: Rogue Leader (USA), Star Wars Rogue Squadron III: Rebel Strike (USA), Pokémon
Colosseum (USA), Resident Evil (USA, first disc), Resident Evil Zero (USA), Pokémon Channel (USA), Mario Party 4 (USA,
both disc versions, Europe and Japan), Nintendo Puzzle Collection (Japan) and the NDDEMO tech demo. The profile of the
Bonus Disc matches the program in a TGC on the disc (see below).

The `.gcsflib` contains:

- the sections of the game's `main.dol`, with `main` patched to branch to the driver, and the other patches of the
  profile (they skip the clear of the OS arena: `ClearArena` in Mario Golf and the same code inside `OSInit` in Mario
  Power Tennis. The patch of Final Fantasy Crystal Chronicles ends the heap of the memory manager below the driver);
- the driver and its parameter block with the game profile;
- for a JAudio game, the disc header (`sys/boot.bin`), the start-up archive of the sound library (a BAA, or an AAF in
  The Wind Waker, Luigi's Mansion, Super Mario Sunshine, Pikmin 2 and Pac-Man Vs.), the sequence archive if the game has
  one, the other sound files of the game (the chord archive and chord database of Donkey Kong Jungle Beat), and wave
  archives (`.aw`), as `FILE` chunks. Super Mario Sunshine keeps its start-up archive, `msound.aaf`, in
  `data/nintendo.szs`, an archive compressed with Yaz0. The library contains it unpacked, as `AudioRes/mSound.aaf`. The
  sound library reads that file when the game hands it no archive. Pikmin has no start-up archive. Its library contains
  the disc header, the bank file (`pikibank.bx`), the sequence archive (`pikiseq.arc`) and every wave archive that the
  wave systems of the bank file name. The library of Pikmin 2 also contains the other files that the sound layer of the
  game reads: the conductors (`Conductor.arc`), `Key.arc`, the lists of music and streams, and the track maps. For Mario
  Golf and Mario Power Tennis, the header of the disc, the files of the two MusyX sound groups of the game, the file of
  every song (an SNG) and the files of the other groups that contain effects, four for each group, as `FILE` chunks. For
  Final Fantasy Crystal Chronicles, the header of the disc, the file of every piece (`dvd/sound/Music/*.bgm`), the four
  blocks of effects that the game loads (blocks 0 to 3 in `dvd/sound/SE/Block/`), the SEP files of the effects from
  number 4000 on (`dvd/sound/SE/Sep/*.sep`) and the wave banks that the pieces and the effects name
  (`dvd/sound/Wave/*.wd`), as `FILE` chunks. For Animal Crossing, Doubutsu no Mori+ and Doubutsu no Mori e+, the disc
  header and the sound data of the game (`audiorom.img`), as `FILE` chunks. For the Pokémon Colosseum Bonus Disc, the
  disc header of the program in the TGC and the scripts and samples of the effects (`pm6fgm.sem` and `pm6fgm.ssm`), as
  `FILE` chunks. For a game for `driver/musyx.c`, `driver/musyx_upload.c` or `driver/musyx_song_controls.c`, such as
  Metroid Prime, the disc header, the catalogue of the game's sounds, and the MusyX files of their groups and songs (see
  "MusyX catalogue");
- a process descriptor.

The wave archives are every one that the wave systems of the start-up archive name, unless the game loads its wave
groups as its scenes require them. Twilight Princess, The Wind Waker, Super Mario Sunshine, Donkey Kong Jungle Beat and
Pikmin 2 do. The libraries of The Wind Waker and Super Mario Sunshine also contain the archives that the sound library
loads at start-up. The library of Pikmin 2 contains the archive of group 0. A wave system lists the waves of a wave bank
in groups. Each group is one wave archive. A wave can be in several groups. For such a game, the library contains the
archives of the groups that the sounds of the rip use. Each `.minigcsf` lists the groups of its sound (see "Wave
groups").

Each `.minigcsf` selects one sound by its JAudio sound ID. The sound table in the start-up archive lists the sounds and
names them. In the table of a BAA, the ID of a sound contains its section in the top byte, its group in the next byte
and its index in the low 16 bits. `--bgm` selects the sounds of the section named BGM. Mario Kart: Double Dash!! names
it TYPE_BGM. `--only` selects the sounds whose names match a regular expression.

The start-up archive of The Wind Waker is the AAF of JAudio1. Its sound table lists the effects in categories, then the
music. It names no sound. An effect is named after its category and index, for example `se4-0123`. A piece of music is
named after its file in the sequence archive. The ID of an effect contains its category in bits 12 to 19 and its index
in the low 10 bits. Bit 11 is set. The game sets that bit for most effects. With it, the sound library plays the effect
to its end. The ID of a piece of music is 0x80000000 plus its index. The first piece in the table is the effects
sequence. The sound library plays it all along. The ripper leaves it out. The start-up archive of Luigi's Mansion is an
AAF too, and its sounds follow the same rules. So do the sounds of Super Mario Sunshine. That game names its sounds in
`AudioRes/mSound.asn`: a 16-byte header, then 32 bytes for each name, with the name in up to 28 bytes and then a word.
The first 18 name the categories of the sound table. Each one after them names a sound. Its word is the ID of the sound.
A rip uses those names, for example `MSD_BGM_DOLPIC`. In the table, a piece of music has the ID 0x80010000 plus its
index, and the driver starts it by that ID.

The sound layer of The Wind Waker plays 14 pieces of music in more than one arrangement (see "Driver" for the starters).
A rip starts each piece as the game starts it, in the state that the sound layer starts in: the sail of the boat down
and the boat still, Link off board, his sword sheathed and no enemy near. The rip also contains each other arrangement,
after its piece and with the number of the piece. Its title is the title of the piece, a space and the name of the
arrangement. The IDs of the game from 0x80000105 to 0x80000152 start a piece in another arrangement. Such an arrangement
is named after its ID, for example `kaminoto 80000125`. 0x80000125 also plays the piece 1.3143 times as fast. The other
arrangements are named after the state: `sailing` (`sea` and `sea_strm`), `on board` (`sea_enemy`), and `near`, `sword`
and `near sword` (`enemy2`, the battle music, and `mboss` and `mboss_s`, the miniboss music). An arrangement that plays
what another plays is left out. The profile lists them (`GameProfile::music_states`). Each contains its state in bits 16
to 19 of its sound ID.

Donkey Kong Jungle Beat changes the tracks of its music during play (see "Driver"). A rip starts each piece as the game
starts it. It also contains the later states of 43 pieces, 101 variants in all. Each variant follows its piece and has
the number of the piece. Its label is the label of the piece, "_" and the name of the state, for example
`JA_BGM_MAIN_01_combo1`. The states:

- `combo1` and `combo2`: the layers of a combo. Once the count of the combo reaches the first or second count of the
  stage, the BGM player plays more tracks. Most stages use the counts 10 and 45. At the third count, the BGM player
  plays the same tracks as at the second. That count gets no variant.
- `helibird`: the tracks that a Helibird adds while it carries DK, in three pieces. `helibird_combo1` and
  `helibird_combo2` add the layers of a combo too.
- `below_75`, `change`, `round2` and `round2_below_30`: the fights against the four Kongs. The boss's face sets these
  states once the boss has 75 % of its health or less, while the boss changes between the rounds, in the second round,
  and once the boss has 30 % or less there.
- `hit`, `change`, `round2` and `round2_hit`: the two-round fights against GoroPig, ElephantCannon and SlumpBird. The
  code of the boss sets these states while DK hits or holds the boss in the first round, while the boss changes between
  the rounds, in the second round, and while DK hits or holds the boss there.
- `hold`, `below_60`, `below_60_hold`, `below_30` and `below_30_hold`: the final fight. The health of the boss sets the
  state. `hold` plays while DK holds on to the boss.
- `underwater`: the music of TropicalCoast while DK is below a depth of 12,000.
- `fall`: JA_BGM_MAIN_22 while DK falls from the Helibird into the final fight.
- `state1` and `state2`: the other states of the second table of the BGM player. The frame of VsFinalKong sets state 2
  of JA_BGM_MAIN_22 by the place of DK. The frame of EndingCeremony sets state 2 of JA_BGM_ENDING_CELEMONY by the place
  of DK. The music of the bind, JA_BGM_BIND_01, gets state 1.

The pieces with variants play in these stages, by the names in the game's stage table:

| Piece | Stages |
| --- | --- |
| JA_BGM_MAIN_01 | MorningField |
| JA_BGM_MAIN_02 | SteepValley |
| JA_BGM_MAIN_03 | DeepJungle |
| JA_BGM_MAIN_04 | BigGodTree |
| JA_BGM_MAIN_05 | TropicalCoast |
| JA_BGM_MAIN_06 | SkyGarden01, TheNestOfHeliBird |
| JA_BGM_MAIN_07 | GoUpTheWaterFall |
| JA_BGM_MAIN_08 | EscapeFromRelic |
| JA_BGM_MAIN_09 | KikkiFestival |
| JA_BGM_MAIN_10 | FactoryRelic, UnderGroundMaze |
| JA_BGM_MAIN_11 | DesertRelic |
| JA_BGM_MAIN_12 | IceCave, IceTunnel |
| JA_BGM_MAIN_13 | InfernalZoo |
| JA_BGM_MAIN_15 | CloudHeaven |
| JA_BGM_MAIN_16 | AboveTheClouds |
| JA_BGM_MAIN_17 | ClockTower |
| JA_BGM_MAIN_18 | FairyWood |
| JA_BGM_MAIN_19 | RoadToCastle |
| JA_BGM_MAIN_20 | InTheCastle |
| JA_BGM_MAIN_21 | DemonFireMarsh |
| JA_BGM_MAIN_22 | VsFinalKong, VsFinalKong2 |
| JA_BGM_UG_01 | LavaValley |
| JA_BGM_VOLCANO | VolcanoBottom |
| JA_BGM_BIRDRACE_01 and _02 | BirdLand, GreatestRace (_02 from the start of the race) |
| JA_BGM_SEARACE_01 and _02 | SwimRace (_02 from the start of the race) |
| JA_BGM_NINRACE_01 and _02 | RelicOfJungle (_02 from the start of the race) |
| JA_BGM_MATSURI_A and _B | DreadKong01 (_B from the start of the boxing) |
| JA_BGM_BOSS_KONFU_A and _B | VsKonfu (_B from the start of the boxing) |
| JA_BGM_BOSS_MIYAMOTO_A and _B | VsMiyamoto (_B from the start of the boxing) |
| JA_BGM_BOSS_AKEBONO_A and _B | VsAkebono (_B from the start of the boxing) |
| JA_BGM_BOSS_GOROPY_B | VsGoroPig1 to 4 |
| JA_BGM_BOSS_IUZOU_B | VsElephantCannon1 to 4 |
| JA_BGM_BOSS_BIRD_B | VsSlumpBird1 to 4 |
| JA_BGM_BOSS_FINAL | VsFinalKong, VsFinalKong2 (from the start of the fight) |
| JA_BGM_ENDING_CELEMONY | EndingCeremony |
| JA_BGM_BIND_01 | the bind |

A variant plays its state from the start of the piece. In the game, a state comes partway through the piece, with a fade
of 5 to 180 frames. The rips leave out the change from one state to the next. The profile lists the variants
(`GameProfile::layer_states`). Bits 16 to 23 of the sound ID of a variant contain its states.

Some objects of a stage also play a chord effect beside the music of the stage while they're near DK (see "Chord
effects"): a sleeping enemy, an angry one, an enemy that waits for DK's attack, the attack chance of a stunned enemy,
the battle of the coconut pigs, the rush of an armadillo and others. These effects repeat their steps until the object
stops them. Some of them replace a part of the music. A rip also contains each pair of a piece and such an effect that a
stage plays: 135 variants of 27 pieces. The label of a variant is the label of the piece, "_" and the name of the effect
without `JA_SE_IST_`, in lower case, for example `JA_BGM_MAIN_01_enemy_sleeping`. The variants follow the states of the
piece and have its number. The effects:

- `enemy_sleeping` (record 134): PigPoppoSleep and KobuPakkun.
- `enemy_angry` (135): Garigari, KobuPakkunAngry, NoFin, NoMouth, PigPoppoFire, PigPoppoKiba, RollingFrog and BigBee.
- `dk_wait_attack` (136): most enemies while they wait for DK's attack.
- `dk_has_wait` (138) and `dk_has_weight_damage` (143): MiniIga and its fire and ice kinds.
- `enemy_wait_pig` (140) and `enemy_blow_wind` (141): PigPoppo, and BigBee and PigPoppoFire for the second.
- `attack_chance` (157 and 158 together): a stunned enemy, a pig that faints, and a CocoNut that DK hits in a GoroPig
  fight.
- `enemy_battle` (159): BigBee, NeedlePoppo and CocoPig.
- `garigari_rush` (34), `garigari_spin` (37) and `garigari_ball_roll` (44): Garigari.
- `only_head_move` (46), `bubble_flower_rotate` (47) and `lect_kikki_run` (132): OnlyHead, BubbleFlower and
  LectureKikki.

The names are those of the game's object factory (0x8034e000). Most objects request their record once a frame through
0x80256fcc (record, object), from 0x802301b4 and the helpers at 0x80230264 to 0x802302f4. 0x80256fcc starts the record
through `startChordSe` while its effect doesn't play (0x8024cba4), and keeps the effect playing otherwise. The battle of
the coconut pigs and the attack chance start once instead, through the list of shared effects of the audio manager at
+0x1040 (0x802570f0 and 0x8025751c). For a variant, the driver writes the number of the stage to the number and row of
the stage object and starts the music of the row through 0x8025049c. The stage does the same. Each frame, the driver
then requests the records through 0x80256fcc, or starts the records that start once through 0x80256f48 while they don't
play. The tracks of the music play as the stage starts them. The first state of JA_BGM_OP_CEMEMONY turns every track
off. The rip of that layer writes the second state of the piece over its first in main memory. The start of the stage
then plays the tracks of the piece. The rip of the piece alone plays them the same way. The length analysis leaves out
the tracks of the effects: a track belongs to an effect if its root track belongs to a JAISe. Each variant has the
length of the music.

Two variants play other music than the row of their stage names. At VsFinalKong (0x56) and VsFinalKong2 (0x58), the row
names JA_BGM_MAIN_22. The demo of the final fight starts JA_BGM_BOSS_FINAL in its place (0x800cf840). In the first round
of the fight, while DK holds on to the pig of the boss, the states BindWait and BindDamage of the pig request record 136
once a frame. That comes from a reading of the code and from runs of the game in an emulator that patched the faint of
the pig and the mount of DK: the runs called the functions that play reaches, and sent the pig the hits of the sensor of
a player. No run reached that point through play. The rip of `JA_BGM_BOSS_FINAL_dk_wait_attack` writes the sound ID of
the piece over the music of row 0x56 in main memory: `01 00 00 4b` at 0x803b3ed8, in the stage table at 0x803b3978. The
start of the stage then starts JA_BGM_BOSS_FINAL. The driver has no code for this variant. The piece plays in the state
that its start sets. In one run with hooks on the state setters, the fight set no other state of it while DK held the
pig.

At VsGoroPig1 to 4 (0x2e, 0x31, 0x34 and 0x37), the row names JA_BGM_BOSS_GOROPY_A. The demo of the fight starts
JA_BGM_BOSS_GOROPY_B before GoroPig throws its first CocoNut (0x800cb688). A CocoNut that the attack event of DK ends on
plays the attack chance, for 20 steps in the Wii release, a third of a second. The rip of
`JA_BGM_BOSS_GOROPY_B_attack_chance` writes `01 00 00 44` over the music of row 0x2e, at 0x803b3c58. The piece plays in
the state that its start sets. The variant plays the effect for as long as the piece plays.

The pairs come from the objects that each stage places (the `objinfo` files of its `world/*.arc`) and the records that
the class of each object plays. The main.dol of the Wii release names the classes of the states of the objects. A piece
that several stages play gets each effect once, at the first stage by number that places such an object. At the castle
stages, 0x43 and 0x44, `startChordSe` refuses records 134, 135, 138, 140, 141, 143 and 157 to 159 (0x8024c590). The
table of the driver contains 256 variants. The profile lists the variants (`GameProfile::chord_layers`, ntdlib's
`kJungleBeatChordLayers`). Bit 23 of the sound ID of a variant marks it. Bits 16 to 22 contain bits 0 to 6 of its index
in the list, and bit 15 contains bit 7.

Some effects of The Wind Waker make no sound in a rip, and the ripper skips them. Most are empty: their code returns at
once, and the game plays nothing for them either. 30 others read a port that the game sets when it starts the effect.
They play nothing while the port contains 0. A rip contains their variants instead. Each variant plays the effect with
one value of the port:

- **Port 6** contains the low 16 bits of the map information that the game passes with an effect. For a step, a jump, a
  landing or a climb, the game passes the material of the ground in its collision data: 1 to 27. For a hit, it passes
  the material of the surface, or a code from 32 up for the kind of thing hit. These effects read port 6: se2-0003 to
  0005, se2-0051 to 0052, se2-0085, se2-0111, se2-0122 to 0123, se3-0000 to 0003, se3-0010 to 0012, se3-0021, se3-0024,
  se3-0038 to 0041, se3-0043 to 0048 and se5-0301.
- **Port 9** contains the reverb of the room that the effect plays in. The game's `seStart` sets that port for every
  effect. Rips play effects without it. se3-0050, the step of a Stalfos, plays nothing while port 9 contains 0. A rip
  contains a variant for each reverb of the rooms that place a Stalfos: 40, 80, 100, 110, 120 and 127.

The `gcsf_var` tag of a variant contains the value of its port. The title of the variant is the name of the effect, a
space and the value of the tag, such as `se3-0000 6=4`. Its file shares the number of the effect. A user can play
another value by changing the tag. The profile lists the values of each effect (`GameProfile::port_effects`). Values
that play the same samples share one variant. The list contains the lowest of them.

Four more effects pick the key of their note at random: se3-0049, se4-0254, se4-0255 and se4-0327. Each one adds a
random number to a base key: a number below 3 to 48, below 8 to 48, below 8 to 36 and below 4 to 24. The number is a
draw from the generator of the sound library (`JASystem::Player::getRandomS32`), modulo the count. The banks have no
sound for some of those keys. With the sound files of the game, the four effects sound only on keys 48, 48 to 50, 36 to
38 and 24 to 25. The generator starts from the same state at each boot. From that state, the first draw picks a key
without sound for each of the four. In the game, every earlier draw moves the generator on, and each effect sounds on
some of its draws. A rip contains a variant of each effect for each key, in place of the effect. The ripper skips the
variants that make no sound. The title of a variant is the name of the effect, " key " and the key, such as `se4-0254
key 48`. Its file shares the number of the effect. Before the effect starts, the driver draws the numbers that come
before the first draw that picks the key (see "Driver"). The profile lists the keys and their draws
(`GameProfile::random_effects`).

Donkey Kong Jungle Beat has effects like these too. 133 are empty. 68 read port 0xb, and some of them also ports 0xc to
0xf. Most read the key of a note from those ports. They play nothing while port 0xb contains 0. The chord code of the
game writes the ports of the effects that the chord database names. A rip plays those effects through that code (see
"Chord effects"). Port 0xb contains 0 for the others. Two more make no sound in a rip. `gcsfrip` names each effect that
it skips.

Luigi's Mansion plays one piece, `LuigiSings`, by the state of the game. Four of its tracks contain the tune. One of
them plays at a time. Luigi's health picks one of three: above 70 of his 100 points, above 40, or 40 and below. The
fourth plays alone while the lights of the room are on, unless the room is outdoors. Other tracks accompany the tune by
the room: one set in the hallways, one outdoors and one in the other rooms. Rooms 19, 32 and 65 of the room table of the
game have no accompaniment. Luigi's health also sets the tempo: 0.6 + 0.4 × the health / 100. With the lights on, the
time between Luigi's steps scales it, to 0.9 while he stands still. A rip contains the piece once for each state, in
place of the piece: the hallways, the other rooms, outdoors and the rooms without accompaniment, each at 100, 70 and 40
points of health, and a lit room at 100 points while Luigi stands still. A poison mushroom shrinks Luigi for a time. The
game then plays the three tunes of his health an octave higher. A rip contains that state in each place but the lit
room, at 100 points. The title of a state is the name of the piece followed by the place and Luigi's health. The title
of a state in which Luigi is shrunk ends in "shrunk". The title of the lit room names the place alone. While a message
window is open, the game mutes the four tracks of the tune, and the accompaniment plays alone. A rip contains that state
at 100 points in the hallways, the other rooms and outdoors, with "no humming" at the end of its title. The file of each
state shares the number of the piece. ntdlib's `jaudio/luigis_mansion_states.h` lists the states
(`GameProfile::singing_states`).

Four boss pieces of Luigi's Mansion change their tracks as the fight goes on: `BabyBattle`, `BlueBattle`, `TelesaBattle`
and `KuppaBattle`. A function of the game sets the volume of each of the 16 children of the root track to 0 or 1 for a
state, and for three of the pieces the tempo. State 0 is the start of the fight: the game's start of the music applies
it, and the file of the piece plays it. A rip contains each later state as a variant named after the piece and the
number of the state, for example `BabyBattle state 1`. BabyBattle and BlueBattle have states 1 and 2, and TelesaBattle
and KuppaBattle state 1. The driver starts the piece and then calls the game's function that sets the state. The
variants share the number of the piece. The same header lists the states (`GameProfile::boss_states`).

Luigi's Mansion plays one effect as music: `se1-0169`, a piano piece that the mansion plays while Luigi is in the
hallway next to the pianist's room. `--bgm` rips it with the music (`GameProfile::music_effects`).

Mario Kart: Double Dash!! keeps the sequences of its music in the start-up archive. It has no sequence archive. Its
voices play the banks of three more start-up archives in the `baac` block of the first one. The game loads one of those
at a time. The ripper walks the sequence of each effect for the banks that it selects. The ID of a voice contains the
archive with its bank, plus 1, in bits 28 to 31. The library contains the wave archives of the archives that its sounds
load. The ripper skips the effects whose volume in the sound table is 0, and the empty ones.

Animal Crossing keeps no names for its music. Neither do Doubutsu no Mori+ and Doubutsu no Mori e+. The code of each
release contains a table of the numbers that the game uses for its pieces: 256 in Animal Crossing and Doubutsu no Mori+,
and 384 in Doubutsu no Mori e+. Each number names the sequence that the game plays for it. A rip names a piece after its
number, for example `bgm046`. The 24 pieces that play the hours are ripped once for each weather that changes them, for
example `bgm001-fine`, `bgm001-snow` and `bgm001-sakura`. The museum piece is ripped once for each room, for example
`bgm088-lobby`. The island piece in Doubutsu no Mori e+ has two rips: `bgm376-islander` with the islander on the island,
and `bgm376-no-islander` without. Where several numbers play the same sequence, the ripper rips the first of them.
Number 0 isn't ripped: it stops the music. In Animal Crossing and Doubutsu no Mori e+, numbers 83 to 87 play effects.
The rips of those two releases leave the numbers out. Doubutsu no Mori+ plays them as pieces of music. The rips of
Doubutsu no Mori+ include them. Numbers 92 to 111 are the first parts of Kapp'n's songs, and 112 to 121 are their second
parts. A rip of a first part ends with the first part, and each second part has a rip of its own (see "Driver").
Sequences that the table doesn't name aren't ripped as music. They play the effects, the voices and the sounds of some
furniture.

The game keeps no names for its effects either. They play from six banks in the sequence of effects. A rip names an
effect after its bank and its place in the bank, for example `se2-017`. Effect 0 of bank 0 isn't ripped: the game's
slots use the number 0 for no effect. For 17 effects, the game adds a random number from 0 to 3 to the number of the
effect. For five or six others, by release, the game adds 0 or 1. The game then plays the effect of the sum. A rip of
such an effect plays the effect that its number names (see "Driver"). The effects that the sum can name have rips of
their own. The continuous sounds of the game, such as rivers and insects, play in other channels of the sequence. The
ripper rips neither them nor the villagers' voices.

The Pokémon Colosseum Bonus Disc plays its sequenced sound in one program, `tgc/pokedownload.tgc`. A TGC is a disc image
inside a disc. It contains the disc header, the `main.dol`, the file system table and the files of a program. gcsfrip
opens the TGC and rips the program in it as it rips a disc. The profile matches the game ID of the program, POKE01, and
the SHA-1 of its `main.dol`. A TGC starts with the big-endian word 0xAE0F38A2. The word at 0x08 contains the offset of
the disc header of the program. The words at 0x10 and 0x14 contain the offset and the size of the file system table, and
the words at 0x1C and 0x20 those of the `main.dol`. The table lists the offset of each file on the disc of the program.
The offset of a file in the TGC is that offset, minus the word at 0x34, plus the word at 0x24.

The program names its effects in `pm6fgm.h`, a C header next to the effect files. Each `#define` in it contains the name
of an effect and its number: the group and the place in the group. A rip uses those names, for example `SFX_cancel`. The
file of scripts contains four tables. Each is a word with the number of entries, then the entries. The third table lists
the first effect of each group. The fourth lists the script of each effect. The file of scripts of the program contains
five effects in group 0. An effect that the header doesn't name would be named after its group and its place in the
group, for example `se0-004`. The TGC contains two more files of the sound library, `sfxsem.sem` and `sfxmain.ssm`, with
other effects. The program never loads them. The ripper leaves them out.

The sequence archive can contain sequences that no sound of the table plays. The game never plays them. The ripper rips
each one after the sounds of the table, as music, named after its file without the extension. Its `.minigcsf` selects
the first sound of the table that plays a file of the sequence archive. It also contains a `FILE` chunk with a copy of
the start-up archive. The copy replaces the one in the library. In the copy, that sound plays the sequence instead, at
the volume that most of the music in the table has.

The sound table lays out the data of a sound in one of three ways. A type with bit 0x40 set has the later layout
(Twilight Princess). The second byte of the data is the volume of the sound. The 16 bits at +2 name a resource. A sound
of type 0x60 to 0x6f plays the file of the sequence archive with that id. In the earlier layout (Four Swords
Adventures), the third byte of the data is the volume. A sound of type 0x11 plays a file of the sequence archive. The
low 16 bits of the word at +8 of its data contain the id of the file. The entry of an AAF contains the id of the file at
+6 and the volume at +0xc. That applies when bit 0 of the second byte of the table, the music format, is set. The table
of The Wind Waker sets it. Without the bit, a piece of music plays the file whose id is its index, as in Luigi's
Mansion. In The Wind Waker, the table plays every file of the sequence archive. A sequence archive can be compressed
with Yaz0. The one of Twilight Princess is. So can a start-up archive. The one of Donkey Kong Jungle Beat is
(`Donkey.szs`). Luigi's Mansion and Super Mario Sunshine keep their sequences in one file without an archive directory.
A block of the AAF (chunk type 4, `BARC`) lists the name, offset and size of each sequence in that file. The first
layout (Mario Kart: Double Dash!!) came before the other two. The profile of Mario Kart: Double Dash!! selects it. A
sound of type 0x10 plays a sequence of the sequence collection in the start-up archive. Type 0x20 plays the sequence
block of the start-up archive with the ID of the sound. Type 0x30 plays a stream. The ninth byte of the data is the
volume.

The code of Mario Golf lists its songs in two tables: 38 songs and 9 jingles. The code of Mario Power Tennis lists 111
songs and jingles in one table. Each entry names the file of the song. A `.minigcsf` selects a song by its table and
number (see "Driver"). `--bgm` selects every song and jingle. The name of a song is the name of its file without the
extension. The code of both games lists their sound effects too. Mario Golf lists 737: the ranges of IDs of the bank
that the game loads at start-up, 37 voice banks and 51 narrations. Mario Power Tennis lists 924: the range of its
start-up bank, 18 voice banks, 106 narrations in one group and the banks of 10 courts. 37 more groups of Mario Power
Tennis follow each other on the disc. They contain the other 56 effects of the game. The four files of a group are its
pool, its project, its sample directory and its samples. The ripper lists the effects after the songs and jingles. The
name of an effect is the name of its voice bank, `se` for the start-up bank and the other groups, or `court` for the
bank of a court, then a hyphen and its ID in four digits, for example `mario-0154`. The name of a narration is
`narration-` and the ID of its effect. A song reads none of the files of the effect groups. A rip without an effect,
such as a rip of the music alone, contains zeros in place of those files. Each file keeps its size. The file table of
the disc and the places of the other files on the disc then stay the same.

The code of Pikmin contains the index of its sequences: a BARC of 22 entries that lists the name, offset and size of
each sequence in `pikiseq.arc`. The first entry is the effects sequence, and the second plays the system events. Five
entries are empty. The other 15 are pieces of music. A `.minigcsf` selects a sound by its ID: 0x80000000 | group << 24 |
wave system << 16 | part << 8 | piece. The piece is the number of the sequence in the index. The driver loads the group
of the wave system that the ID names. The ID of a piece contains part 0 and group 0 of the wave system with the number
of the piece. The name of a piece is the name of its file without the extension, for example `select`. `--bgm` selects
every piece. One piece plays an empty bank. That piece makes no sound, and the ripper skips it. The cutscene music of
the game is one piece in parts, `demobgm`. The ripper lists the parts that the game plays in place of that piece. The ID
of a part contains the part and the wave group that the game loads for it. The name of a part is the name of the piece,
a hyphen and the number of the part, for example `demobgm-5`. The game plays 13 more pieces, most of them in cutscenes,
from streams (`.stx` files).

The code of Pikmin also contains a catalogue of its sound effects, with their names. A row records how the game starts
the effect: as an effect of the system, as an effect of the player, or as an action of an event of a given type. The ID
of an effect is 0x80000000 | kind << 24 | number << 8 | sequence. The kind is 1 for an effect of the system, 2 for one
of the player, 3 for the action of an event and 4 for the sound of the formation. The number is the number of the
effect, or the type of the event << 12 | the action. The sequence is 0, the effects sequence, or 1, the events sequence.
The catalogue names an effect, for example `SE_FLOG_JUMP`. Where the actions of several events play the same effect, the
ripper lists it once, with the name of the first row. The game code plays a few effects that the catalogue doesn't list:
effects of the system in the demo of a found ship part, a voice of the player and the sound of the formation. Their
names are their IDs in hex. A rip of the formation contains one variant, `84000000 3=127`: a full move straight ahead (x
0 and y 127, see "Driver"). The ripper leaves out four effects of the system. One stops the effects, one plays nothing,
one plays a part of `demobgm`, and one plays a stream. An event of the ship type starts the idling of the ship as the
game creates it. A rip plays the actions of the ship on an event of another type. That event reaches the same entries in
the table of actions.

The library of a Pikmin rip contains zeros in place of the wave archives that none of its sounds loads. At start-up, the
sound library loads group 0 of wave system 0, the first group of the effects. The driver loads the group that the ID of
a piece names. For an effect, the driver loads groups 1 to 3 of wave system 0 and group 0 of wave systems 14, 15 and 2.
When the game sets up its first scene of play, it loads the same groups. The groups that no sound of the rip loads keep
their sizes, but contain zeros. The file table of the disc and the places of the other files on the disc then stay the
same.

The code of WarioWare, Inc.: Mega Party Game$! names its music in the table of its Sound Room: 231 pieces. A
`.minigcsf` selects a piece by its MusyX song ID (see "Driver"). Each piece keeps the name that the Sound Room gives
it. `--bgm` selects every piece. Two of the pieces only count beats for the game. They have no notes, and the ripper
skips them. The game plays most of its sound effects as MusyX songs too. Its play function (0x800348B8) plays a song
for a number below 1606. A table of 933 songs that play as effects (0x801B4918) contains 8 bytes for each: the MusyX
song group in the first byte, the number of the song in a big-endian half at +2 and its volume at +4. A number from
1606 on starts a MusyX effect: the effect whose ID is the number less 1606. The table at 0x801B6640 contains 6 bytes
for each of the 145 effects that the function can start: the number for the function in a big-endian half, then the
volume at +2, the send to the reverb at +4 and the send to the chorus at +5. The function starts the effect through
sndFXStartEx at that volume, with the pan of the row of the effect in its group, and studio 0. It then sets MIDI
controllers 91 and 93 to the sends. The ripper lists the songs of the first table that the Sound Room leaves out, then
the effects of the second, in the order of the tables. Each one is named after its number for the play function, for
example `se-0013`. For WarioWare, the `.gcsflib` contains the header of the disc and the five sound files of the game
as `FILE` chunks: the MusyX project, pool, sample directory and samples, and one file with every song.

The sound test of Mario Party 4 lists 76 songs and 14 streams by name. The ripper lists them in the same order. A
`.minigcsf` selects a song by its group set and its number (see "Driver"). Each song keeps the name that the sound test
gives it. The names in the Japanese release use the Japanese font of the game. The font draws the voiced and semi-voiced
marks at 0x80 and 0x81, hiragana from 0x91 and katakana from 0xD1. A mark follows the kana that it marks, and the ripper
joins the two. `--bgm` selects every song. The streams aren't ripped. The table of the sound manager contains two more
songs. Song 0 is a dummy. Song 42 is song 41 in another group. The ripper leaves both out. For Mario Party 4, the
`.gcsflib` contains the header of the disc and two sound files as `FILE` chunks: the file of sound data of the sound
manager, and the stream file up to the samples of the streams. The sound manager reads that part at start-up.

The sound effects and voices of the game follow the songs. The file of the sound manager contains a table of 2,308
effects at the offset that its header stores at 0x30, 16 bytes for each. An entry contains the MusyX IDs of the group
and of the effect in big-endian halves. The volume, the pan, the pitch bend in a big-endian half, the surround pan, the
sends to the reverb and the chorus, a flag for an emitter and the compression of the emitter follow. An entry whose
effect ID is 0xFFFF is empty. The ripper leaves it out. The table of groups is at the offset that the header stores at
0x20, 32 bytes for each group. The entry of a group starts with its MusyX ID. The group of an effect is the first group
from 1 on with the group ID of the effect. An effect is named after its number in the table, for example `se-0042`. The
list of character voices in the Option Room names some of them. The list follows the entries of the sound test in
`dll/option.rel`: 123 entries of the same form, at 0xB518. The ripper adds such a name, for example `se-0054 Toad
Happy`. The game plays an effect through msmSePlay. It loads the group of an effect with the group set of a scene, or
alone through its function at 0x800336D4. A rip loads the group of the effect alone, unless it's one of the base groups.
The rip then starts the effect without parameters: with no position, at the volume, the pan and the sends of its entry.

The start-up archive of Pikmin 2 is an AAF too. Its sound table lists the effects sequence and 24 other sequences. The
sound layer of the game starts each piece of music by its file. The ripper leaves out the sequences of the table. It
rips every file of the sequence archive but the effects sequence, `se.bms`: 48 pieces. A piece is named after its file
without the extension. The game never plays 14 of them. One of those, `result`, makes no sound, and the ripper skips it.
`--bgm` selects every piece. The game picks a conductor for each floor of a cave. The conductor picks the phrases that
the tracks of the cave music play, at random. A rip plays the piece of a cave with the first conductor of its family,
such as `caveconc_00_0.cnd` for `caveconc`. The game code names that conductor itself, for the floor of a boss. The
piece of a cave never repeats. It plays to the ripper's limit of 600 seconds, then fades (see "Lengths").

The cave floor lists of the game name 11 more conductors. Each gets a variant of its piece, with the wave group that the
game code assigns to the name of the conductor. A variant is named after its conductor without the extension, such as
`caveconc_05_0`. The music of a boss has parts. The actions of the boss jump between them. A rip of the piece starts at
the part at which the boss starts the music. Each other part gets a variant that starts there and plays what follows.
Such a variant is named after the piece and the part, such as `m_boss-3`: parts 0 to 8 and 10 to 12 of `m_boss`, and
parts 2 to 13 of `l_boss`. At part 0 of `l_boss`, a track jumps to itself without end. That part gets no variant. When
the music jumps into an attack, the code of the game for the music of a boss writes to the object of the boss. The scene
of the driver has no boss. The driver gives the music of a boss a stand-in, a zeroed object. The attacks then play.

The game plays `l_boss` only while the sound object of the final boss exists. The driver sets the variable that contains
that object to the same stand-in before it makes the piece. The game then plays tracks 10 and 11 in full. Its directors
mute tracks 14 and 15 until the fight turns them on, and the rips keep them muted. Track 15 contains notes only in parts
4 and 11. It plays while a captain stands within 300 units of the boss. Those two parts get another variant each, with
the track on: `l_boss-4-near` and `l_boss-11-near`. In all, the pieces have 37 variants. `--bgm` selects them too.

The sound table of Pikmin 2 lists 1,038 effects in categories 0 to 5. Their IDs follow the rules of The Wind Waker. The
game names its sounds in `AudioRes/PSound.asn`: a 16-byte header, then 32 bytes for each name, with the name in up to 28
bytes and then a word. The first 18 name the categories of the sound table. Each one after them names a sound. Its word
is the ID of the sound. A rip of an effect uses that name, for example `PSSE_SY_MENU_CURSOR`. The ripper lists the
pieces first, then the effects. One effect makes no sound, and the ripper skips it: the code of `PSSE_PL_DEMOE00_POD02`
returns at once. Three more play only some of the times that the game starts them: `PSSE_EN_FROG_VOICE`,
`PSSE_EN_OOPAN_BOUND2` and `PSSE_EN_OOPAN_VOICE_DOWN`. Each one draws a random number and ends at once on some draws.
The generator of the sound library (`JASPlayer::getRandomS32`) starts from the same state at each boot. From that state,
the first draw ends all three. Before a rip of one of them starts, the driver draws the numbers that come before the
first draw on which it plays (see "Driver"): 3 for `PSSE_EN_FROG_VOICE`, and 1 for each of the other two. Each of the
three plays the same sound on every draw that doesn't end it. The profile lists the counts
(`GameProfile::random_starts`).

The start-up archive of Pac-Man Vs. is an AAF too. Its sound table lists the effects sequence and 76 effects in
categories 0 to 2. The IDs of the effects follow the rules of The Wind Waker. The game names none of its sounds. A rip
of an effect is named after its category and index. 17 effects make no sound. The ripper skips them. The code of 5
returns at once: se0-0000, se1-0007, se1-0008, se1-0012 and se1-0013. 11 narration effects play a note that has no
sample in the instrument: se2-0000, se2-0009, se2-0015, se2-0017, se2-0028 to 0030 and se2-0035 to 0038. The sound table
sets a volume of 0 for se2-0020.

The menu of Collector's Edition has a BAA with the later layout. Its sound table lists 23 effects, one piece of music
and one stream. A rip keeps the names of the table, such as `JA_SE_TR_OC_CURSOR_L`. The sequence collection of the
start-up archive (`bsc`) contains the sequences of the effects. The sequence archive is compressed with Yaz0. It
contains one file, `zeldamain.bms`. The piece, `JA_BGM_DUMMY`, plays it. The code of the menu never starts the piece.
The other programs on the disc are out of scope: the four games that the disc emulates, and a demo of The Wind Waker.

Final Fantasy Crystal Chronicles keeps each piece of music in a file of its own in `dvd/sound/Music/`. The name of the
file is `music` and the number of the piece in three digits, for example `music100.bgm`. The game's disc library matches
names in any case. The ripper lists those files: 60 pieces. The name of a piece is the name of its file without the
extension, for example `music100`. A `.minigcsf` selects a piece by its number (see "Driver"). `--bgm` selects every
piece. The file of a piece starts with `BGM `. It contains the number of the piece and the number of its wave bank in
big-endian halves at 4 and 6. The bank is a file in `dvd/sound/Wave/`. Its name is `wave` and the number of the bank in
four digits, for example `wave0100.wd`. The pieces name 44 banks. One piece makes no sound. Its only note plays at a
volume of 0. Its other tracks stop at once. The ripper skips it. The game streams 21 more pieces, from
`dvd/sound/Stream/`. GCSF doesn't rip streams.

The effects of the game are in `dvd/sound/SE/`. The game plays an effect by its number. It reads a number below 4000 as
an effect in one of four blocks of effects, numbered 0 to 3. A block is a file in `Block/`. Its name is `se` and the
number of the block in three digits, with the extension `.seb`. The file of a block starts with `SeBlock`. A big-endian
half at 10 contains its number of places. A table of big-endian words at 16 lists the effect of each place, or -1 for
none. The number of the block times 1000 plus the place of the effect is the number of the effect. A rip names such an
effect after the file of the block and the place, for example `se000-001`. An effect from 4000 on is a SEP file in
`Sep/`. Its name is `se` and the number of the effect in six digits, for example `se010001.sep`. A rip names the effect
after its file, for example `se010001`. A SEP file starts with `SeSep`. The number of the wave bank of the effect is a
little-endian half at 0x11. An effect of a block names its bank the same way, at the second byte of its information. The
effects of the blocks use banks 0 and 500. The game loads those two banks with the blocks, into the first two bank slots
of Red. Command 0x21 of a track picks a wave from a slot. Effects of SEP files use the command too. The game can't play
the five SEP files numbered below 4000. The ripper leaves them out. It also leaves out a fifth block, `se012.seb`. The
game never loads it. The ripper lists 261 effects in blocks and 2,137 in SEP files. The library contains the four
blocks, the SEP files from 4000 on and the 113 banks that the effects name. The ripper skips 262 effects that make no
sound. 258 of them start no note. The other four play their notes on waves that their bank leaves empty.

The library of a rip contains zeros in place of the files that none of its sounds reads. A piece reads its file and its
bank. An effect reads the four blocks and banks 0 and 500. An effect of a SEP file also reads that file and its bank. A
rip of the music alone therefore contains zeros in place of the blocks, the SEP files and the banks that only the
effects name. Each file keeps its size. The file table of the disc and the places of the other files on the disc then
stay the same.

Metroid Prime streams most of its music. Four songs are sequenced. `MidiData.pak` contains them as CSNG resources. The
packs of the game are in the format of Retro Studios, version 3.5: a list of the named resources, then a table of every
resource with its type, ID, size and offset. A resource can be compressed with zlib. A CSNG resource starts with five
big-endian words: 2, the MusyX song ID, the group of the song, the ID of the audio group set (AGSC) that contains the
group, and the size of the file of the song. The file of the song follows. `AudioGrp.pak` contains the 72 audio group
sets of the game. An audio group set starts with two strings: a folder and the name of the set. The four MusyX files
follow, each after a word that contains its size: the pool, the project, the samples and the sample directory. The
ripper lists the four songs in the order of the pack. A song is named as the pack names its resource, for example
`Main_Plaza`. `--bgm` selects every song. The four songs use group 53, in the audio group set named `test`. Each song
contains a few long notes.

The sound effects of the game follow the songs, in the order of their IDs. The project of a set lists its effects in a
table for each effects group. The row of an effect has 10 bytes: its ID in a big-endian half, the ID of its macro in
another, then a byte each for the most voices that may play the effect at once, its priority, volume, pan, key and
volume group. MusyX never starts an effect whose row allows no voices. The ripper leaves out the one such effect. A rip
names an effect after its audio group set and its ID in four decimal digits, for example `Scarab-0721`. The driver
starts an effect with a volume and a pan of 0xFF. MusyX then reads them from the row of the effect. In the game, the
place of an effect in the world can change its volume and pan. A rip plays every effect on studio 0. Of the 3,166
effects, 1,023 make no sound. The ripper skips them. 932 start a sample and end their macro in the same step. MusyX
frees the voice before the sample starts. 86 scale their volume to 0. Four make a random choice that can skip their
sample. In a rip, the choice always skips it. One plays its sample at a volume of 0.

Metroid Prime 2: Echoes streams its music. Its packs have the same format as the packs of Metroid Prime. `MidiData.pak`
contains the four songs of Metroid Prime. No pack contains the audio group set that they name. The ripper leaves them
out. The 20 packs of the game contain 690 audio group sets. Each set but one contains one group of effects. The other
contains no group. A set may be in more than one pack. An audio group set starts with a big-endian word of 1, the name
of the set, and the ID of the group in a big-endian half. Four big-endian words contain the sizes of the four MusyX
files. The files follow: the pool, the project, the sample directory and the samples. None of the audio resources that
the ripper reads is compressed. The string table `audio_name_lookup_STLC` in `AudioGrp.pak` names the effects. It starts
with a big-endian word that contains the number of names. The names follow. Each ends in a 0 byte. The place of a name
in the table is the ID of its effect. A rip names an effect as the table does, for example
`SFXfnt_x_titlemusic_lp_00L_looped`. An effect without a name would be named after its set and its ID, as in Metroid
Prime. The game adds ARAM for the samples of each audio group set that it loads. A rip gives MusyX 0x400000 bytes of
ARAM from the start instead. The ripper leaves out the 15 effects that allow no voices. Of the other 3,461, 160 make no
sound. The ripper skips them. 147 start no sample. 10 scale their volume to 0. Of the other three, one makes a random
choice and one tests a variable of the MusyX macros. The third plays too quietly to count as sound.

Paper Mario: The Thousand-Year Door streams its music. `sound/proj/pmario.proj`, `.pool`, `.sdir` and `.samp` are the
four MusyX files. They contain the 12 groups of the game. Six are effects groups, with 1,376 effects. The other six are
song groups. The game plays their 143 songs as sounds. The game pushes all 12 groups at start-up and keeps them. A rip
pushes only the sound's group. The game code lists its sounds in a table at 0x80316B28: 4,242 rows of 20 bytes. A row
starts with the address of the name of the sound. The next word contains the kind of the sound in its top bits and its
ID in its low 16 bits: 0x20000000 for an effect of MusyX, 0x40000000 for a song and 0x80000000 for a stream. A row of
zeros is unused. The bytes at 0xC, 0xD and 0xF contain the volume, the pan and the send to the reverb of an effect. The
big-endian half at 0x12 bends its pitch. The game starts an effect through sndFXStartEx with a volume of 0x7F and the
pan of the row. It then sets MIDI controller 7 to the volume of the row, the pitch wheel to 0x2000 plus the bend, and
controller 91 to the send. Controller 91 feeds aux A, the 3-second high-quality reverb of MusyX. Aux B contains the
MusyX chorus. The game adds the reverb of the room to a send above 0. A rip plays no room. Most sounds of the game play
at a place in the world. Their volume and pan then follow the listener. A rip plays each effect as its row sets it.
Several rows can play one effect. The ripper uses the first row with a volume above 0, or the first row if none has one.
A rip names the effect as that row does, for example `SFX_PRESS_START1`. The table plays 1,304 of the effects. The other
72 are named after the set and their ID, for example `pmario-0051`. They play at the MusyX defaults. Every row of two
effects sets a volume of 0. They make no sound. The ripper skips them.

The game reads its songs from `sound/proj/pmario.slib` at start-up. The file contains them one after another. The record
of a song starts with its size in a big-endian word, its 32 bytes of header included. The song follows the header. A
size of 0xFFFFFFFF ends the file. The row of a song contains the group of the song in bits 16 to 23 of its second word,
the MusyX song ID in bits 8 to 15, and the record of the song in bits 0 to 7, counted from 0. The game plays the song
through sndSeqPlayEx with the group, the ID and the song of the record. It then sets the volume of the song to the
volume of the row through sndSeqVolume. The pan, pitch bend and send of the row play no part. 168 rows play 139 of the
songs. For a song too, the ripper uses the first row with a volume above 0. A rip names the song as that row does, for
example `SFX_SE2_EVT_STARSTONE_GET1`. `pmario.stbl` names the songs: 32 bytes for each record, with the name of the
song, ended by a byte 0, then its group at 0x1E and its ID at 0x1F. The other four songs get their names, groups and IDs
from there, for example `SE1_TEST`. They play at the MusyX default volume. The ripper counts the songs as music. `--bgm`
rips them alone. A rip of the game contains 1,374 effects and 143 songs.

The code of Tales of Symphonia lists its music in a table at 0x801F9830: 8 bytes for each song. An entry contains the
address of the file name of the song, then the MusyX song ID and the kind of the song, a big-endian half each. A song ID
of 0xFFFF ends the table. The song files are in `S/`, for example `S/bgm_t000.song`. The code names them in lower case,
and the game's disc library matches names in any case. The file of a song is a MusyX song (an SNG) without a header. The
music group, group 0, is in `S/inst.snd`. That file starts with the number of its parts, 4, then the offset and size of
each part, a big-endian word each. The parts are the MusyX project, pool, sample directory and samples. The ripper lists
the 111 songs of the table in its order. A song is named after its file without the extension, for example `bgm_t000`.
`--bgm` selects every song. The game plays a song at the music volume of its options, 127 with the default options. The
play function sets the reverb of aux A for each song (see the profile in `src/rip/profiles_musyx.cpp`). The catalogue
entry of a song contains those settings. The disc contains six songs that the table leaves out: `bgm_etc004` and `temp1`
to `temp5`. The game code names none of them, and the ripper leaves them out. Both discs contain the same code and the
same music. A rip of either disc makes the same set.

The sound effects and voices of the game follow the songs. Each of the other `.snd` files in `S/` contains one group of
effects: `se.snd` the common effects of the game, `se_ev00.snd` to `se_ev07.snd` the effects of events, and
`v_01_llo.snd` to `v_70_bh18.snd` the voices in battle. The IDs of the effects rise from file to file in the order of
the file names. The ripper lists the effects in that order. The game code names the first nine of those files. The code
also names nine voice files of the party in `BTL/`. They contain the same bytes as the first nine voice files in `S/`.
Another file in `BTL/` contains the samples of the other voice files, in the order of the files. The ripper reads every
group from `S/`. One voice file contains no pool and only 256 bytes of samples. MusyX can't push the group of that file.
A rip leaves out its effects. The start-up of the game (0x800708C8) pushes the group of the music, then the group of the
common effects. The game keeps both. The common effects use some samples and tables of the music group. The effects of
events use samples, macros and tables of the group of the common effects. A rip of an effect therefore pushes those two
groups first, then the group of the effect. The game starts an effect through sndFXStartEx with a volume of 127, a pan
of 64 and studio 0. Every row in the effects tables of the groups sets the same volume and pan. The game can later set
the pan of an effect from the place of the source of the sound. A rip plays every effect in the centre. Nothing names
the effects. A rip names an effect after its file and its ID in four decimal digits, for example `se-0012` or
`v_01_llo-0501`.

The code of Star Fox Adventures lists its music in a table at 0x802C5E80: 99 entries of 16 bytes. An entry contains the
MusyX song ID in a big-endian half and the group of the song in the next byte. The address of the name of the song
follows at +4. `audio/midi.wad` contains the songs by ID: their sizes in 100 big-endian words at its start, then the
songs from 0x1A0, each padded to 32 bytes. The music groups are in two sets of MusyX files in `audio/`: `starfoxm` and
`starfoxs`, each as `.pro` (the project), `.poo` (the pool), `.sdi` (the sample directory) and `.sam` (the samples).
Most songs use group 0 of `starfoxm`. Songs 0x4A to 0x63 use groups 18 to 54 of `starfoxs`. At start-up the game pushes
group 0 of `starfoxm`, then groups 1 to 55 of `starfoxs`, and keeps them all. A rip of a song pushes the same groups in
the same order. The ripper lists the 99 songs by ID. A song is named as the table names it, for example `starfox_map`.
`--bgm` selects every song. The game starts its music through the triggers in `audio/data/Music.bin`, 16 bytes each. A
trigger contains its number and the ID of the song in big-endian halves at its start, and the volume of the song in its
byte at 0xC. A song plays at the volume that most of its triggers set. Of volumes that tie, the first one in the file
wins. Trigger 0xEC plays a song that its caller picks. A song that no other trigger names plays at the volume of that
trigger, 80. The game starts a song at volume 0 and raises it to the volume of the trigger over 0.5 s. A rip does the
same. `midi.wad` contains a 100th song, 0x33. A trigger names it, but the table doesn't, and the game never plays it.
The ripper leaves it out.

The sound effects of the game follow the songs, in the order of their IDs: every effect of the effects groups that the
game pushes. All of them are in `starfoxs`. A rip of an effect pushes the same groups as a rip of a song. The game
starts its effects through the rows of `audio/data/Sfx.bin`, 32 bytes each. A row contains the volume of the effect in
its byte at 2. Six effect IDs follow from 0xA, in big-endian halves. The top four bits of the last byte of the row
contain how many of them the row uses. The game picks one of those at random. An ID of 0 picks no effect. The game
passes the volume of the row, a pan of 0x40 and studio 0 to sndFXStartEx. An effect plays at the volume that most of its
rows with a volume above 0 set. Of volumes that tie, the first one in the file wins. An effect whose rows all set 0
plays at 0 and makes no sound. The ripper skips it. An effect that no row names plays at the volume and the pan of its
row in the effects table of its group. In some places of the game world, the game sends its effects to the reverb of aux
A through controller 0x5B. A rip plays the effects without that send. Everywhere else, the game does the same. The game
code names none of its effects. `audio/starfox.h.bak` on the disc names the effects of an earlier build. The IDs in that
header don't match the project of the game. A rip names an effect after its set and its ID in four decimal digits, for
example `starfoxs-0420`.

Star Wars Rogue Squadron II: Rogue Leader packs most of its files in `Rogue2/data.dat`. `Rogue2/data.dir` lists them in
lines of text: the path of a file, padded with spaces to 64 characters, then its offset in the pack, its size and two
more numbers, split by tabs. The sound of the game is in `audio/data`. The words of `audio/data` are big-endian. An
offset in `audio/data` counts from the start of the file. The word at 4 contains the offset of the table of banks. The
table contains the number of banks, 39, then the offset of the entry of each bank. An entry contains the offsets and
sizes of the four MusyX files: the project, the pool, the sample directory and the samples. Next come the offset and
size of a block before the songs of the bank, the number of the songs, the offset of their table and the offset of the
group list of the bank. The row of a song in the table contains its offset and size, then its group and its ID in
big-endian halves. The music hall of the game lists 114 songs at 0x80060C38, in entries of 16 bytes. An entry contains
the MusyX song ID and group in big-endian halves, then the address of the name of the song in UTF-16. The byte at 0xA of
an entry contains the volume at which the hall plays the song. The banks contain 10 more songs. The ripper lists the
songs of the hall in its order, then the others by bank. A song from the hall gets the name that the hall gives it, for
example `Dogfight_Action`. Another song is named after its group and ID, for example `Song_0x45-0x0A`. `--bgm` selects
every song. Most songs play samples of bank 20, the bank of the first song of the hall. A rip of a song pushes the
groups of bank 20, then those of the bank of the song, each in the order of the bank. A song plays at the volume that
the hall sets. The hall fades a song in over 300 ms. A rip leaves the fade-in out. The menus of the game start their
songs through 0x7FD4CE78 in the game, with the group of a song, its ID and a volume. They play `Song_0x6E-0x65` at
volume 0x67 in two of their three calls. The hall doesn't list that song. A rip plays it at that volume. No call in the
game code sets a volume for the other songs that the hall doesn't list. The scripts of the levels probably set them. A
rip plays them at the full volume of MusyX.

Banks 0 to 19 contain the sound effects of the game. The ripper lists them after the songs, bank by bank. The ID of each
effect belongs to one bank. Some effects of banks 1 to 19 play samples of bank 0. Bank 0 shares no ID of a macro, a
table or a sample with those banks. A rip of an effect pushes the groups of bank 0, then those of the bank of the
effect, each in the order of the bank. The sound table of the game, at 0x7FDDBF30 in the game, contains 522 rows of 12
bytes. A row contains the ID of an effect in a big-endian half and its volume in the byte at 3 of the row. An ID of
0xFFFF marks an empty row. Several rows can name one effect, at different volumes. To play a row, the game calls
sndFXStartEx with the volume of the row, a pan from the caller and studio 0. Three functions scale the volumes of rows
489 to 491 by a setting. Another starts the effect of a row with values for two controllers. A rip leaves both out. A
rip plays an effect at the volume that most of its rows set, in the centre. Of volumes that tie, the first row wins. An
effect that no row names plays at the volume and the pan of its row in the effects table of its group. A rip names an
effect after its ID in four decimal digits, for example `RogueLeader-0048`.

`main.dol` is a loader. It contains the OS, MusyX and a second DOL: the game. The loader maps the game in at 0x7FC00000.
The driver runs only the code of the loader. A rip skips the set-up of the heaps of the game by the loader, and the
static constructors of the game. The profile in `src/rip/profiles_musyx.cpp` lists the other patches of the rip.

Sixty songs of Rogue Leader play only streams. Each of them contains a single note. The program of the note plays
stream buffers: samples of one second that loop. A stream buffer has the sample type 5 in the sample directory of its
bank. The game probably fills the buffers from the disc as the song plays. The numbers of the `Rogue2/dg000NN.vsp`
files on the disc match those of the banks that contain the songs with streams, 21 to 28 and 30 to 37, and that of the
music bank, 20. GCSF doesn't rip streams. These songs make no sound in a rip, and the ripper skips them. Eight more
songs play a stream along with their notes: The Battle Of Yavin I and III, Hyperspace, Imperial Attack, The Asteroid
Field, Celebration & End Titles, and two songs that the hall doesn't list (song 0x1A of group 0x4D and song 0x3D of
group 0x61). A rip of one of them would play only its notes. The ripper leaves them out, as streams. The profile in
`src/rip/profiles_musyx.cpp` lists them.

Pokémon Colosseum keeps most of its files in FSYS archives (see ntdlib's `disc/fsys.h`). The module `common_rel` in
`common.fsys` contains the sound table of the game: 1,236 entries of 12 bytes. The first byte of an entry has bit 7 set
for an effect. The entry of a song contains its group at +1, the preset of its reverb at +2, the number of its file at
+3 and the MusyX song ID in a big-endian half at +4. The table of 79 files in the module lists the FSYS ID of each file.
`bgm_archive.fsys` contains the songs under those IDs and names them. The music groups are in `common.fsys`: group 0 in
`snd_music_proj`, `snd_music_pool` and `snd_music_sdir`, and group 1 in the same files of `snd_music_atmos`. Their
samples are `sound/snd_music.samp` and `sound/snd_music_atmos.samp`. The profile in `src/rip/profiles_musyx.cpp` lists
the places of the tables in the module. The ripper lists the 78 songs of the table in its order. A song is named after
its file, for example `title_song`. `--bgm` selects every song. `null_bgm_song` contains no notes. The game plays it to
silence its music. The ripper skips it. The game plays its songs at volume 127. Aux A of studio 0 contains the
high-quality reverb of MusyX. The module contains 12 presets of the reverb settings. The entry of a song picks one. The
catalogue entry of a song contains the settings of the preset. Preset 0 leaves the reverb as it is. Only `null_bgm_song`
picks it.

The effects of Pokémon Colosseum follow the songs, in the order of the table. The entry of an effect contains its group
at +1 and the MusyX ID of the effect in the big-endian half at +4. The effect groups are in `common.fsys` too, one for
each of the sets `snd_se`, `snd_se_battle`, `snd_se_motion`, `snd_se_nakigoe` and `snd_se_nakigoe2`. Their samples are
in `sound/`. An effect is named after its set and its ID, for example `snd_se_battle-0202`. An effect whose row in the
project of its group allows no voices is left out. MusyX never starts it. The game's play function (0x80166AB8) starts
an effect through sndFXStartEx at volume 0x7F, pan 0x40 and studio 0. It then sets the MIDI volume of the effect to
0x7F. MusyX starts an effect at that MIDI volume. A rip starts the effect in the same way and leaves out the controller.
The set-up of the game calls sndVolume(100, 0, SND_FX_VOLGROUPS). MusyX puts every effect in volume group 31. That call
sets the volume of the group to 100/127. The catalogue entry of an effect writes the volume of the group into the MusyX
fader. A rip of an effect uses the reverb of preset 0.

Resident Evil keeps its music in `bgm/`, in the files `sg00.snd` to `sg7f.snd`. The code of the game builds the name of
a file from the number of the song in hexadecimal (0x80061BF4). A file contains one song, the one group that the song
plays and the samples of the group. The file starts with 16 big-endian words: the offsets and the sizes of the pool, the
project, the sample directory, a table of effects, the song and the samples, then the ID of the group and the ID of the
song. A rip pushes the group and starts the song through sndSeqPlayEx. The game does the same (0x80062364). Five files
repeat the song and the samples of an earlier file. The ripper leaves them out. Of the other 123, 16 make no sound: 14
blank songs, whose one note plays at a channel level of 0, sg4b without notes, and sg7a. The seven channels of sg7a
contain nine notes and start at level 0. The code of a room sets the levels and the pans of the channels of a song
through 0x80061E6C, in masks. That function sends command 55 to the driver of the game for each channel. The command
calls sndSeqCntrl with CC7 and CC10. Six rips play the mixes that rooms set for two ambience songs. sg74 plays its three
channels at a level of 80. The rips `sg74 ch0`, `sg74 ch1` and `sg74 ch2` play one of them alone. Most of the rooms that
use the song do that. `sg7a ch0-3+5` plays channels 0 to 3 and 5 of sg7a at 80, `sg7a ch4` plays channel 4 alone and
`sg7a ch5` plays channel 5 alone, each at a pan of 64. Other rooms set other levels for some songs, or levels from the
state of the game. A rip at the levels of the file makes no sound for sg7a, and the ripper leaves it out.

The sound library of the game is an early build of MusyX under Capcom's sound driver. The driver receives commands
through 0x800671B8. The set-up (0x80063990) gives MusyX 48 voices, 32 for music and 32 for effects, one studio and 8 MiB
of ARAM. Aux A gets the high-quality reverb of MusyX at 4.2 s, and aux B the standard reverb at 0.9 s. The music option
of the game starts at 100 and calls sndMasterVolume(110) for music (0x800620B4). Effects play from cue tables in the
files of `se/`. A table sets the level and sends of each effect. No rip contains them. The table of a room names up to
four songs that play at once (0x80061B1C). The code of a room or an event changes the channel levels of a song through
0x80061E6C. A rip plays each song alone, at the levels of its file. Logged Dolphin runs of the game confirm the values
of the set-up: the arguments of sndInit, the output mode, the reverbs, sndVolume(100) and sndMasterVolume(110). A run
that started two songs through 0x80062364 confirms the start: sndSeqPlayEx with no parameters, on studio 0, and no
change of volume or levels. The rest of this paragraph comes from a reading of the game code.

Resident Evil Zero comes on two discs. Both contain the same `main.dol`. The game code lists its songs in a table at
0x80243BCC: 12 bytes for each song. An entry contains the MusyX song ID and the group of the song in big-endian halves,
then the addresses of two names: the archive that contains the group, and the file of the song. A song ID of 0xFFFF ends
the table. The files are in `bio0/sound/`. The file of a song has the extension `.son`, for example `bgm_00.son`. It
contains a MusyX song (an SNG) without a header. An archive (`.arc`) starts with a word 0, the number of its parts and
the size of its header. Each part then has 0x20 bytes: its offset and size, its name in 8 bytes and its type in 16. The
parts are the MusyX project (`proj`), pool (`pool`) and sample directory (`sdir`). A file with the name of the archive
and the extension `.sam` contains the samples. Every word is big-endian. The game's disc library matches names in any
case. The table names `bgm_0c`, for example, and the first disc contains `bgm_0C.son`.

The table lists 230 songs. Each disc contains the songs of its part of the game in `bio0/sound/`: 79 on the first disc
and 129 on the second. 59 of them are on both discs. That makes 149 songs in all. Sixteen more songs are the music of
cutscenes. Each disc contains eight of them. Neither disc contains the other 65. The files of a cutscene are in
`bio0/demo/`. The game loads the archive of a cutscene from a compressed file, for example `dd00.alz`. The bits of the
compressed file come from the lowest bit of each byte up. Its header contains a mode in 8 bits and the size of the
archive in 32. Mode 0 stores the archive. In modes 1 and 2, a 1 bit starts a literal byte in the next 8 bits, and a 0
bit starts a copy of earlier bytes. A copy contains its distance less 1, then its length. Mode 1 stores the distance in
10 bits. Mode 2 stores it as a class and a value. A class is the number of 0 bits before a 1 bit. Classes 0 to 3 store
the value in 2, 4, 6 and 10 bits. The length is a class and a value in both modes. Part 7 of the archive is an archive
too. Its parts contain the music of the cutscene: the MusyX pool, project and sample directory, then the song. Part 1 of
the samples of the cutscene, for example `dd00sam.arc`, contains the samples of the music. The ID of the first group of
the project is the group in the row of the song in the table. Of the cutscenes that contain the group, a rip uses the
files of the first by name. Two cutscenes of the second disc share `bgm_73`, and three share `bgm_7c`. Their files are
the same. Part 6 of the archive of a cutscene contains its sound effects, if it has any. 18 cutscenes have them: 13 on
the first disc and 5 on the second. The part is an archive of the MusyX pool, project and sample directory of one group
of songs, then an archive of 2 to 17 songs. Part 0 of the samples of the cutscene contains their samples. The game plays
these effects as songs. 0x8016C06C plays song N of the archive through 0x8014AAB8, with the cutscene fade-in of 133 ms.
A table in the game code at 0x80258BE4 contains 4 bytes for each cutscene: the ID of the effect group and the song ID of
the first song, in big-endian halves. Song N has the ID that follows the first by N. The song list of the project
contains the same IDs in the same order. A group of 0 means that the cutscene has no such effects.

The ripper lists the songs that the disc contains, in the order of the table. The library of a rip is named after the
number of the disc: `Resident_Evil_0_Disc_1.gcsflib` or `Resident_Evil_0_Disc_2.gcsflib`. Rips of both discs can then
share a folder. A song is named after its file without the extension, for example `bgm_00`. `--bgm` selects every song.
59 songs are music. Their names start with `bgm_`. The other 106 are the ambiences of the areas of the game. Their names
start with `train_`, `inst_`, `grou_`, `chur_`, `fact_` or `plan_`. Every note of `bgm_4b_3` has a velocity of 1. It
makes no sound, and the ripper skips it. The cutscene songs end. The others loop. Some cutscene songs start with a rest
that times the music to the scene. A rip plays the rest. `bgm_78` is silent for its first 52.6 s. The game pushes the
group of a song and plays the song through sndSeqPlayEx. A room or a cutscene starts a song at the volume of one of the
sound options of the game. Each option starts at 127. A rip plays a song at 127. A room starts its song at that volume.
A cutscene starts its song at a volume of 0 and raises it to that volume over 133 ms. A rip of a cutscene song does the
same. No room plays `bgm_52`. The extras of the game play it at 100/127 of the music option, and a rip plays it at 100.
At start-up the game sets the high-quality reverb of MusyX on aux A and aux B. The reverb of aux A lasts 1.5 s, and that
of aux B 1 s. The catalogue entry of a song contains their settings. The game also sets the master volumes of MusyX to
127 at start-up. A rip leaves them at the MusyX defaults. The output is the same. Many cutscenes of the game stream
their music and voices from `bio0/demo/`. GCSF doesn't rip streams.

The sound effects of the game follow the songs. Each of the other archives in `bio0/sound/` contains one group of
effects. Its samples are in the file of the same name with the extension `.sam`. `all.arc` contains the effects that the
whole game plays. The others contain the effects of rooms, doors, enemies, weapons and the extras, and the voices. The
first disc contains 137 such archives and the second 192. 100 of them are on both discs, with the same files. The ripper
lists the effects archive by archive, in the order of the archive names. At start-up, the game pushes the group of
`all.arc`, then the groups of ten archives of weapons, and keeps them. A table in the game code at 0x80255D68 lists the
ten. A row of 8 bytes contains the ID of a group in a big-endian word and the address of the name of the archive. A
negative ID ends the table. Those groups share no ID of a macro, a table, a sample or an effect with the other archives.
The effects of each archive start only macros and samples of that archive. A rip of an effect pushes the group of
`all.arc`, then the group of the effect. The game starts an effect through sndFXStartEx with a volume and a pan from the
caller. The game then sets the send of the effect to aux B. Most calls pass a volume of 0xFF. MusyX then uses the volume
of the row of the effect in the effects table of its group. 98% of the rows set a pan of 64. A rip plays an effect at
the volume and the pan of its row, without the send. 2,002 rows on the first disc and 1,436 on the second set a volume
of 0. Their effects make no sound at that volume. In all but two archives, those rows start the same macro. The tables
of most archives contain 50 rows. The rows of volume 0 look like the unused rows of the tables. The ripper leaves them
out. A rip names an effect after its archive and its ID in four decimal digits, for example `sh00-1467`. The cutscene
effects follow, cutscene by cutscene, in the order of the cutscene names. A rip of one pushes the effect group and plays
the song through sndSeqPlayEx at the volume of the game option, 127, raised over 133 ms as the game does. A rip names
such an effect after its cutscene and its song ID in four decimal digits, for example `dd01-0413`. `--bgm` leaves them
out. 97 of the 128 make no sound, and the ripper skips them. 95 of those play only notes of velocity 1 or less. The rips
contain the other 31: 6 from the first disc and 25 from the second.

Star Wars Rogue Squadron III: Rebel Strike packs its files in `Rogue3/data.dat` as Rogue Leader does. `Rogue3/data.dir`
pads each number with spaces on its left. The 172 banks in `audio/data` have the layout of Rogue Leader. Many banks
contain copies of the same groups, with the same effects. The projects of the banks list 871 effects. Each effect ID
belongs to one group. A rip of an effect pushes the group of the effect from the first bank that lists it. The effect
plays through sndFXStartEx with the volume and the pan of the table of its group. The library contains the 45 banks that
the effects use. A rip names an effect after its ID, for example `RebelStrike-0048`. An index at the start of
`audio/data` names the game's sounds. The ripper doesn't read it. The game streams all of its music. 90 of the banks
contain its songs. Each song plays one note. The program of the note plays stream buffers: a pair of short samples for
the left and the right channel, with the sample type 6 or 5 in the sample directory of the bank. The game probably fills
the buffers from the disc as a song plays. For each bank that contains songs, the disc contains a file named after the
number of the bank, such as `Rogue3/dg00005.vsp` for bank 5. GCSF doesn't rip streams. The ripper leaves the songs out.

The `main.dol` of Rebel Strike is a loader. It contains the OS and the disc library. The code and data of the game,
MusyX among them, are a second DOL. `main.dol` contains that DOL packed at 0x80200000: two big-endian words, the packed
size and the size, then the DOL as zlib packs it. The start-up of the loader unpacks the DOL into ARAM. The game then
runs from 0x7F200000 through the page table of the CPU. The GCSF CPU translates addresses only through the BATs. A rip
places the DOL of the game 16 MB above its addresses, from 0x80200000, in place of the sections of `main.dol` that it
overlaps. The rip replaces the start-up of the loader with code that maps 0x7F000000 to 0x7FFFFFFF there with IBAT3 and
DBAT3. MusyX keeps its state in the memory of the loader. The memory hooks of the game are two mallocs and a free. The
rip patches sndSetHooks to use the malloc of the driver for both. Each level sets a reverb on aux A of studio 0, or
none. A rip sets none.

Pokémon Channel plays its music as MusyX effects. `en/pch/pchsound.proj`, `.pool`, `.sdir` and `.samp` are the four
MusyX files. They contain every group of the game, with 1,159 effects. Each piece of music is a layer of two voices. The
voices play one stereo recording, panned hard left and right. The game's music function (0x80015398) plays an effect of
its table at 0x8015EB50: 38 rows of 8 bytes. A row contains two big-endian words: the ID of the effect, and the number
of a sound whose group the game pushes. That group is the one that the project assigns to the effect. The ripper lists
the effects of the table first, in its order, as music. `--bgm` selects them. The other effects of the project follow,
in the order of their IDs. A rip of a sound pushes the group of the sound alone. The game starts every sound through
0x80015570: sndFXStartEx with a volume of 0x7F and the default pan, then MIDI controller 7 with the volume of the sound,
and the pan through controllers 0x83 and 10. A rip plays every sound with controller 7 at 127 and the pan in the centre
(0x40). The game plays its music that way. The scripts of the game play the themes of the TV programmes as effects. A
rip lists them with the effects. The script of the sound viewer of the game, `pch/scripts/sndviewer.pkc`, names the
sounds. Its names are strings between tabs. They start at offset 0x197C3. The code of the script from offset 0x4552 sets
the effect for each name (see `ViewerNames` in ntdlib's `src/musyx/catalogue_pokemon.cpp`). A rip names a sound as the
viewer does, for example `SND_SUOU`. A sound that the viewer doesn't name is named after the set and its ID, for example
`pchsound-1064`. The sample file of the game is larger than main memory. The loader of the game hands the samples to
MusyX through the MusyX upload callback. `driver/musyx_upload.c` does the same.

Nintendo Puzzle Collection (Japan) contains a menu and three games: Dr. Mario, Panel de Pon and Yoshi's Cookie.
`main.dol` contains the OS, the disc library and MusyX. `main.dol` loads the menu and each game as a module of code (a
REL) from a file that Yay0 packs: `menu_relsamp.szp`, `Dr_MARIO.szp`, `panepon.szp` and `ycookie.szp`. The game code
names some files in another case than the disc does. The disc library matches names in any case. A Yay0 file starts with
"Yay0", the unpacked size, and the offsets of a table of copies and a table of bytes, in big-endian words. Masks of 32
bits follow. Each bit of a mask, from the top, either copies the next byte or copies earlier output. The big-endian half
of a copy contains the distance back, less 1, in its low 12 bits and the length, less 2, in its top 4. A length of 0
there reads the length, less 18, from the table of bytes. The ripper finds the songs and the MusyX files through the
relocations of a module against that module. An object runs up to the next place in its section that a relocation points
at.

The file of the menu unpacks to four parts: the module of the menu, then the samples of groups 2, 0 and 4. The first
four words of the file contain the offsets of the parts. The module contains the songs of the menu, and the project, the
pool and the sample directory of groups 0 to 5. `title/yoshi_title.samp`, `title/agb_title.samp` and
`title/grand_staff.samp` contain the samples of groups 1, 3 and 5. Section 4 of the module contains, at 0x448, a table
of the song tables of the groups. A rip of a menu song pushes group 0, the effects of the menu, then the group of the
song. The code of the menu sets a volume for each song from another table in section 4, by its group and ID. The modules
of Dr. Mario and Panel de Pon each contain their songs and one set of MusyX files. `dr_test.samp` and `panepon.samp`
contain the samples. The two games push groups 1 and 0, and play their songs in group 1. Dr. Mario plays its songs at
full volume during play. The game lowers the volume of a song during a pause, in its manual and in some other states. A
rip plays each song at full volume. The code of Panel de Pon sets a volume for each song in a table. The module of
Yoshi's Cookie contains a table of the items in `ycookie.bin`: the files of three groups, then the songs, then the
samples of the groups. The game pushes groups 0 and 1 at start-up, and group 2 before a song of that group. Yoshi's
Cookie sets the MusyX master volumes for music and for effects from two levels in the options of the game. Tables at
0x179B74 and 0x179B7F in section 5 of the module contain the volume of each level, for music and for effects. A rip uses
the default levels of the options: a song plays with the master for music at 58, and an effect with the master for
effects at 105.

The ripper lists the 9 songs of the menu, then the 24 of Dr. Mario, the 59 of Panel de Pon and the 26 of Yoshi's Cookie.
The name of a song starts with the name of its module. The songs of the menu are named after their IDs, for example
`menu-01`. The songs of Dr. Mario and Panel de Pon keep the names that the games give them, for example
`panepon-flower_01`. The songs of Yoshi's Cookie are named after their files, for example `ycookie-sng05`. `--bgm`
selects every song. The effects follow the songs: the 9 of the menu, then the 106 of Dr. Mario, the 527 of Panel de Pon
and the 65 of Yoshi's Cookie. The name of an effect is the name of its module, `-se-` and the ID of the effect in four
digits, for example `panepon-se-0012`. `dr_mario-se-0105` makes no sound. A rip leaves it out.

The effects of the menu are in group 0, those of Dr. Mario and Panel de Pon in group 0 of their sets, and those of
Yoshi's Cookie in group 1. A rip of an effect pushes the groups that the module pushes at start-up. The menu and Dr.
Mario start an effect through sndFXStartEx with a volume and a pan of 0xFF and studio 0. MusyX then reads both from the
effects table of the group. Panel de Pon starts most of its effects through a table at 0x165678 in section 5 of its
module. The table contains 16 bytes for each effect, in the order of their IDs: the volume at +4 and the pan at +5. A
volume of 0xFF there means 110, and a pan of 0xFF the pan of the group. A rip does the same. Other calls in the code of
Panel de Pon start an effect with a volume and a pan of 0xFF. The module of Yoshi's Cookie contains a table of effects
at 0x1797B4 in section 5, with a row of 12 bytes for each: the ID of the effect in a big-endian word, then its volume,
pan, surround pan, motion and reverb send. A motion of 0 keeps the pan still, and every row contains 0. The game starts
an effect through sndFXStartEx with the volume and pan of the row and studio 0. The game then sets controller 0x5B to
the reverb send and controller 0x83 to the surround pan. In stereo, the game sets a surround pan of 64 for every effect.
A rip does the same. The table contains 80 rows. The effects of the last 15 are missing from the project.

Each module sets up the aux effects of studio 0 for its sounds. The menu, Dr. Mario and Panel de Pon put the MusyX
high-quality reverb on aux A, with the same settings, and the MusyX chorus on aux B. The profile names those effects.
The catalogue entry of a sound contains the settings of the reverb and the chorus. Yoshi's Cookie puts the MusyX
standard reverb on aux A and no effect on aux B. The words of a Yoshi's Cookie sound replace the callback and the
preparation of aux A in the block of the driver, and clear those of aux B. Yoshi's Cookie also limits MusyX to 40 voices
for music and 24 for effects with sndSetMaxVoices. The words of the sound write those limits into the MusyX state. The
menu, Dr. Mario and Panel de Pon call sndVolume(127, 0, 0xFF). The menu and Dr. Mario then call sndVolume(110, 0, 0xFF).
The start of a song sets the group of the song again. A rip of a song leaves the second call out. The effects keep 110.
A rip of an effect makes the second call too. Yoshi's Cookie calls sndVolume(127, 0, 0xFD) and sndVolume(127, 0, 0xFE),
then sets the master volumes. The call sndVolume(127, 0, 0xFF) of a rip sets the same volumes. The rip then sets the
master that the sound plays under. The options of the game set the output to mono, stereo or surround. A rip plays in
stereo, the default option.

The NDDEMO tech demo is a GameCube disc that never went on sale. Its disc ID is `00\0E01`: a game code of two zeros and
a NUL, then E, and the maker code 01. `gcsfrip` prints the NUL as `\x00`. The disc header leaves out the GameCube magic
word. The ripper reads such a disc when its file system table and `main.dol` are sound. The code of the demo lists its
nine songs at 0x80065394. Each entry is the address of the path of a song. The place of a song in the list is its song
ID. The song files are in `audio/` and have the extension `.sng`. The one set of MusyX files of the demo is in `audio/`
too: `nddemo.prj`, `nddemo.pol`, `nddemo.sdr` and `nddemo.smp`. At start-up the demo pushes group 0, the instruments of
the songs, and group 1, the effects. A rip of a song pushes both. The demo plays a song through sndSeqPlayEx with group
0 and studio 0. The demo then sets the volume of the song to 127. A rip does the same. The demo calls sndMasterVolume
only to mute its music and to fade it. A rip doesn't call it. At start-up the demo also puts the MusyX high-quality
reverb of 5 s on aux A of studio 0, and the MusyX chorus on aux B. The catalogue entry of a song contains the settings
of the reverb and the chorus. A rip names a song after its file, for example `coins`. `--bgm` selects every song. All
nine loop.

The four sound effects of the demo are in group 1. The effects table of the group in `nddemo.prj` lists them in rows of
10 bytes, as in the projects of Metroid Prime (see above). A row starts with the ID of the effect in a big-endian half.
The demo starts each effect through sndFXStartEx (0x80047DD4) with the ID of the effect, a volume and a pan of 0xFF, and
studio 0. The table of the group then sets the volume and the pan. Four functions of the demo start one effect each:
0x800272A0 effect 0, 0x80027270 effect 1, 0x80027240 effect 2 and 0x80027210 effect 3. A fifth, at 0x800272D0, starts
effect 0 and then sets two of its MIDI controllers through sndFXCtrl14 (0x80047D74): 0x84 and the pitch wheel, 0x80,
each to 0x1000. A rip plays each effect as the first four functions start it. A rip of an effect pushes both groups
first. The rip lists the effects after the songs, in the order of the table. The header `audio/nddemo.h` on the disc
names them. It contains a line `#define SFX<name> <ID>` for each effect. A rip names an effect as that line does, for
example `SFXcoin_oneshot`.

The sound library of the demo is an early MusyX. Its sndInit takes no number of studios. Its sequencer runs five times
in each frame, in steps of 1 ms. A step adds floor(floor(tempo × 3,072 / 60) × 65 / 8) / 65,536 ticks. The build counts
a millisecond as 65 / 65,536 of a second, not 1 / 1,000. The songs of the demo therefore play 0.8% slower than their
tempo. A rip plays them at the speed of the demo. The disc library accepts only names in the 8.3 form unless `bi2.bin`
allows long names. The `bi2.bin` of the demo doesn't. A rip patches the library to accept the long names of the driver.
The demo has no DVDReadAsyncPrio. The driver reads the disc through DVDReadPrio instead.

### Chord effects

Donkey Kong Jungle Beat plays many of its instrument effects (`JA_SE_IST_*`) in the chords of the music. The sequence of
such an effect reads its keys from ports 0xb to 0xf of its tracks. While port 0xb contains 0, the sequence plays
nothing. The chord code of the game works out the keys and writes the ports as it starts each step of the effect. A
record of the chord database (`ChordSeDataBase.csd`) starts an effect. The chord archive (`JaiChord.arc`) contains a
chord file for the music of each stage. The addresses below are in the main.dol of the USA release (`GYBE01`).

- `startChordSe` (0x8024c4c8) starts a record. It takes the chord effects of the audio manager (the pointer at +0xd08 of
  the manager), the record, an object, 0, and the volume in f1. The game passes a volume of 1.0. The function refuses a
  record until the chords of the music are in. Some states of the game make it refuse records too.
- `startChordSe` swaps two effects by stage. The stage object, at +0xd0c of the audio manager, contains the number of
  the stage at +4. At stage 0x2c, a record of `JA_SE_IST_AUTO_COMP_ALTO_SAX` (0x50042) or `JA_SE_IST_AUTO_COMP_TRUMPET`
  (0x50045) plays `JA_SE_IST_AUTO_COMP_E_PIANO_T` (0x50063) in its place. At stages 0x35 and 0x41, such a record plays
  `JA_SE_IST_AUTO_COMP_E_PIANO` (0x50062).
- 0x8024c28c gets one of the 64 players of the chord system, at +0xc of the chord effects. The byte at +0x18 of a player
  is set from its start until the game stops it. Its byte at +0x19 is set while a step of its effect waits for a beat.
  Its sound object at +0x34 contains the JAISoundHandles of the steps at +0 and their number at +4.
- 0x8025049c starts the music of the row of the stage object (+0xc) in the stage table (0x803b3978, 16 bytes a row) in
  the BGM player. It then lets chord effects start (the byte at +0x34 of the stage object). The row is normally the
  number of the stage. The first row contains `JA_BGM_MAIN_01`.
- The rhythm, at +0x810 of the audio manager, follows the beats of the music. Its frame process (0x80248784) runs from
  an object of the scenes of the game. The frame process of the audio manager doesn't call it. The scenes run the
  movement list of the rhythm (list 2, 0x8020dcf8) before that of the audio object (list 1, 0x8020dfe0). In each frame,
  the frame process of the rhythm therefore runs before that of the audio manager (0x8023f390).
- 0x80253684 (player, codes, time) sets each of the 16 tracks of a piece by a code, as a state of the BGM player does.
  Code 2 sets the volume of a track to 0 and mutes the track at once (`JASTrack::mute`, 0x800088dc). A muted track runs
  on. It starts no notes: `JASTrack::noteOn` (0x8000805c) returns at once while the track or one of its parents is
  muted.

The words of the chord database are big-endian. "CDSE" is at +4, the number of entries at +8 and the offset of the table
of records at +0xc. The table contains a count, then 16 bytes for each record: the entry, the sound ID and two more
words. The count of the table can be larger than the file contains. The file contains 164 records and one of zeros. The
count is 218. The driver reads only the records that the file contains.

**The state of a rip.** The game plays a chord effect only over the music of a stage. Its keys depend on the stage, on
the chord of the moment and on earlier keys. A rip plays one state: the state that the game plays first or most, by its
code. The driver starts the music of stage 0, the first row of the stage table. The game plays stage 0 for any scene
that the table leaves out. The driver then turns each track of the music off at once with code 2. The tracks still run
and supply the chords and beats of the effect. Their notes take no channels and look up no waves. The driver starts each
effect through `startChordSe` with the lowest-numbered record that names it. The key memory and the random state are
those of the game after its start-up. `JA_SE_IST_AUTO_COMP_E_PIANO_T` plays at stage 0x2c and
`JA_SE_IST_AUTO_COMP_E_PIANO` at stage 0x35. Both play the first record of `JA_SE_IST_AUTO_COMP_ALTO_SAX`. A step that
waits for a beat waits for the first beat of its kind in the music. The records name 72 effects. With the two that some
stages play in their place, a rip covers 74. The sequences of two of them are empty: `JA_SE_IST_SWAVE_HIT_SIGNAL` and
`JA_SE_IST_SWAVE_HIT_IMPACT`. The ripper skips them. `JA_SE_EAT_BIND_COMBO` (0x70) has no record. It plays one tone of
the chord for a count of the combo. The function of the audio manager that starts a sound with a count, 0x8023f98c,
starts it through the chord code (0x80245788). The driver starts it that way over the same music, with a count of 0.

**The end of an effect.** A player stays busy after its last step until the game stops it. Later in a combo, the game
moves some effects on by more steps. A rip plays the steps that start by themselves: the first step and the steps that
wait for a beat. The effect ends when no step sounds and no step waits for a beat. Some effects repeat their steps until
the game stops them. Their rips play to the limit of the length analysis. The driver writes the JAISound of the music to
its block (see "Driver"). The length analysis leaves out the commands of the tracks of that sound. A track belongs to
the music if the root track of the music is the track or one of its parents. A JASTrack contains its parent at +0x12c.

### Effects that the game starts with a count

Donkey Kong Jungle Beat starts most effects by name. 0x8022f968 (name, count) looks the name up and calls the function
of the audio manager that starts a sound with a count, 0x8023f98c (manager, sound ID, count). For six effects, that
function writes the count to port 0xb of the sound once it starts (0x802405ac). It keeps the count of
`JA_SE_PAUSE_MENU_CLAP_PARAM` from 0 to 11, and that of `JA_SE_BOSS_DK_FIST_COMBO` at 6 or below. Some objects of the
game start a sound and then write a value to port 0xb of it in the same way (0x8024658c). The sequences of these effects
add the port to a key. The game code sets the value. A rip plays the value that the game code first gives an effect. The
addresses are in the main.dol of the USA release (`GYBE01`).

| Effect | Sound ID | The value | First | Key |
|---|---|---|---|---|
| `JA_SE_PAUSE_MENU_CLAP_PARAM` | 0x8e | the microphone setting of the pause menu + 1, after each step of the setting (0x801c8964). The setting runs from 0 to 9. A step out past either end plays nothing. The menu works the setting out from the threshold of the microphone as it opens (0x801c87f4, 0x801c8748): (threshold - 20) / 9, in whole steps. The threshold of a new save is 75 (0x8021ddf4, 0x80219c9c). The setting then starts at 6. The menu checks a step down first (0x801c8de4): to 5. That step plays 6. A step up goes to 7 and plays 8. | 6 | 71 + the value. With 0, the effect plays no note. |
| `JA_SE_BEAT_POINT_ADD` | 0x37 | the points added so far + 60 (0x802400c8), while the points added stay below a limit (0x80240090). Both start at 0 (0x80240064). No code in main.dol sets the limit. The game therefore never reaches the start. | 60 | the value |
| `JA_SE_OBJ_SOUND_BLOCK_DISAPPEAR` | 0x4000f | the entry of a table (8, 4, 1, 0 at 0x803f09a0) for the step of the block (0x80244aec). The step starts at 0 (0x802438cc) and goes on by 1 after each block (0x80243998). It goes back to 0 after 90 frames without a block (0x80243940). | 8 | 56 + the value |
| `JA_SE_BOXING_COMBO` | 0x0e | the count of hits of the combo - 1, at most 8 (0x80159f30). The hit raises the count first (0x80159eb8). | 0 | 60 + the value |
| `JA_SE_CTN_CALL_GUAGE` | 0xc7 | the step of the banana gauge, less 1 (0x801a5ef4). The gauge starts at 0 (0x801a5c74) and rises. Each step of 200 plays the effect. | 0 | 67 + the value |
| `JA_SE_OBJ_SOUND_BLOCK_APPEAR` | 0x4000e | the entry of a table (0, 4, 7, 14, 12, 9 at 0x803b2cf0) for the step of the block (0x80244ac0). The step starts at 0 and goes on by 1 after each block (0x80243968). | 0 | 60 + the value |
| `JA_SE_PLY_JUMP_OBJ` | 0x2000b | the count of the object (0x80244aa0). It starts at 0 and rises by 2 after each jump (0x80243948). | 0 | 53 + the value |

The profile passes the driver the effects whose first value isn't 0. The driver starts the first two through 0x8023f98c
with their counts. It starts `JA_SE_OBJ_SOUND_BLOCK_DISAPPEAR` as it starts other sounds, then writes its value to port
0xb (0x802405ac). The other effects play their first value, 0, without the profile. Three effects stay open. The game
code names neither `JA_SE_BOSS_DK_FIST_COMBO` (0x47) nor `JA_SE_BOXING_COMBO_DMY` (0x1c). Nobody has found the code that
starts them. `JA_SE_CHR_FAILY_SING` (0x30043) uses a note of the chord that plays. Their rips play them with 0 at the
port. `JA_SE_STAGE_RESULT_DEC` doesn't read the port in this release.

### Wave groups

For a game that loads its wave groups as its scenes require them, the ripper finds the groups that each sound requires.
It plays the sound with no group loaded. Each note still looks up its wave in its wave bank. The ripper watches the
lookups: the bank and the id of the wave. It plays the sound as far as the length analysis would (see "Lengths"), with a
lookup counting as a sound. Then it picks groups from the wave system of the bank. The group that contains the most
waves of the sound comes first, then the group that contains the most of the rest, and so on. Of two groups that contain
as many, the lower group goes first. The `.minigcsf` lists the groups. The driver loads them before it starts the sound.
The length analysis then plays the sound with them loaded. The ripper finds the groups with `--no-length` too. The scene
groups of Super Mario Sunshine share some waves, with the same samples. The game keeps one of them in ARAM at a time.
Its driver loads the first group of the list that the start-up doesn't load.

For a piece of Pikmin 2 that the game plays, the ripper lists the groups that the scene of the game loads for the piece
instead. A note whose wave is in another group is silent in the game, and in the rip too. Every effect of Pikmin 2 finds
its waves in group 0.

### Sound files from another release

`--sound-files DIR` makes the ripper read three kinds of file from another release of the game: the start-up archive,
the sequence archive and the wave archives. Everything else comes from the input, the game code and the disc header
included. The ripper searches all of `DIR` for a file with the name of the start-up archive of the profile
(`Z2Sound.baa`). The folder that contains it stands for the folder of that start-up archive in the profile
(`Audiores/`). The sequence archive and the wave archives are read from their places relative to it. A `DIR` with no
such file, or with more than one, is an error. A sound file that is missing from `DIR` belongs to the other release. The
ripper doesn't read the file on the disc in its place.

The sound table and the wave systems then come from the other release. A wave group whose archive is missing from `DIR`
is taken out of the group lists of the sounds, with a warning. Those sounds play without the waves of the group. The
`FILE` chunks of the library keep the paths of the disc. A rip from the files of another release therefore plays like
any other.

A sequence archive that is missing from `DIR` can come from a sequence table: one `.bdt` file in the folder where the
archive would be. The Wind Waker HD keeps its sequences that way (`Seqs/JaiSeq.bdt`). The table is a 32-bit count, zeros
up to 0x20, and then a 32-bit offset and size for each sequence. The index of a sequence in the table is the id of its
file in the archive of the GameCube release. The ripper reads the archive of the disc and replaces the data of each file
with the entry of the table for the id of the file. The game code finds a sequence by the id of its file. The archive
therefore keeps the names and ids of the disc. The data of each file starts on a 32-byte boundary.

### Lengths

The ripper plays each sound in game mode to work out its length. Sound means a sample louder than 8 of 32,767 (-72 dB).
The ripper watches the sequence parser of the sound library. It records each command that each track of the sound runs.
In The Wind Waker, Luigi's Mansion, Super Mario Sunshine, Pikmin, Pikmin 2 and Pac-Man Vs., the effects sequence plays
all along. Each effect plays in one of its tracks. The events sequence of Pikmin plays all along too. There the ripper
follows only the root track of the sound and the tracks below it: the tracks of a piece of music, or the track of the
effect and the tracks that the effect opens.

- A sound ends when the game releases it. Its notes can sound on after that: the output runs behind the game, and a
  release fades on. The length of a sound that ends runs to its last sound.
- A sound loops when one of its tracks jumps back to a command it has already played, twice to the same command. A jump
  inside a subroutine or a counted loop doesn't count. The length of a looping sound is the time of the second jump
  back. Its fade is 7 seconds. A pass runs from the last visit of the track to the command that the jump goes to. A loop
  inside the pass is part of the pass. Its jumps back don't start the count of the outer loop again.
- A pass that changes nothing doesn't count: it plays no note, changes none of the parameters of the track, and opens
  and closes no track. Such a pass only waits, or polls the game with conditional commands. The track idles in that
  loop. A pass that also plays notes counts, whatever it polls. Loops before the first sound don't count either. A loop
  that completes without a sound ends the sound at its last sound instead.
- A track can run an interrupt. The root tracks of Pikmin 2 run one every tick. The commands of the interrupt count as
  those of a subroutine.
- Mario Kart: Double Dash!! starts a level sound again each frame. Some of its level sounds finish each pass. The next
  start then runs the finished tracks again from their first commands. Such a start ends a pass of the whole track,
  wherever the track finished. Two passes in a row loop the sound. Two jumps back do the same. The loops inside the
  track don't start that count again.
- In Pikmin 2, Pac-Man Vs. and Donkey Kong Jungle Beat, a track can also idle in a loop inside a subroutine. That
  applies only when the passes of the loop change nothing. Many root tracks of Pikmin 2 call a routine that polls the
  game once a beat and never returns. The cave music is the exception. Its conductors pick the phrases at random. The
  piece of a cave never repeats. The analysis counts the polls of its root track as moves, and the piece plays to the
  limit. In one effect of Pac-Man Vs. (se1-0006), the track of the effect polls in such a routine while a child track
  loops. In Donkey Kong Jungle Beat, the first track of an effect calls a routine that waits a tick and jumps back while
  any other track of the effect plays. The loops of those tracks count.
- The tracks are taken in the order in which they start. The loop of a track counts only while each track before it
  idles, has ended, or has run no command for 10 seconds with no ticks left to wait. Many sound effects work this way:
  the first track polls until the game stops the sound, and another track loops. The events of Pikmin call each action
  as a subroutine. An action that loops plays until the limit, unless it holds. In two pieces of Mario Kart: Double
  Dash!!, a note holds a looping wave while the first tracks wait 133 seconds between their jumps back. Those pieces
  play for 266.665 seconds, two loops of the first track. 16 level sounds of the game play until the 600-second limit.
  Each holds the note of an engine while a timer interrupt sets the parameters of the track 24 times a second. Each run
  of the interrupt is a move of the track.
- A sound holds when one of its tracks idles, every track has settled as above, and for 10 seconds no track has run a
  command outside the loop it idles in. A note on a looping wave that nothing releases holds like this. In Luigi's
  Mansion, a track with at least 0x2000 ticks left to wait has settled too. Some of its effects hold a note with a wait
  of 16,000 or 32,000 ticks until the game stops them. In Super Mario Sunshine the threshold is 0x7000 ticks. Some of
  its effects hold a note of 32,767 ticks. In that game, the commands of the interrupt routine of a track aren't moves
  of the track. Some of its effects hold a note while a timer routine polls the game every 5 ticks. A sound that has
  sounded in the last second loops where it holds, with the fade. Any other sound ended at its last sound.
- A sound that stays busy without a sound for 180 seconds ended at its last sound.
- Music plays on to 3,600 seconds: a loop longer than 10 minutes still gets two loops and the fade. A sound effect stops
  at 600 seconds. A sound still playing at its limit gets a length of 600 seconds and the fade, and the ripper prints a
  warning. A piece of Pikmin 2 that a conductor plays never repeats. It stops at 600 seconds, with no warning.

The songs of a MusyX game follow other rules. MusyX plays a song in one section, or in up to 16 sections. A song in
sections puts each of its tracks in one of them. Most songs of Star Fox Adventures are in sections. Each section has a
loop point and loops apart from the others. A section loops when one of its tracks ends in a loop entry. MusyX counts
the loops of each section. The ripper reads the counts. For Mario Golf: Toadstool Tour, Mario Power Tennis and
WarioWare, Inc.: Mega Party Game$!, the ripper watches the MusyX slot for each track of a song instead. The first word
of a slot is the tick at which the next region of its track starts. The ticks of the tracks of a section fall when the
section loops. The length of a looping song runs until each section that loops has looped twice, plus the fade. A song
that ends leaves the MusyX lists of playing songs. The rip of such a song ends at its last sound. The output of MusyX
keeps a constant level on each channel after a song has stopped. For these songs, a sample therefore counts as sound
when it differs by more than 8 from the average of its channel over about the last 50 milliseconds. The rules above for
loops before the first sound, loops without a sound, 180 seconds without a sound and the 600-second limit still apply.

The pieces of Final Fantasy Crystal Chronicles follow the MusyX rules, with the state of Red in place of the state of
MusyX. Red plays a piece in one of its three music controls. The loop command of a piece returns all of its tracks to
their state at the start of the loop. The ripper watches the code that does that. The length of a looping piece runs to
the end of its second loop, plus the fade. A piece that ends leaves its control. Its rip ends at its last sound. Sound
is a sample that differs by more than 8 from the average of its channel over about the last 50 milliseconds.

The effects of the game follow the same rules. Red plays an effect on the tracks of its fourth control. Each track loops
on its own. The last command of a track can send it back to the start of a loop for ever. The ripper watches the code
that does that. The first track that loops sets the loops of the effect. Another track that is still busy holds the fade
back until it has looped twice too. Red can also vary the steps of a track at random. It reads the random numbers from a
fixed table, one after another. A rip plays the steps that the first numbers pick. The loops of such an effect differ
from pass to pass. The ripper measures each loop as it plays.

The sound library of Animal Crossing runs the sequence player of the Nintendo 64. The player runs a script for a piece.
Each channel of the piece and each of their layers runs a script too. Each script is a track for the rules above. The
script of the player is the root. The channels and layers follow in the order in which they start. A counted loop counts
as a subroutine. A piece ends when its sequence player stops. An effect plays in a channel of the sequence of effects.
The script of that channel is the root of the effect. The layers of the channel follow. An effect ends when the sound
library frees its slot. Doubutsu no Mori+ and Doubutsu no Mori e+ follow the same rules.

The effects of the Bonus Disc follow the MusyX rules. The list of the effects that play in the sound library takes the
place of the MusyX lists. An effect that ends leaves the list. Its rip ends at its last sound. The output of AX rests at
0 once an effect has stopped. Sound is therefore a sample louder than 8. The JAudio games use the same rule. The
analysis doesn't check for loops. None of the effects of the program loops.

The effects of Metroid Prime, Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door and Star Wars Rogue Squadron
III: Rebel Strike follow the MusyX rules too. An effect plays on the MusyX voices. Each voice runs a macro: steps that
start a sample, change the sound and wait. The ripper reads every voice once a frame. A rip sends an effect no events,
such as a key-off. A voice whose macro has ended, or waits for an event, therefore changes no more. Such a voice has
settled. The sound changes when a voice starts or stops, moves to another step, or starts to wait. A macro loops when a
LOOP step without a count jumps back. The first voice that loops sets the loops of the effect. The length of a looping
effect runs to the second jump back of that voice, plus the fade. Another voice that is still busy holds the fade back
until it has looped twice too, or has settled. An effect whose voices have all settled holds while its samples loop. It
loops at the later of two points: 10 seconds after its sound last changed, and the second time since then that each
playing sample has gone back to the start of its loop. An effect whose voices have all settled while no sample plays has
ended. The other rules for MusyX songs still apply. Some macros of Rebel Strike jump back through GOTO instead of LOOP.
Most of them make a random choice or test a variable first. The ripper finds no loop in them. 19 effects of the game
play to the 600-second limit that way. The effects of Pokémon Channel, its music included, follow the same rules. So do
the effects of Tales of Symphonia, Star Fox Adventures, Rogue Leader, Pokémon Colosseum, Resident Evil Zero, Mario Party
4, Mario Golf: Toadstool Tour, Mario Power Tennis, Nintendo Puzzle Collection and the NDDEMO tech demo, and the effects
that WarioWare, Inc.: Mega Party Game$! starts through sndFXStartEx. The effects of WarioWare that are songs follow the
rules for its songs. Two effects of Tales of Symphonia play to the limit too. In one, the macro waits a random time
before it jumps back. In the other, the macro waits 100 minutes before it holds. Three effects of Rogue Leader reach the
limit as well. Their macro tests a variable, plays a sample and jumps back through GOTO.

The ripper lists and skips the sounds that never make a sound. It also skips a sound that stops the emulated machine
with an error before the sound makes a sound. The message of the ripper then names the error. The effect se2-0239 of
Luigi's Mansion is one: it reads an instrument from past the end of the table of its bank. `--no-length` leaves out the
analysis. Without the analysis, no sound is skipped and the files have no length tags.

Tags written by `gcsfrip`: `title` and `gcsf_sound` (the name of the sound, or the name of the file for a sequence that
no sound plays), `game` (the value of `--game`, or else the title in the disc header, or else the name of the image or
folder), `length` and `fade`, `loop_start` and `loop_length` for a loop, `_lib`, `gcsf_var` for a variant that sets
ports, `gcsf_mode`, `gcsfby`, `utf8=1`, and optionally `artist`, `year` and `copyright`. Only the options set `artist`,
`year` and `copyright`.

## Copyright

A `.gcsflib` contains copyrighted game code and data. Rips are for personal use.
