# gcsf

GCSF is a PSF-family music rip format for Nintendo GameCube sequenced audio. It covers the music and sound effects that
GameCube games play through JAudio, the sound library of Nintendo EAD's JSystem, in both of its generations: JAudio1 and
JAudio2. It also covers the songs that games play through MusyX, the sound library of Factor 5, and the sound effects of
16 MusyX games. Final Fantasy Crystal Chronicles plays its music and its sound effects through the Red driver by Square,
and GCSF covers both. Animal Crossing and its Japanese releases play their sound through a port of Nintendo's sound
library for the Nintendo 64. GCSF covers those as well. On the Pokémon Colosseum Bonus Disc, HAL Laboratory's sound
library plays the sequenced sound effects, and GCSF rips those too.

A GCSF rip contains the game's code. The player runs that code on an emulated GameCube: the OS, the sound library and
the DSP program of the game. The game plays the sound itself. The mixing, filtering and resampling therefore match the
game. A rip requires a driver profile for the game. `docs/gcsf.md` specifies the format.

## Supported games

Version 1 of the format supports the games below. GCSF doesn't rip streams. For a game that streams its music, GCSF rips
only its sequenced sounds. Where a game plays a piece in several states, a rip contains the piece as the game starts it
and a variant for each other state.

### JAudio games

- **The Legend of Zelda: Four Swords Adventures** (USA), its European release and its Japanese release, The Legend of
  Zelda: Four Swords Plus. Navi Trackers, a mode that only Four Swords Plus has, plays its music from streams. The game
  plays `JA_BGM_SELECT` with two sets of its tracks: one on the file select and one on the map where the players pick a
  stage. A rip contains both: `JA_BGM_SELECT` and `JA_BGM_SELECT map`.
- **The Legend of Zelda: Twilight Princess** (USA) and its European and Japanese releases. The game mutes some tracks of
  five pieces as it starts them, and a rip mutes the same tracks. The day music of Hyrule Field plays one arrangement
  while Link walks and another while he rides. A rip contains both: `Z2BGM_FIELD_LINK_DAY` and
  `Z2BGM_FIELD_LINK_DAY_riding`. The scenes, the fights and the other starters of the game give 17 pieces more states.
  Some states play a track at a volume between 0 and 1. A rip starts a piece in the state that the game starts it in,
  such as Castle Town's music in its first room. It contains each other state too, named after the piece and the state,
  for example `Z2BGM_TOAL_VILLEGE_status1`. The day music of Hyrule Field also follows the code of the field: what Link
  does, the enemies near him and the time of day. A rip contains each way through the piece, in each arrangement.
  `_status12` plays the sections of a long walk or ride, `_status4` to `_status7` the sections of a fight by their four
  entries, `_status10` and `_status11` the two endings of a day, `_status8` and `_status9` the starts that the code of
  the field makes once no music plays, and `_riding_status3` the section of Link standing on his horse. The game can
  also be ripped with the sound files of its Wii and Wii U releases (see "Sound files from another release").
- **The Legend of Zelda: The Wind Waker** (USA) and its Japanese release, Zelda no Densetsu: Kaze no Takuto. The game
  names none of its sounds. A rip names an effect after its category and index, for example `se4-0123`, and a piece of
  music after its file. Four effects pick the key of their note at random. A rip contains each of them once for each
  key that makes a sound, for example `se4-0254 key 48`. Some effects play one of several variants by a value that the
  game gives them, such as the material of the ground under a footstep. A rip contains each variant, named after the
  effect and the value, for example `se3-0000 6=4`. The `gcsf_var` tag contains the value, and a player plays another
  variant when the tag contains another value. The sound layer of the game mutes some tracks of 14 pieces as it starts
  them. The state of the game picks the tracks, such as the sail of the boat or an enemy near Link. Some IDs of the
  game start a piece in another arrangement. A rip starts each piece as the game starts it, and it contains each other
  arrangement, named after the piece and the ID or the state, for example `kaminoto 80000125` or `sea sailing`. The
  battle music and three other pieces fade in over two seconds. The game fades them in too. While some pieces play, the
  game writes a status to their port 9, such as the phase of a boss fight. A rip contains such a piece once more for
  each value that the game writes and that changes what it plays, for example `p_ganon1 9=2`. The battle music also
  comes with 1 on port 11. The game writes that value when Link hits an enemy. The rips of the Japanese release start
  the music in the same way and contain the same arrangements. The game can also be ripped with the sound files of The
  Wind Waker HD.
- **Luigi's Mansion** (USA). The game names none of its sounds, and a rip names them as for The Wind Waker. The game
  picks the tracks of its piece `LuigiSings` by Luigi's health and by the room. His health also sets the tempo. A rip
  contains the piece once for each state, named after the piece, the place and Luigi's health. A poison mushroom shrinks
  Luigi, and the tune then plays an octave higher. A rip contains that state too, with "shrunk" at the end of its name.
- **Super Mario Sunshine** (USA). The game keeps a table of the names of its sounds. A rip names each sound as that
  table does, for example `MSD_BGM_DOLPIC`.
- **Pikmin** (USA). The code of the game lists its pieces of music by the names of their files, and a rip names a piece
  after its file, for example `select`. The game plays the music of its cutscenes as parts of one piece. A rip names a
  part after the piece and the number of the part, for example `demobgm-5`. The rip includes the sound effects of the
  catalogue of the game, named as the catalogue names them, for example `SE_FLOG_JUMP`. The sound of the formation plays
  a full move of the formation straight ahead, and its `gcsf_var` tag contains the move. The streamed music of the game
  isn't ripped.
- **Donkey Kong Jungle Beat** (USA). The game plays many of its effects in the chords of the music. A rip plays such an
  effect over the music of one stage, with the music silent. The game also adds layers to its music as you play: a
  combo, a flight with a Helibird and each part of a boss fight turn more tracks on or change them. A rip contains 101
  variants of 43 pieces for those, one for each later state. A variant is named after the piece and what sets its state,
  for example `JA_BGM_MAIN_01_combo1` and `_combo2` for the layers of a combo, `_helibird` for a flight with a Helibird,
  `_round2` and `_change` for the second round of a boss and the change before it, and `_underwater` for the music of
  TropicalCoast while DK swims below. Some enemies and objects also play a loop beside the music while they're near DK,
  such as a sleeping hog or a stunned enemy. A rip contains 135 more variants of 27 pieces for those, one for each loop
  that a stage plays, named after the loop, for example `JA_BGM_MAIN_01_enemy_sleeping`. Each variant plays its state
  from the start of the piece. The game moves from one state to the next with a short fade, and the rips leave out those
  changes. The specification, `docs/gcsf.md`, lists the states and the stages that play each piece.
- **Mario Kart: Double Dash!!** (USA). The game streams the music of its courses. GCSF rips the other music and the
  sound effects.
- **Pikmin 2** (USA): the sequenced music and sound effects. A piece of music is named after its file. The music of a
  cave changes with the floor, and the music of a boss with the actions of the boss. `gcsfrip` rips a variant of the
  piece for the conductor of each other floor, named after the conductor, for example `caveconc_05_0`. It also rips a
  variant for each other part of the music of a boss, named after the piece and the part, for example `m_boss-3`. Two
  parts of the music of the final boss have a layer that plays only while a captain stands near the boss. They get one
  more variant each, labelled `-near`. The game keeps a table of the names of its effects. A rip names an effect as that
  table does, for example `PSSE_SY_MENU_CURSOR`.
- **Pac-Man Vs.** (USA). The game streams its music. GCSF rips its sequenced sound effects. The game names none of its
  sounds, and a rip names them as for The Wind Waker.
- **The Legend of Zelda: Collector's Edition** (USA). The disc streams its music. GCSF rips the sequenced sound effects
  of the menu. The sound table of the menu also lists one sequenced piece of music, `JA_BGM_DUMMY`. The code of the menu
  never starts it, and GCSF rips it too.

### Square's Red driver

**Final Fantasy Crystal Chronicles** (USA) plays its sequenced music and sound effects through Square's Red driver. A
rip names a piece or an effect after its file, for example `music100` or `se010001`. An effect in one of the blocks of
effects of the game is named after the block and its place in the block, for example `se000-001`. The streamed music of
the game isn't ripped.

### Nintendo 64 sound library

**Animal Crossing** (USA) and its Japanese releases, **Doubutsu no Mori+** and **Doubutsu no Mori e+** (disc versions 0
and 1 of each), play their music and their sound effects through a port of Nintendo's sound library for the Nintendo 64.
The games keep no names for them. A rip names a piece after the number that the game gives it, for example `bgm046`. A
piece that plays the hour is ripped once for each weather that changes it, for example `bgm001-snow`. The museum piece
is ripped once for each room, for example `bgm088-fish`. Doubutsu no Mori e+ has two rips of the island piece:
`bgm376-islander` with the islander on the island, and `bgm376-no-islander` without. An effect is named after its bank
and its place in the bank, for example `se2-017`.

### HAL Laboratory's sound library

**Pokémon Colosseum Bonus Disc** (USA): the sound effects of the program on the disc that sends Jirachi to a Game Boy
Advance. That program is a disc image inside the disc. A rip names each effect as the list of names in the program does,
for example `SFX_cancel`.

### MusyX games

- **Mario Golf: Toadstool Tour** (USA): the 38 songs, 9 jingles and 737 sound effects.
- **Mario Power Tennis** (USA): the 111 songs and jingles and the 980 sound effects. A rip of Mario Golf or Mario Power
  Tennis names an effect after its voice bank and its ID, for example `mario-0154`. An effect of the bank that the game
  loads at start-up is named `se`, for example `se-0413`. So is an effect of the other groups of Mario Power Tennis, for
  example `se-0151`. The name of a narration starts with `narration`, for example `narration-0362`. An effect of a court
  in Mario Power Tennis is named `court`, for example `court-0018`.
- **WarioWare, Inc.: Mega Party Game$!** (USA): the 231 pieces in the Sound Room. The game plays most of its sound
  effects as MusyX songs. GCSF rips them and the other effects of the game. A rip names an effect after the number that
  the game gives it, for example `se-0013`.
- **Metroid Prime** (USA, disc version 2). The game streams most of its music. GCSF rips its four sequenced songs and
  its sound effects. A rip names a song after its resource in the pack of the game, for example `Main_Plaza`, and an
  effect after its audio group set and its ID, for example `Scarab-0721`.
- **Metroid Prime 2: Echoes** (USA). The game streams all of its music. GCSF rips its sound effects. A rip names an
  effect as the table of names in the game does, for example `SFXfnt_x_titlemusic_lp_00L_looped`.
- **Paper Mario: The Thousand-Year Door** (USA). The game streams its music. GCSF rips its sound effects and the 143
  songs that the game plays as sounds. A rip names a sound as the table of sounds in the game does, for example
  `SFX_PRESS_START1` or `SFX_SE2_EVT_STARSTONE_GET1`. An effect that the table doesn't list is named after its set and
  its ID, for example `pmario-0051`. A song that the table doesn't list gets its name from the list of songs in the
  game, for example `SE1_TEST`.
- **Tales of Symphonia** (USA): the 111 songs that the code of the game lists. Both discs contain them. A rip names a
  song after its file, for example `bgm_t000`. GCSF rips the sound effects and voices of the game too. A rip names an
  effect after its file and its ID, for example `se-0012`.
- **Star Fox Adventures** (USA, disc version 1). The code of the game lists 99 songs, and GCSF rips them all. A rip
  names a song as that list does, for example `starfox_map`. GCSF rips the sound effects of the game too. A rip names an
  effect after its set and its ID, for example `starfoxs-0420`.
- **Star Wars Rogue Squadron II: Rogue Leader** (USA). The game contains 124 songs. Sixty of them play only streams, and
  eight more play a stream along with their notes. A rip leaves out all 68, and GCSF rips the other 56. A rip names a
  song as the music hall of the game does, for example `Dogfight_Action`. A song that the hall doesn't list is named
  after its group and ID, for example `Song_0x45-0x0A`. GCSF rips the sound effects of the game too. A rip names an
  effect after its ID, for example `RogueLeader-0048`.
- **Star Wars Rogue Squadron III: Rebel Strike** (USA). The game streams all of its music. Its rips therefore contain
  only its sound effects. A rip names an effect after its ID, for example `RebelStrike-0048`.
- **Pokémon Colosseum** (USA). The table of sounds of the game lists 78 songs. GCSF rips 77 of them. The other,
  `null_bgm_song`, has no notes. The game plays it to silence its music. A rip names a song after its file, for example
  `title_song`. GCSF rips the sound effects of the game too. A rip names an effect after its set and its ID, for example
  `snd_se_battle-0202`.
- **Resident Evil** (USA). The game comes on two discs. GCSF rips the songs of the first disc. The second disc contains
  the same sound files. The first disc contains 128 song files in `bgm/`, and five of them repeat another file. 16 of
  the others make no sound: 14 blank songs, a song without notes and the ambience of a room whose channels all start at
  a level of 0. GCSF rips the other 107. A rip names a song after its file, for example `sg00`. The code of a room
  changes the channel levels of some songs while they play. A rip plays each song alone, at the levels in its file. Six
  more rips play the mixes that rooms set for two of the ambiences, such as `sg74 ch1` and `sg7a ch0-3+5`. GCSF doesn't
  rip the sound effects of the game.
- **Resident Evil Zero** (USA). The game comes on two discs. Each disc contains the songs of its part of the game: 87 on
  the first and 137 on the second, with 59 on both. The songs include the music of 16 cutscenes and the ambiences of the
  areas of the game. One song makes no sound, and GCSF rips the others. A rip names a song as the code of the game does,
  for example `bgm_00`. The name of the library of a rip includes the number of the disc, for example
  `Resident_Evil_0_Disc_1.gcsflib`. Many cutscenes of the game stream their music. GCSF rips the sound effects of the
  game too. A rip names an effect after its archive and its ID, for example `sh00-1467`. 18 cutscenes play their sound
  effects as songs. A rip names one of those after its cutscene and its song ID, for example `dd01-0413`.
- **Pokémon Channel** (USA). The game plays each of its 38 pieces of music as a MusyX effect that plays a stereo
  recording. GCSF rips them as music, and it rips the other sound effects of the game too. A rip names a sound as the
  sound viewer of the game does, for example `SND_SUOU`. A sound that the viewer doesn't name is named after its set and
  its ID, for example `pchsound-1064`. The themes of the TV programmes play as sound effects, and a rip lists them with
  the effects.
- **Mario Party 4**, in the American, European and Japanese releases: the 76 songs that the sound test lists. A rip
  names a song as the sound test does, for example `Try Everything`, or `いろいろためそう` in the Japanese release. The 14
  streams of the sound test aren't ripped. GCSF rips the sound effects and voices of the game too. A rip names an effect
  after its number in the table of effects of the game, for example `se-0042`. The Option Room names some of the voices,
  and a rip adds such a name, for example `se-0054 Toad Happy`.
- **Nintendo Puzzle Collection** (Japan). The disc contains a menu and three games: Dr. Mario, Panel de Pon and Yoshi's
  Cookie. GCSF rips their 118 songs: 9 of the menu, 24 of Dr. Mario, 59 of Panel de Pon and 26 of Yoshi's Cookie. The
  name of a song starts with `menu`, `dr_mario`, `panepon` or `ycookie`. The songs of Dr. Mario and Panel de Pon then
  keep the names that the games give them, for example `panepon-flower_01`. The songs of the menu are named after their
  IDs, for example `menu-01`. The songs of Yoshi's Cookie are named after their files, for example `ycookie-sng05`. The
  menu and the games also contain 707 sound effects: 9 of the menu, 106 of Dr. Mario, 527 of Panel de Pon and 65 of
  Yoshi's Cookie. One effect of Dr. Mario makes no sound, and GCSF rips the others. The name of an effect is the name of
  its module, `-se-` and the ID of the effect, for example `panepon-se-0012`.
- **The NDDEMO tech demo**, a GameCube disc that never went on sale: its nine songs and its four sound effects. A rip
  names a song after its file, for example `coins`. An effect is named as the header of the disc names it, for example
  `SFXcoin_oneshot`.

## Tools

| Tool | Description |
|---|---|
| `gcsfrip [options] <input>...` | Rips each input into a `.gcsflib` and a `.minigcsf` per sound. The output goes in a folder next to the input. The folder name is the input name with `_gcsf` added. A `.gcsflib` that differs from one of the same name in the folder gets the next free name, such as `<name>_2.gcsflib`. The rips already in the folder then still play. To replace earlier rips, rip into an empty folder. An input is a GameCube disc image or an extracted disc. An extracted disc is a folder with `sys/main.dol` and `files/`, either directly or in a `DATA` folder. Dolphin extracts discs in these layouts. `--list` lists a game's sounds. `--only` and `--bgm` choose what to rip. Sounds that make no sound are listed and skipped. Streams are left out. The sequence archive can contain music that no sound of the game plays. Each such sequence gets a `.minigcsf` too, named after its file. `--sound-files` reads the sound files from another release of the game (see below). `--help` shows the rest. |
| `gcsfplay [options] <file>...` | Renders each `.minigcsf` to a WAV file next to it at 32,028 Hz. The length and fade come from the file's tags. Each file plays as an album track: 0.5 s of silence, the file from its first sound, then 1 s of silence. `--raw` renders the file as it is. `-o FILE` writes FILE instead. Its extension selects WAV or FLAC. `--length` and `--fade` override the tags. `--fade-exponent P` sets the curve of the fade, from 0.5 to 4: 1 fades linearly, and the default, 2, fades with the square of the time left in the fade. Most soundtrack albums fade that way. `--info` shows a file's tags and libraries. `--help` shows the rest. |
| `plugins/foobar2000` | foobar2000 input component: `foo_input_gcsf.component` for macOS and `foo_input_gcsf.dll` for Windows. Both are built with the foobar2000 SDK (see its [README](plugins/foobar2000/README.md)). |
| `plugins/xmplay` | XMPlay input plugin: `xmp-gcsf.dll`, a 32-bit DLL for XMPlay on Windows, built with clang-cl and the XMPlay plugin SDK (see its [README](plugins/xmplay/README.md)). |

`gcsfrip` reads disc images in every common format: ISO (`.iso`, `.gcm`), NKit (`.nkit.iso`), RVZ, WIA, GCZ, CISO and
WDF, whole or split into pieces.

Both tools accept multiple inputs and continue if one fails. A nonexistent input path stops the command before it writes
any files. For example, `gcsfrip game.rvz` writes `game_gcsf/`. `gcsfplay game_gcsf/*.minigcsf` then renders every sound
in it. `--version` shows a tool's version.

### Sound files from another release

Twilight Princess also came out on the Wii, and on the Wii U as Twilight Princess HD. Both keep their sound files in the
formats and places of the GameCube release, and the GameCube code plays them. `gcsfrip --sound-files DIR` rips a
GameCube disc with the sound files of another release in place of those on the disc:

    gcsfrip --bgm --sound-files "Twilight Princess (Wii)" "Twilight Princess (GameCube).rvz"

`DIR` contains the files of the other release. `gcsfrip` searches all of `DIR` for the start-up archive, `Z2Sound.baa`.
It reads the sequence archive (`Seqs/`) and the wave archives (`Waves/`) next to it. The code and the other files of the
game come from the GameCube disc. The disc is still required.

- **The Wii.** Extract the disc first. Dolphin extracts a disc from the Filesystem tab of the game's properties. Wiimm's
  ISO Tools extract one with `wit extract`.
- **The Wii U.** `DIR` is the title folder from a dump of the game, with `code/`, `content/` and `meta/`. The sound
  files are in `content/Audiores/`. They're the files of the Wii release, apart from `HomeMenu_0.aw`. Twilight Princess
  HD is missing that file. `gcsfrip` warns about it, and the sounds that use it play without its waves.

The Wii and the Wii U changed four pieces of music. `Z2BGM_BOSS_SNOWWOMAN_D1`, `Z2BGM_WCS_D01` and `Z2BGM_TEMP` sound
different, and `Z2BGM_RODEO` differs a little. The rest is the GameCube music. `Z2BGM_MENU_SELECT` is a sequence only on
the GameCube. The other releases play it from a stream. GCSF doesn't rip streams.

The Wind Waker HD, on the Wii U, works the same way with a disc of The Wind Waker:

    gcsfrip --sound-files "The Wind Waker HD" "The Wind Waker (GameCube).ciso"

`DIR` is the title folder from a dump of the game. In the American release, the sound files are in
`content/Cafe/US/AudioRes/JAudioRes/`. The Wind Waker HD keeps its sequences in one table, `Seqs/JaiSeq.bdt`, where the
GameCube has an archive. `gcsfrip` reads the table into the archive of the disc. The Wind Waker HD changed five
sequences. `p_bigpow`, `p_boco` and `tower` sound different. `defaultse`, the sequence of the sound effects, contains
the new effects. `baachan` only gained padding. The sound table has 98 pieces of music. The last one plays `get_perl`
again. `tower` plays louder than on the GameCube. Its volume in the table is 65 of 127. The GameCube table gives it 40.
Many waves were resampled from 16,000 Hz to 28,000 Hz. Some new ones play at 44,100 Hz.

### Ripping faster

Finding the length of each sound takes most of the time of a rip. `gcsfrip` plays every sound on the emulated GameCube
until it ends or starts to repeat, for 10 minutes at most. Music that hasn't repeated by then plays on for up to an
hour. A sound effect that loops for as long as the game keeps it going, such as wind or a machine, often plays the full
10 minutes. In a game that loads its waves as each scene requires them, such as Donkey Kong Jungle Beat, every sound
also plays once before that, to find the waves it uses. A whole rip of such a game can take a long time.

- `--bgm` rips only the music. It's the quickest way to rip a game's soundtrack.
- `--only REGEX` rips only the sounds whose labels match. For example, `--only BOSS` rips the sounds whose labels
  contain `BOSS`. `--list` shows the labels.
- `--jobs N` sets how many sounds play at once. The default is the number of CPU threads. That's the fastest setting.
- `--no-length` skips the length analysis. The rips then have no length or fade tags, and a player plays each one for
  its default length. The sounds that make no sound get rips too.

Dropping a disc image on `gcsfrip` rips every sound. To use these options, run `gcsfrip` from a command prompt or a
terminal:

    gcsfrip --bgm "Donkey Kong Jungle Beat.ciso"

### Downloads

The Releases page has `gcsfrip` and `gcsfplay` for 64-bit Windows, the foobar2000 component for 64-bit and 32-bit
foobar2000 on Windows, and the XMPlay plugin. On macOS, build GCSF from source (see "Building").

To install the XMPlay plugin, unzip `xmp-gcsf-X.Y.Z.zip` and put `xmp-gcsf.dll` in XMPlay's folder, or in a folder
inside it. Then restart XMPlay.

### Drag and drop

- **Windows:** drop files on `gcsfrip.exe` or `gcsfplay.exe`. A console window shows the results and stays open until
  you press Enter.
- **macOS:** the build also makes `gcsfrip.app` and `gcsfplay.app` in the build folder. Drop files on one, or open it
  and choose them. The app reports the results for each file and offers to open the output folders.

As above, the results go next to the files. File and folder names can use any characters. On Windows, that requires
Windows 10 version 1903 or later.

## Building

You need Git, CMake 3.20 or later, and C and C++20 compilers. GCSF builds on two libraries: xsflib, for players and
rippers of the PSF family, and ntdlib, for the hardware and formats of Nintendo's consoles. They're Git submodules in
`externals/xsflib` and `externals/ntdlib`: clone GCSF with `git clone --recurse-submodules`, or run `git submodule
update --init --recursive` in a clone. zlib, Zstandard, liblzma, bzip2 and libFLAC are downloaded and built along with
GCSF when they aren't installed.

- **Windows:** Visual Studio 2022 or later with the "Desktop development with C++" workload and its "C++ Clang tools for
  Windows" component. The workload includes CMake. The component adds clang-cl. Configure with `cmake -S . -B build -A
  x64 -T ClangCL` instead of the configure command below. Run the commands in a Developer PowerShell or Developer
  Command Prompt.
- **macOS on Apple Silicon:** the Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`).
  `brew install zstd xz flac` saves building those three.

Linux, Intel Macs, MSVC and 32-bit Windows aren't supported. The exceptions are the 32-bit foobar2000 component and the
XMPlay plugin. Build them with clang-cl, configured with `-A Win32 -T ClangCL`.

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

`CMakePresets.json` has `release` and `debug` presets for the same steps: `cmake --preset release`, `cmake --build
--preset release` and `ctest --preset release`. The presets require CMake 3.23 or later. They don't select clang-cl. On
Windows, use the commands above.

ntdlib contains the tests of its code, such as the disc readers and the DSP. They run when ntdlib is built by itself.
Its disc test builds a small GameCube disc and stores it in every format that the reader supports, with each compression
method and layout. The test then checks that every file reads back the same.

## Layout

```
src/gc/       the GameCube: its disc interface, and the machine that game mode runs
src/gcsf/     the format and the player
src/rip/      the ripper: its stages, the game profiles, a file for each sound library, the shared parts of the length
              analysis and the images of the drivers
src/cli/      gcsfrip and gcsfplay
plugins/foobar2000/  the foobar2000 component
plugins/xmplay/      the XMPlay plugin
driver/       the driver sources, the headers that they share and the script that builds them
docs/         the format specification and the release notes
tests/        ctest suites
externals/xsflib/  xsflib: the PSF container, the player framework, the tool frames and the shared parts of the build
externals/ntdlib/  ntdlib: the CPU, the DSP and the devices that the GameCube shares with the Wii, disc images in
              every format, and the JAudio sound files and length analysis
```

A driver runs on the emulated GameCube. `driver/build.py` builds one for PowerPC with ntdlib's
`tools/build_ppc_driver.py`, and requires a clang with the PowerPC target. The images of the drivers are committed in
`src/rip/`: `driver_four_swords_blob.h` for Four Swords Adventures, `driver_twilight_princess_blob.h` for Twilight
Princess, `driver_wind_waker_blob.h` for The Wind Waker, `driver_luigis_mansion_blob.h` for Luigi's Mansion,
`driver_super_mario_sunshine_blob.h` for Super Mario Sunshine, `driver_pikmin_blob.h` for Pikmin,
`driver_jungle_beat_blob.h` for Donkey Kong Jungle Beat, `driver_double_dash_blob.h` for Mario Kart: Double Dash!!,
`driver_mario_golf_blob.h` for Mario Golf: Toadstool Tour and Mario Power Tennis, `driver_warioware_blob.h` for
WarioWare, Inc.: Mega Party Game$!, `driver_pikmin2_blob.h` for Pikmin 2, `driver_pacman_vs_blob.h` for Pac-Man Vs.,
`driver_collectors_edition_blob.h` for The Legend of Zelda: Collector's Edition, `driver_crystal_chronicles_blob.h` for
Final Fantasy Crystal Chronicles, `driver_animal_crossing_blob.h` for Animal Crossing, Doubutsu no Mori+ and Doubutsu no
Mori e+, `driver_colosseum_bonus_blob.h` for the Pokémon Colosseum Bonus Disc, `driver_musyx_blob.h` for Metroid Prime,
Metroid Prime 2: Echoes, Paper Mario: The Thousand-Year Door, Tales of Symphonia, Star Fox Adventures, Rogue Leader,
Rebel Strike, Pokémon Colosseum, Resident Evil Zero, Nintendo Puzzle Collection and the NDDEMO tech demo,
`driver_musyx_song_controls_blob.h` for Resident Evil, `driver_musyx_upload_blob.h` for Pokémon Channel, and
`driver_mario_party_blob.h` for Mario Party 4. Building GCSF itself requires no PowerPC toolchain.

## Licence

GCSF is under the MIT licence (`LICENSE`), and so are xsflib and ntdlib. `THIRD-PARTY-NOTICES.md` lists the libraries
that GCSF uses and what a binary distribution must include. This licence doesn't cover game data. Rips contain a game's
code and sound data and are for personal use.