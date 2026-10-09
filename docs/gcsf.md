# GCSF: GameCube Sound Format

Version 1. GCSF stores sequenced music from Nintendo GameCube games so that it can be played back as the game plays it.
A GCSF rip holds the game's code. A player runs that code on an emulated GameCube: the game's OS, its sound library and
its DSP program. The game plays the sound itself. The rip is therefore exact.

GCSF works like 2SF for the DS. A rip requires a driver profile for the game (see "Ripping"). Version 1 rips games
whose sound library is JAudio2. JAudio2 is the sound library of Nintendo EAD's JSystem.

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

Rips contain GCSF's driver, `driver/driver.c`. The driver is loaded at 0x81700000. Its entry point is at the same
address. The rip patches the game's `main` to branch to it. The driver calls the game's functions to do what the game's
start-up does for sound. It leaves out the graphics. It sets up the video interface, JSystem's heaps and ARAM manager,
the game's audio object and disc thread, and the game's audio set-up. It loads every wave bank into ARAM. Then it starts
the sound by its JAudio sound ID. From then on it calls the game's audio frame process once per video retrace.

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

A `.minigcsf` patches only the sound ID: a 4-byte `MEM` chunk at 0x8170001C. After applying all `MEM` chunks, a version
1 player checks the driver block. If the block has the expected magic, the player rejects versions other than 1.

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
Version 1 supports The Legend of Zelda: Four Swords Adventures (USA).

The `.gcsflib` contains:

- the sections of the game's `main.dol`, with `main` patched to branch to the driver;
- the driver and its parameter block with the game profile;
- the disc's header (`sys/boot.bin`), the sound library's start-up archive (a BAA), the sequence archive, and every wave
  archive (`.aw`) that the start-up archive's wave systems name, as `FILE` chunks;
- a process descriptor.

Each `.minigcsf` selects one sound by its JAudio sound ID. The sound table in the start-up archive lists the sounds and
names them. A sound's ID gives its section in the top byte, its group in the next byte and its index in the low 16
bits. `--bgm` selects the sounds of the section named BGM. `--only` selects the sounds whose names match a regular
expression.

### Lengths

The ripper plays each sound in game mode to work out its length. Sound means a sample louder than 8 of 32,767 (-72 dB).
The ripper watches the sound library's sequence parser. It sees each command that each of the sound's tracks runs.

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

The ripper lists and skips the sounds that never make a sound. `--no-length` leaves out the analysis. Without the
analysis, no sound is skipped and the files have no length tags.

Tags written by `gcsfrip`: `title` and `gcsf_sound` (the sound's name), `game`, `length` and `fade`, `gcsf_mode`,
`gcsfby`, `utf8=1`, and optionally `artist`, `year` and `copyright`.

## Copyright

A `.gcsflib` contains copyrighted game code and data. Rips are for personal use.
