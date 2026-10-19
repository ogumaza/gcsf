// SPDX-License-Identifier: MIT

// The ripper's stages that every game shares: a TGC's program, the drivers' images and parameters, the library's game
// code and driver, and the .minigcsf files. The sounds, the sound files and the lengths come from the file of the
// profile's sound library (rip/sound_libraries.h).

#include "rip/rip.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ntdlib/disc/compression.h"
#include "ntdlib/disc/header.h"
#include "ntdlib/dol.h"
#include "ntdlib/flipper/wave_list.h"
#include "ntdlib/jaudio/wave_system.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/text.h"

#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/driver_animal_crossing_blob.h"
#include "rip/driver_collectors_edition_blob.h"
#include "rip/driver_colosseum_bonus_blob.h"
#include "rip/driver_crystal_chronicles_blob.h"
#include "rip/driver_double_dash_blob.h"
#include "rip/driver_four_swords_blob.h"
#include "rip/driver_jungle_beat_blob.h"
#include "rip/driver_luigis_mansion_blob.h"
#include "rip/driver_mario_golf_blob.h"
#include "rip/driver_musyx_blob.h"
#include "rip/driver_pacman_vs_blob.h"
#include "rip/driver_pikmin2_blob.h"
#include "rip/driver_pikmin_blob.h"
#include "rip/driver_super_mario_sunshine_blob.h"
#include "rip/driver_twilight_princess_blob.h"
#include "rip/driver_warioware_blob.h"
#include "rip/driver_wind_waker_blob.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// A driver's image: its blob, the address it's loaded at, its entry and its parameter block. A block with `wave_list`
// holds a wave list before the game profile (DriverBlock). Its driver loads only the waves that a .minigcsf names
// (NamesWaves).
struct DriverImage
{
    std::span<const uint8_t> blob;
    uint32_t base;
    uint32_t entry;
    uint32_t params;
    bool wave_list;
};

// The image of a profile's driver, or nullopt if GCSF has no such driver yet.
std::optional<DriverImage> ImageOf(Driver driver)
{
    switch (driver)
    {
    case Driver::kFourSwords:
        return DriverImage{four_swords::kDriverBlob, four_swords::kDriverBase, four_swords::kDriverEntry,
                           four_swords::kDriverParams, /*wave_list=*/false};
    case Driver::kWindWaker:
        return DriverImage{wind_waker::kDriverBlob, wind_waker::kDriverBase, wind_waker::kDriverEntry,
                           wind_waker::kDriverParams, /*wave_list=*/true};
    case Driver::kTwilightPrincess:
        return DriverImage{twilight_princess::kDriverBlob, twilight_princess::kDriverBase,
                           twilight_princess::kDriverEntry, twilight_princess::kDriverParams, /*wave_list=*/true};
    case Driver::kMarioGolf:
        return DriverImage{mario_golf::kDriverBlob, mario_golf::kDriverBase, mario_golf::kDriverEntry,
                           mario_golf::kDriverParams, /*wave_list=*/false};
    case Driver::kWarioWare:
        return DriverImage{warioware::kDriverBlob, warioware::kDriverBase, warioware::kDriverEntry,
                           warioware::kDriverParams, /*wave_list=*/false};
    case Driver::kLuigisMansion:
        return DriverImage{luigis_mansion::kDriverBlob, luigis_mansion::kDriverBase, luigis_mansion::kDriverEntry,
                           luigis_mansion::kDriverParams, /*wave_list=*/false};
    case Driver::kSuperMarioSunshine:
        return DriverImage{super_mario_sunshine::kDriverBlob, super_mario_sunshine::kDriverBase,
                           super_mario_sunshine::kDriverEntry, super_mario_sunshine::kDriverParams, /*wave_list=*/true};
    case Driver::kPikmin:
        return DriverImage{pikmin::kDriverBlob, pikmin::kDriverBase, pikmin::kDriverEntry, pikmin::kDriverParams,
                           /*wave_list=*/false};
    case Driver::kJungleBeat:
        return DriverImage{jungle_beat::kDriverBlob, jungle_beat::kDriverBase, jungle_beat::kDriverEntry,
                           jungle_beat::kDriverParams, /*wave_list=*/true};
    case Driver::kDoubleDash:
        return DriverImage{double_dash::kDriverBlob, double_dash::kDriverBase, double_dash::kDriverEntry,
                           double_dash::kDriverParams, /*wave_list=*/false};
    case Driver::kPikmin2:
        return DriverImage{pikmin2::kDriverBlob, pikmin2::kDriverBase, pikmin2::kDriverEntry, pikmin2::kDriverParams,
                           /*wave_list=*/true};
    case Driver::kPacManVs:
        return DriverImage{pacman_vs::kDriverBlob, pacman_vs::kDriverBase, pacman_vs::kDriverEntry,
                           pacman_vs::kDriverParams, /*wave_list=*/false};
    case Driver::kCollectorsEdition:
        return DriverImage{collectors_edition::kDriverBlob, collectors_edition::kDriverBase,
                           collectors_edition::kDriverEntry, collectors_edition::kDriverParams, /*wave_list=*/false};
    case Driver::kCrystalChronicles:
        return DriverImage{crystal_chronicles::kDriverBlob, crystal_chronicles::kDriverBase,
                           crystal_chronicles::kDriverEntry, crystal_chronicles::kDriverParams, /*wave_list=*/false};
    case Driver::kAnimalCrossing:
        return DriverImage{animal_crossing::kDriverBlob, animal_crossing::kDriverBase, animal_crossing::kDriverEntry,
                           animal_crossing::kDriverParams, /*wave_list=*/false};
    case Driver::kColosseumBonus:
        return DriverImage{colosseum_bonus::kDriverBlob, colosseum_bonus::kDriverBase, colosseum_bonus::kDriverEntry,
                           colosseum_bonus::kDriverParams, /*wave_list=*/false};
    case Driver::kMusyX:
        return DriverImage{musyx::kDriverBlob, musyx::kDriverBase, musyx::kDriverEntry, musyx::kDriverParams,
                           /*wave_list=*/false};
    }

    return std::nullopt;
}

// A driver that NamesWaves reads its wave list in the words of ntdlib's WaveList. The driver block and WaveList allow
// the same number of waves.
static_assert(DriverBlock::kMaxWaves == ntdlib::flipper::kMaxWaves);

// The bytes that driver/pikmin2.c's Piece gives each file name.
constexpr std::size_t kPieceNameSize = 24;

// The profile's driver parameters, in the order of the driver's parameter block: `params`, then for a profile with
// scene pieces their number and the pieces (driver/pikmin2.c's Piece).
std::vector<uint32_t> DriverParameters(const GameProfile& p)
{
    std::vector<uint32_t> words = p.params;
    if (p.scene_pieces.empty())
    {
        return words;
    }

    words.push_back(static_cast<uint32_t>(p.scene_pieces.size()));
    for (const ScenePiece& piece : p.scene_pieces)
    {
        words.push_back(static_cast<uint32_t>(piece.kind) | uint32_t{piece.start_point} << 8 |
                        uint32_t{piece.beat_interval} << 16);
        words.insert(words.end(), piece.info.begin(), piece.info.end());
        for (const std::string_view name : {std::string_view(piece.sequence), std::string_view(piece.conductor)})
        {
            std::array<uint8_t, kPieceNameSize> bytes = {};
            std::copy_n(name.begin(), std::min(name.size(), bytes.size() - 1), bytes.begin());
            for (std::size_t i = 0; i < bytes.size(); i += 4)
            {
                words.push_back(xsflib::LoadBe32(bytes.data() + i));
            }
        }
    }

    return words;
}

// A TGC's header: its magic, and where its parts lie in it. The program's disc header comes first, then bi2.bin and
// the apploader. The file system table gives each file's offset as if the files began at kTgcTableFiles. They begin at
// kTgcFiles in the TGC. The values were read from the Pokémon Colosseum Bonus Disc's TGC. Its file system table then
// finds each file's data.
constexpr uint32_t kTgcMagic = 0xae0f38a2;
constexpr uint32_t kTgcHeader = 0x08;     // the program's disc header
constexpr uint32_t kTgcFst = 0x10;        // the file system table's offset, then its size
constexpr uint32_t kTgcDol = 0x1c;        // main.dol's offset, then its size
constexpr uint32_t kTgcFiles = 0x24;      // the files' offset in the TGC
constexpr uint32_t kTgcTableFiles = 0x34; // and in the file system table

// bi2.bin and the apploader follow the disc header. The apploader's header gives its code's size at 0x14 and its
// trailer's at 0x18.
constexpr uint32_t kBi2Size = 0x2000;
constexpr uint32_t kApploaderHeaderSize = 0x20;

// The program in a TGC, as LoadGame would give it from the program's disc (see OpenProgram). `source` names the
// image that holds the TGC. A file system table entry is 12 bytes: a word with a folder's flag in the top byte and the
// name's offset below, then a file's offset and size, or a folder's parent and the entry after its last. The first
// entry is the root folder. The names follow the entries. Throws xsflib::FormatError for a damaged TGC.
disc::GameFiles ReadTgc(std::shared_ptr<const std::vector<uint8_t>> tgc, const std::string& source)
{
    const xsflib::Reader r(*tgc, true);
    if (r.U32(0) != kTgcMagic)
    {
        throw xsflib::FormatError("it isn't a TGC");
    }

    const auto part = [&r](uint32_t offset, uint32_t size)
    {
        const std::span<const uint8_t> bytes = r.Sub(offset, size);
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    };

    disc::GameFiles game;
    game.source = "TGC in " + source;
    const uint32_t header = r.U32(kTgcHeader);
    game.boot = part(header, disc::kHeaderSize);
    game.bi2 = part(header + disc::kHeaderSize, kBi2Size);
    const uint32_t apploader = header + disc::kHeaderSize + kBi2Size;
    game.apploader = part(apploader, kApploaderHeaderSize + r.U32(apploader + 0x14) + r.U32(apploader + 0x18));
    game.dol = part(r.U32(kTgcDol), r.U32(kTgcDol + 4));
    game.fst = part(r.U32(kTgcFst), r.U32(kTgcFst + 4));
    game.region = xsflib::LoadBe32(game.bi2.data() + 0x18);

    // The files, by their paths. A folder's entries run up to its last entry.
    const xsflib::Reader fst(game.fst, true);
    const uint32_t entries = fst.U32(8);
    if (entries == 0 || uint64_t{entries} * 12 > game.fst.size())
    {
        throw xsflib::FormatError("its file system table is damaged");
    }

    const uint32_t files = r.U32(kTgcFiles);
    const uint32_t table_files = r.U32(kTgcTableFiles);
    std::map<std::string, std::pair<uint32_t, uint32_t>> places;
    std::vector<std::pair<uint32_t, std::string>> folders = {{entries, ""}};
    for (uint32_t i = 1; i < entries; i++)
    {
        while (i >= folders.back().first && folders.size() > 1)
        {
            folders.pop_back();
        }

        const uint32_t word = fst.U32(i * 12);
        const std::string name = fst.CString(entries * 12 + (word & 0xffffff), 255);
        const std::string path = folders.back().second + name;
        if (word >> 24)
        {
            folders.emplace_back(fst.U32(i * 12 + 8), path + "/");
            continue;
        }

        const uint32_t offset = fst.U32(i * 12 + 4);
        const uint32_t size = fst.U32(i * 12 + 8);
        if (offset < table_files || uint64_t{offset} - table_files + files + size > tgc->size())
        {
            throw xsflib::FormatError(path + " lies outside the TGC");
        }

        places[path] = {offset - table_files + files, size};
        game.files.push_back({path, size, i});
    }

    std::sort(game.files.begin(), game.files.end(),
              [](const disc::GameFile& a, const disc::GameFile& b) { return a.path < b.path; });
    game.read_file = [tgc, places](const std::string& path, std::vector<uint8_t>& data)
    {
        const auto place = places.find(path);
        if (place == places.end())
        {
            return false;
        }

        data.assign(tgc->begin() + place->second.first, tgc->begin() + place->second.first + place->second.second);
        return true;
    };

    return game;
}

} // namespace

std::optional<std::string> OpenProgram(disc::GameFiles& game)
{
    for (const GameProfile& profile : KnownProfiles())
    {
        if (!profile.tgc || game.GameId() != profile.game_id ||
            std::none_of(game.files.begin(), game.files.end(),
                         [&profile](const disc::GameFile& file) { return file.path == profile.tgc; }))
        {
            continue;
        }

        auto tgc = std::make_shared<std::vector<uint8_t>>();
        if (!game.read_file || !game.read_file(profile.tgc, *tgc))
        {
            return std::string("can't read ") + profile.tgc;
        }

        try
        {
            game = ReadTgc(tgc, game.source);
        }
        catch (const xsflib::FormatError& e)
        {
            return std::string(profile.tgc) + ": " + e.what();
        }

        return std::nullopt;
    }

    return std::nullopt;
}

bool HasStartUpArchive(const GameProfile& profile)
{
    return profile.library == SoundLibrary::kJAudio2 || profile.library == SoundLibrary::kJAudio1;
}

bool NamesWaves(const GameProfile& profile)
{
    const std::optional<DriverImage> image = ImageOf(profile.driver);

    return image && image->wave_list;
}

bool FindsWaves(const GameProfile& profile)
{
    return profile.watch.wave_handle_functions[0] != 0;
}

std::span<const uint8_t> DolBytes(const ntdlib::Dol& dol, uint32_t address)
{
    const std::span<const uint8_t> bytes = dol.At(address);
    if (bytes.empty())
    {
        throw xsflib::FormatError("main.dol has nothing at " + xsflib::Hex(address, 8));
    }

    return bytes;
}

std::vector<uint8_t> ReadGameFile(const disc::GameFiles& game, const std::string& path)
{
    std::vector<uint8_t> bytes;
    if (!game.read_file || !game.read_file(path, bytes))
    {
        throw xsflib::FormatError("can't read " + path);
    }

    return bytes;
}

std::string ZeroPadded(uint32_t number, std::size_t digits)
{
    std::string text = std::to_string(number);
    text.insert(0, digits > text.size() ? digits - text.size() : 0, '0');

    return text;
}

std::string TextBeforeZero(std::span<const uint8_t> bytes)
{
    return std::string(bytes.begin(), std::find(bytes.begin(), bytes.end(), uint8_t{0}));
}

std::string TerminatedText(std::span<const uint8_t> bytes, const char* error)
{
    const auto end = std::find(bytes.begin(), bytes.end(), uint8_t{0});
    if (end == bytes.end())
    {
        throw xsflib::FormatError(error);
    }

    return std::string(bytes.begin(), end);
}

namespace
{

// The sounds of a game without a start-up archive, from its code or its files (see ListGameSounds). A game with a
// start-up archive gets none here. Throws xsflib::FormatError if main.dol, a table or a file that lists the sounds
// can't be read.
std::vector<Sound> SoundsFromCodeOrFiles(const disc::GameFiles& game, const GameProfile& profile)
{
    if (profile.driver == Driver::kMusyX)
    {
        return CatalogueSounds(game, profile);
    }

    switch (profile.library)
    {
    case SoundLibrary::kMusyX:
        return MusyXSongs(game, profile);
    case SoundLibrary::kRed:
        return RedSounds(game, profile);
    case SoundLibrary::kHal:
        return HalEffects(game, profile);
    case SoundLibrary::kNintendo64:
        return N64Sounds(game, profile);
    case SoundLibrary::kJac:
        return JacPieces(game, profile);
    case SoundLibrary::kJAudio2:
    case SoundLibrary::kJAudio1:
        break;
    }

    return {};
}

} // namespace

std::optional<GameSounds> ListGameSounds(const disc::GameFiles& game, const GameProfile& profile, std::string* error)
{
    GameSounds listed;
    std::optional<std::string> problem;
    if (HasStartUpArchive(profile))
    {
        problem = ListJAudioSounds(game, profile, listed);
    }
    else
    {
        try
        {
            listed.sounds = SoundsFromCodeOrFiles(game, profile);
        }
        catch (const xsflib::FormatError& e)
        {
            problem = e.what();
        }
    }

    if (problem)
    {
        if (error)
        {
            *error = std::move(*problem);
        }

        return std::nullopt;
    }

    return listed;
}

namespace
{

// The part of a game's code and data that main.dol holds packed at `address` (see GameProfile::packed_module),
// unpacked. Throws xsflib::FormatError if it's damaged.
std::vector<uint8_t> UnpackModule(const ntdlib::Dol& dol, uint32_t address)
{
    const std::span<const uint8_t> packed = DolBytes(dol, address);
    if (packed.size() < 8 || xsflib::LoadBe32(packed.data()) > packed.size() - 8)
    {
        throw xsflib::FormatError("main.dol's packed code runs past its section");
    }

    std::vector<uint8_t> module(xsflib::LoadBe32(packed.data() + 4));
    try
    {
        if (ntdlib::disc::Inflate(packed.subspan(8, xsflib::LoadBe32(packed.data())), module) == module.size())
        {
            return module;
        }
    }
    catch (const std::exception&)
    {
    }

    throw xsflib::FormatError("main.dol's packed code is damaged");
}

// Adds the game's code and data to the library, section by section. A game that maps part of them through the page
// table keeps that part packed (GameProfile::packed_module). Its sections go where the profile's patches map them with
// a BAT. They replace main.dol's sections that they overlap. Returns the error, or nothing.
std::optional<std::string> AddGameCode(ProgramBuilder& program, const ntdlib::Dol& dol, const GameProfile& profile)
{
    std::vector<uint8_t> module_bytes;
    std::optional<ntdlib::Dol> module;
    if (profile.packed_module)
    {
        try
        {
            module_bytes = UnpackModule(dol, profile.packed_module);
        }
        catch (const xsflib::FormatError& e)
        {
            return e.what();
        }

        module = ntdlib::Dol::Parse(module_bytes);
        if (!module)
        {
            return "main.dol's packed code isn't a DOL";
        }
    }

    const auto overlaps_module = [&](const ntdlib::Dol::Section& section)
    {
        if (!module)
        {
            return false;
        }

        const uint64_t start = section.address, end = start + section.data.size();
        for (const ntdlib::Dol::Section& part : module->Sections())
        {
            const uint64_t part_start = uint64_t{part.address} + profile.module_offset;
            if (start < part_start + part.data.size() && part_start < end)
            {
                return true;
            }
        }

        return false;
    };

    for (const ntdlib::Dol::Section& section : dol.Sections())
    {
        if (!overlaps_module(section))
        {
            program.AddMemory(section.address, section.data.data(), section.data.size());
        }
    }

    if (module)
    {
        for (const ntdlib::Dol::Section& section : module->Sections())
        {
            program.AddMemory(section.address + profile.module_offset, section.data.data(), section.data.size());
        }
    }

    return std::nullopt;
}

// Adds the driver to the library, with the game's profile in its parameters. A .minigcsf fills in the sound. For a
// driver that NamesWaves, it also fills in the waves. main branches to the driver. The profile's patches follow.
// Returns the error, or nothing.
std::optional<std::string> AddDriver(ProgramBuilder& program, const GameProfile& profile)
{
    const std::optional<DriverImage> image = ImageOf(profile.driver);
    if (!image || image->params != DriverBlock::kAddress)
    {
        return std::string("GCSF has no driver for ") + profile.name;
    }

    std::vector<uint8_t> driver(image->blob.begin(), image->blob.end());
    const std::vector<uint32_t> parameters = DriverParameters(profile);
    const uint32_t first = (image->params - image->base) +
                           (image->wave_list ? DriverBlock::kListedProfileOffset : DriverBlock::kProfileOffset);
    if (first + parameters.size() * 4 > driver.size())
    {
        return std::string("the profile for ") + profile.name + " has more parameters than its driver takes";
    }

    for (std::size_t i = 0; i < parameters.size(); i++)
    {
        xsflib::StoreBe32(driver.data() + first + i * 4, parameters[i]);
    }

    program.AddMemory(image->base, driver);

    uint8_t branch[4];
    xsflib::StoreBe32(branch, 0x48000000 | ((image->entry - profile.main) & 0x03fffffc));
    program.AddMemory(profile.main, branch, sizeof(branch));
    for (const auto& [address, word] : profile.patches)
    {
        uint8_t bytes[4];
        xsflib::StoreBe32(bytes, word);
        program.AddMemory(address, bytes, sizeof(bytes));
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> AddDiscFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                        const std::vector<std::string>& paths)
{
    for (const std::string& path : paths)
    {
        std::vector<uint8_t> bytes;
        if (!game.read_file || !game.read_file(path, bytes))
        {
            return "can't read " + path;
        }

        program.AddFile(path, bytes);
    }

    return std::nullopt;
}

namespace
{

// Adds the disc's header to the library, then the files that the game's sound library reads. Returns the error, or
// nothing.
std::optional<std::string> AddSoundFiles(ProgramBuilder& program, const disc::GameFiles& game,
                                         const GameProfile& profile, const std::vector<std::string>* wave_archives)
{
    program.AddFile(kDiscHeaderPath, game.boot);

    if (profile.driver == Driver::kMusyX)
    {
        return AddMusyXCatalogue(program, game, profile);
    }

    switch (profile.library)
    {
    case SoundLibrary::kMusyX:
        return AddMusyXFiles(program, game, profile);
    case SoundLibrary::kRed:
        return AddRedFiles(program, game, profile);
    case SoundLibrary::kHal:
        return AddHalFiles(program, game, profile);
    case SoundLibrary::kNintendo64:
        return AddN64Files(program, game, profile);
    case SoundLibrary::kJac:
        return AddJacFiles(program, game, profile);
    case SoundLibrary::kJAudio2:
    case SoundLibrary::kJAudio1:
        break;
    }

    return AddJAudioFiles(program, game, profile, wave_archives);
}

} // namespace

std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error,
                                                 const std::vector<std::string>* wave_archives)
{
    const auto fail = [error](const std::string& message) -> std::optional<std::vector<uint8_t>>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    const std::optional<ntdlib::Dol> dol = ntdlib::Dol::Parse(game.dol);
    if (!dol)
    {
        return fail("main.dol isn't a DOL");
    }

    ProgramBuilder program;
    if (const std::optional<std::string> problem = AddGameCode(program, *dol, profile))
    {
        return fail(*problem);
    }

    if (const std::optional<std::string> problem = AddDriver(program, profile))
    {
        return fail(*problem);
    }

    if (const std::optional<std::string> problem = AddSoundFiles(program, game, profile, wave_archives))
    {
        return fail(*problem);
    }

    ProcessDescriptor d;
    d.entry = dol->Entry();
    d.status_address = DriverBlock::kAddress + DriverBlock::kStatusOffset;

    return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
}

namespace
{

// The chunks of a .minigcsf (see BuildMini): the sound in the driver block, the waves after it if `waves` isn't null,
// and `file` if it isn't null.
ProgramChunks MiniChunks(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves)
{
    ProgramChunks chunks;
    std::vector<uint8_t> sound(4);
    xsflib::StoreBe32(sound.data(), id);
    chunks.memory.push_back({DriverBlock::kAddress + DriverBlock::kSoundOffset, std::move(sound)});
    if (waves)
    {
        chunks.memory.push_back(
            {DriverBlock::kAddress + DriverBlock::kWaveListOffset, ntdlib::flipper::WaveList(*waves)});
    }

    if (file)
    {
        chunks.files.push_back(*file);
    }

    return chunks;
}

} // namespace

std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags, const FileChunk* file,
                               const std::vector<uint32_t>* waves)
{
    const ProgramChunks chunks = MiniChunks(id, file, waves);
    ProgramBuilder program;
    for (const MemChunk& chunk : chunks.memory)
    {
        program.AddMemory(chunk.address, chunk.data);
    }

    for (const FileChunk& chunk : chunks.files)
    {
        program.AddFile(chunk.path, chunk.data);
    }

    tags["_lib"] = lib_name;

    return WritePsf(kPsfVersionGcsf, {}, program.Data(), tags);
}

LoadedSet LibrarySet(const std::vector<uint8_t>& library)
{
    PsfFile psf;
    if (auto err = ParsePsf(library, psf))
    {
        throw std::runtime_error("the library can't be read: " + *err);
    }

    ProgramChunks chunks;
    if (auto err = ParseProgram(psf.program, chunks))
    {
        throw std::runtime_error("the library can't be read: " + *err);
    }

    LoadedSet set;
    set.memory = std::move(chunks.memory);
    set.files = std::move(chunks.files);
    set.descriptor = ProcessDescriptor::Parse(psf.reserved);
    if (!set.descriptor)
    {
        throw std::runtime_error("the library has no process descriptor");
    }

    return set;
}

LoadedSet SoundSet(LoadedSet library, uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves)
{
    ProgramChunks mini = MiniChunks(id, file, waves);
    library.memory.insert(library.memory.end(), std::make_move_iterator(mini.memory.begin()),
                          std::make_move_iterator(mini.memory.end()));
    library.files.insert(library.files.end(), std::make_move_iterator(mini.files.begin()),
                         std::make_move_iterator(mini.files.end()));

    return library;
}

struct LengthAnalyzer::Impl
{
    LoadedSet library;
    GameProfile profile;
};

LengthAnalyzer::LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile)
    : impl_(std::make_unique<Impl>())
{
    impl_->library = LibrarySet(library);
    impl_->profile = profile;
}

LengthAnalyzer::~LengthAnalyzer() = default;

std::optional<Timing> LengthAnalyzer::Analyze(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                                              std::vector<jaudio::WaveUse>* played) const
{
    std::set<jaudio::WaveUse> found;
    std::optional<Timing> timing = Play(id, file, waves, played ? &found : nullptr, false);
    if (played)
    {
        played->assign(found.begin(), found.end());
    }

    return timing;
}

std::optional<std::vector<jaudio::WaveUse>> LengthAnalyzer::FindWaves(uint32_t id, const FileChunk* file) const
{
    std::set<jaudio::WaveUse> found;
    if (!Play(id, file, nullptr, &found, true))
    {
        return std::nullopt;
    }

    return std::vector<jaudio::WaveUse>(found.begin(), found.end());
}

std::optional<std::vector<uint32_t>> LengthAnalyzer::FindWaveGroups(uint32_t id, const FileChunk* file,
                                                                    const std::vector<jaudio::WaveSystem>& systems,
                                                                    std::size_t* group_count) const
{
    std::optional<std::vector<uint32_t>> groups = GameWaveGroups(impl_->profile, id);
    if (!groups)
    {
        const std::optional<std::vector<jaudio::WaveUse>> waves = FindWaves(id, file);
        if (!waves)
        {
            return std::nullopt;
        }

        groups = jaudio::WaveGroups(systems, *waves);
    }

    if (group_count)
    {
        *group_count = groups->size();
    }

    if (groups->size() > DriverBlock::kMaxWaves)
    {
        groups->resize(DriverBlock::kMaxWaves);
    }

    return groups;
}

std::optional<Timing> LengthAnalyzer::Play(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                                           std::set<jaudio::WaveUse>* found, bool lookups_sound) const
{
    const GameProfile& profile = impl_->profile;
    Player player;
    if (!player.Load(SoundSet(impl_->library, id, file, waves)) || !player.Start())
    {
        return std::nullopt;
    }

    switch (profile.library)
    {
    case SoundLibrary::kMusyX:
        return MeasureMusyXSound(player, profile, impl_->library, id);
    case SoundLibrary::kRed:
        return MeasureRedSound(player, profile, id);
    case SoundLibrary::kNintendo64:
        return MeasureN64Sound(player, profile, id);
    case SoundLibrary::kHal:
        return MeasureHalEffect(player);
    case SoundLibrary::kJAudio2:
    case SoundLibrary::kJAudio1:
    case SoundLibrary::kJac:
        break;
    }

    return MeasureJAudioSound(player, profile, id, found, lookups_sound);
}

} // namespace gcsf::rip
