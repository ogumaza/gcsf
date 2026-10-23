// SPDX-License-Identifier: MIT

// The ripper's parts for each sound library, and the functions that their files share. Each sound library's file has
// a function that lists a game's sounds (for ListGameSounds), one that adds the files that the sound library reads to
// the library (for BuildLibrary), and one that measures a sound's length (for LengthAnalyzer). rip.cpp calls them by
// the profile's sound library. JAudio2 and JAudio1 games list their sounds through ListJAudioSounds.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/jaudio/wave_system.h"

#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/rip.h"

namespace gcsf::rip
{

// What the files share (rip.cpp)

// `number` in decimal, with zeros in front to make it at least `digits` digits long.
std::string ZeroPadded(uint32_t number, std::size_t digits);

// The text in `bytes` up to its first zero byte, or all of `bytes` if no zero byte ends it.
std::string TextBeforeZero(std::span<const uint8_t> bytes);

// The text in `bytes` up to its first zero byte. Throws xsflib::FormatError with the message `error` if no zero byte
// ends it.
std::string TerminatedText(std::span<const uint8_t> bytes, const char* error);

// The bytes of the game's code and data from `address` to the end of its section (ntdlib::Dol::At). Throws
// xsflib::FormatError if no section holds the address.
std::span<const uint8_t> DolBytes(const ntdlib::Dol& dol, uint32_t address);

// The game's file at `path`. Throws xsflib::FormatError if it can't be read.
std::vector<uint8_t> ReadGameFile(const disc::GameFiles& game, const std::string& path);

// Adds the disc's files at `paths` to the library, each at its path. Returns the error for a file that can't be read,
// or nothing.
std::optional<std::string> AddDiscFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                        const std::vector<std::string>& paths);

// JAudio2, JAudio1 and Jac (jaudio.cpp)

// A JAudio game's sounds into `listed` (see ListGameSounds): its start-up archive, the sounds that the archive lists
// (ListSounds) and the sequences that no sound plays (UnplayedSequences). Returns the error, or nothing.
std::optional<std::string> ListJAudioSounds(const disc::GameFiles& game, const GameProfile& profile,
                                            GameSounds& listed);

// A Jac game's pieces of music: the files of its sequence index from number 2, without the empty ones. Numbers 0 and
// 1 are the effects' and the system's sequences. A piece's sound ID is 0x80000000 | group << 24 | wave system << 16 |
// part << 8 | piece. The piece field holds the piece's number in the index. The driver loads the wave group. A piece's
// part is 0. A piece plays group 0 of the wave system with the piece's number. A piece's label is its file's name
// without the extension. A piece in parts is listed as its parts instead (GameProfile::parts). A part's label is the
// piece's label, a hyphen and the part's number. The index has a 32-byte header: "BARC", four bytes, a word, the
// number of files and the sequence archive's name. Then comes a 32-byte entry for each file: its name in up to 14
// bytes, two bytes, two words, and the file's offset and size in the archive. An empty file is a dummy. Throws
// xsflib::FormatError if main.dol isn't a DOL, or if the index lies outside the game's code or isn't a BARC.
std::vector<Sound> JacPieces(const disc::GameFiles& game, const GameProfile& profile);

// A Jac game's sound effects: those of its catalogue (GameProfile::effect_table), then the profile's other effects,
// without the ones that the profile leaves out. An effect's ID is JacEffectId's. A system's or a player's effect gives
// the number that the catalogue's row holds. An event's gives the event's type and the action. An effect that the
// actions of several events play comes once, with its first row's name. The other effects' labels are their IDs in hex.
// An effect of the profile's sounding event plays on an event of the quiet type instead, with the action of that type
// that has the same entry in the table of actions. Throws xsflib::FormatError if main.dol isn't a DOL, or if the
// catalogue or the table of actions lies outside the game's code or is damaged.
std::vector<Sound> JacEffects(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a Jac game: the bank file, the sequence archive and the wave archives that the bank file's wave
// systems name. The bank file starts with the offset and the number of its wave systems' entries, then those of its
// banks' entries. An entry gives a wave system's offset and size in the file. A size of 0 means none. A group can have
// no archive. With `sounds`, the IDs of the sounds that the library plays, an archive that the rip never loads holds
// zeros of its size. The rip loads group 0 of wave system 0, the group that each piece's ID gives, and the profile's
// groups for effects if the sounds include an effect. The disc's table of files and the places of the files on the
// disc then stay the same. Returns the error, or nothing.
std::optional<std::string> AddJacFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile,
                                       const std::vector<uint32_t>* sounds);

// The sound files of a JAudio game: the start-up archive, the sequence archive, the other sound files and the wave
// archives. The wave archives are those that `wave_archives` names, or every one that the start-up archive names if
// it's null. A JAudio1 library always has the wave archives that the sound library loads at start-up. With
// GameProfile::baac_archives, the library also has the wave archives of the "baac" block's archives that `sounds`, the
// IDs of the sounds that the library plays, load, or of all of them without `sounds`. The other files that the game's
// sound layer reads follow. Returns the error, or nothing.
std::optional<std::string> AddJAudioFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                          const GameProfile& profile, const std::vector<std::string>* wave_archives,
                                          const std::vector<uint32_t>* sounds);

// The wave groups that the game loads for sound `id`, each (system << 16) | group, if the profile gives them. The
// profile gives the groups of Pikmin 2's pieces that the game plays.
std::optional<std::vector<uint32_t>> GameWaveGroups(const GameProfile& profile, uint32_t id);

// The length of a JAudio or Jac sound that the driver has started, through ntdlib's AnalyzeGameSound with the
// profile's watch (see LengthAnalyzer::Analyze). `found` and `lookups_sound` are as in LengthAnalyzer::Play.
std::optional<Timing> MeasureJAudioSound(Player& player, const GameProfile& profile, uint32_t id,
                                         std::set<jaudio::WaveUse>* found, bool lookups_sound);

// MusyX (musyx.cpp)

// A MusyX game's songs, from the tables in its main.dol: the songs, then the jingles (SongEntries). A song's ID holds
// its table in the top byte (0 for the songs, 1 for the jingles) and its number in the table below. Its label is its
// file's name without the extension. A game whose code names its songs (GameProfile::named_songs) lists them in the
// table's order instead (NamedSongs). The table then gives each song's ID, and a song's label is its name. The effects
// that such a game's play function starts follow the songs (PlayedEffects).
std::vector<Sound> MusyXSongs(const disc::GameFiles& game, const GameProfile& profile);

// The sounds of a game for driver/musyx.c: those of its catalogue (GatherMusyX), in order. A sound's ID is its number
// there. A song that the profile's `musyx_streamed_songs` lists is a stream too.
std::vector<Sound> CatalogueSounds(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a game for driver/musyx.c: the catalogue, and the files of its groups and songs. With `sounds`,
// the numbers of the sounds that the library plays, a file set or a song that none of them uses (FilesInUse) holds
// zeros of its size. The driver never opens it for those sounds. The disc's table of files and the places of the files
// on the disc then stay the same. Returns the error, or nothing.
std::optional<std::string> AddMusyXCatalogue(ProgramBuilder& program, const disc::GameFiles& game,
                                             const GameProfile& profile, const std::vector<uint32_t>* sounds);

// The sound files of another MusyX game: the sound banks and every song's file. Returns the error, or nothing.
std::optional<std::string> AddMusyXFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                         const GameProfile& profile);

// The length of a MusyX sound that the driver has started: a MusyXSong's, or a MusyXEffect's for an effect of the
// catalogue in `library` or a sound ID with the profile's `musyx_effect_bit`. Returns nullopt for an effect of a
// profile that gives no voices.
std::optional<Timing> MeasureMusyXSound(Player& player, const GameProfile& profile, const LoadedSet& library,
                                        uint32_t id);

// Hudson's sound manager over MusyX (msm.cpp)

// The sounds of a game for driver/mario_party.c: the songs in the order of its sound test (GameProfile::sound_test),
// then the effects of the sound manager's table, without its empty entries. An entry of the sound test gives a word
// that is 0 for an empty entry, the message that names the sound, the kind of sound, the group set that the sound test
// loads, and the sound's number: big-endian words, but halfwords for the kind and the set. A message's ID holds its
// bank in the top 16 bits and its number in the bank in the low 16 bits (MessageText). Kind 1 plays a stream. Kind 28
// plays an effect. Every other kind plays a song of the sound manager's table. A song's ID holds the group set in its
// top 16 bits and the song's number in its low 16 bits. A stream's ID is 0x80000000 plus its number. A song's label is
// its message's text. An effect's ID holds the group to load in its top 16 bits, with bit 15 set, or 0xFFFF for a base
// group. Its low 16 bits hold bit 15 and the effect's number. An effect is named after its number, such as "se-0042".
// The game's list of character voices (GameProfile::sound_test_voices) names some effects. Such an effect's label adds
// the name, such as "se-0054 Toad Happy". Throws xsflib::FormatError if the module, the message file or the sound
// manager's file can't be read, or if a table or a message lies outside its file.
std::vector<Sound> MsmSounds(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a game for driver/mario_party.c: the sound manager's file of sound data, and the stream file up to
// the streams' samples (see GameProfile::stream_file). The word at 0x1C of the stream file's header gives the samples'
// offset. Returns the error, or nothing.
std::optional<std::string> AddMsmFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                       const GameProfile& profile);

// Red (red.cpp)

// A Red game's pieces of music, then its effects. The pieces are the BGM files in its music folder, by number. A
// piece's ID is its number. Its label is the file's name without the extension. The effects are those of its blocks,
// by number, then its SEP files from kRedFirstSep on, by number. An effect's ID is 0x1000000 plus the game's number of
// the effect. A block's effect is labelled with the block's file name without the extension, a hyphen and the
// effect's place in three digits. A SEP file's effect is labelled with the file's name without the extension.
std::vector<Sound> RedSounds(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a Red game: every piece of music, the blocks of effects that the game loads, the SEP files that it
// can play, and the wave banks that they name. A piece's file starts with "BGM ", then the piece's number and its
// bank's number, big-endian halves at 4 and 6. A SEP file starts with "SeSep". Its bank's number is a little-endian
// half at 0x11. RedBlockEffects reads a block. With `sounds`, the IDs of the sounds that the library plays, a file that
// none of them reads holds zeros of its size. A piece reads its file and its bank. Any effect reads the blocks and
// their banks. An effect in a SEP file also reads that file and its bank. The disc's table of files and the places of
// the files on the disc then stay the same. Returns the error, or nothing.
std::optional<std::string> AddRedFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile,
                                       const std::vector<uint32_t>* sounds);

// The length of a Red sound that the driver has started: a RedPiece's, or a RedEffect's for an effect.
std::optional<Timing> MeasureRedSound(Player& player, const GameProfile& profile, uint32_t id);

// HAL (hal.cpp)

// A HAL game's effects (see GameProfile::sound_archive): every effect of the file of scripts, group by group. An
// effect's label is its name in the game's header, or else "se", its group, a hyphen and its place in the group in
// three digits.
std::vector<Sound> HalEffects(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a game with HAL Laboratory's sound library: the effects' scripts and their samples. Returns the
// error, or nothing.
std::optional<std::string> AddHalFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                       const GameProfile& profile);

// The length of a HAL effect that the driver has started (see HalEffect).
std::optional<Timing> MeasureHalEffect(Player& player);

// The Nintendo 64's sound library (n64.cpp)

// A game's sounds with the Nintendo 64's sound library: its pieces of music, then its effects if the profile has them.
// A piece's ID gives the museum's room in bits 16 to 23, the weather in bits 8 to 15 and the game's number of the piece
// in the low byte. An effect's ID has kind 1 in its top byte, its bank in bits 8 to 15 and its place in the bank in
// the low byte.
std::vector<Sound> N64Sounds(const disc::GameFiles& game, const GameProfile& profile);

// The sound files of a game with the Nintendo 64's sound library: the file of sound data. Returns the error, or
// nothing.
std::optional<std::string> AddN64Files(ProgramBuilder& program, const disc::GameFiles& game,
                                       const GameProfile& profile);

// The length of a sound of the Nintendo 64's sound library that the driver has started (see AnalyzeN64Sound). An
// effect's ID has kind 1 in its top byte.
std::optional<Timing> MeasureN64Sound(Player& player, const GameProfile& profile, uint32_t id);

} // namespace gcsf::rip
