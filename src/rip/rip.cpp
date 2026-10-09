// SPDX-License-Identifier: MIT

#include "rip/rip.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "common/ascii.h"
#include "common/bytes.h"
#include "common/dol.h"
#include "common/sha1.h"
#include "dsp/main_memory.h"
#include "gc/system.h"
#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/driver_blob.h"
#include "rip/loop_finder.h"

namespace gcsf::rip
{
namespace
{

// The driver's parameter block (driver/driver.c): the sound's ID, its handle, then the profile from frame_retraces on.
constexpr uint32_t kParamsSoundOffset = DriverBlock::kSoundOffset;
constexpr uint32_t kParamsHandleOffset = 0x10;
constexpr uint32_t kParamsProfileOffset = 0x14;

// The length analysis: what counts as sound, and its limits. A sample louder than 8 of 32,767 (-72 dB) is sound. A
// sound plays for at most 600 s. One that stays busy without a sound for 180 s ended at its last sound. A sound that
// ends plays until 0.5 s after its last sound. A loop gets two passes and a 10 s fade.
constexpr int kSoundThreshold = 8;
constexpr long long kLimitMs = 600'000;
constexpr long long kSilenceMs = 180'000;
constexpr long long kTailMs = 500;
constexpr long long kLoopFadeMs = 10'000;

// A sound that holds loops there if it has sounded in the last second.
constexpr long long kHoldingMs = 1'000;

// The analysis renders this many frames at a time, 1 ms at the output rate.
constexpr std::size_t kAnalysisFrames = 32;

// Four Swords Adventures (USA). Its sound library is JAudio2, in the generation of Twilight Princess's.
const GameProfile kFourSwordsUsa = {
    .name = "The Legend of Zelda: Four Swords Adventures",
    .game_id = "G4SE01",
    .dol_sha1 = "d86b3d9f785566e34264035e140ef3daf3cc19f0",
    .main = 0x801344c0,
    .sound_archive = "GC4Sword_usa/AudioRes/JaiInit.baa",
    .sequence_archive = "GC4Sword_usa/AudioRes/Seqs/JaiSeqs.arc",
    .wave_folder = "GC4Sword_usa/AudioRes/Waves/",
    .frame_retraces = 1,
    .max_heaps = 2,
    .system_heap_size = 0x400000,
    .aram_audio_size = 0x800000,
    .aram_graph_size = 0x600000,
    .aram_priorities = 8 << 16 | 7 << 8 | 6,
    .audio_heap_size = 0x180000,
    .disc_heap_size = 0x800,
    .audio_object_size = 0x90,
    .vi_init = 0x8004bd78,
    .vi_wait_for_retrace = 0x8004c228,
    .get_current_thread = 0x80045214,
    .get_thread_priority = 0x8004632c,
    .create_root_heap = 0x80081454,
    .create_exp_heap = 0x800814d4,
    .create_solid_heap = 0x80084bac,
    .create_aram = 0x800790a8,
    .alloc = 0x80084370,
    .alloc_from = 0x8008440c,
    .thread_ctor = 0x800855cc,
    .become_current_heap = 0x8008382c,
    .system_heap = 0x8053a8b0,
    .game_globals = 0x80529dec,
    .audio_object_ctor = 0x8045a080,
    .create_disc_thread = 0x80401fec,
    .audio_setup = 0x80442810,
    .audio_frame = 0x80459674,
    .sound_manager_offset = 0x30,
    .section_offset = 0x44,
    .load_wave_bank = 0x8003df84,
    .section_data_offset = 0x30,
    .wave_banks_offset = 0xec,
    .wave_arc_state_offset = 0x4c,
    .sequence_vtable = 0x8049175c,
    .sequence_track_offset = 0xa8,
    .effect_vtable = 0x804916c8,
    .effect_track_offset = 0xb8,
    .wait_offset = 0x40,
    .note_wait_offset = 0x51,
    .parse_command = 0x80035184, // JASSeqParser::parse
    .jump_handler = 0x80033ec8,  // the sequence parser's command 0xC7
};

const std::vector<GameProfile> kProfiles = {kFourSwordsUsa};

std::string HexDigest(const Sha1::Digest& digest)
{
    std::string text;
    for (const uint8_t byte : digest)
    {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", byte);
        text += hex;
    }

    return text;
}

// The profile's driver parameters, in the order of the driver's parameter block.
std::vector<uint32_t> DriverParameters(const GameProfile& p)
{
    return {p.frame_retraces,       p.max_heaps,
            p.system_heap_size,     p.aram_audio_size,
            p.aram_graph_size,      p.aram_priorities,
            p.audio_heap_size,      p.disc_heap_size,
            p.audio_object_size,    p.vi_init,
            p.vi_wait_for_retrace,  p.get_current_thread,
            p.get_thread_priority,  p.create_root_heap,
            p.create_exp_heap,      p.create_solid_heap,
            p.create_aram,          p.alloc,
            p.alloc_from,           p.thread_ctor,
            p.become_current_heap,  p.system_heap,
            p.game_globals,         p.audio_object_ctor,
            p.create_disc_thread,   p.audio_setup,
            p.audio_frame,          p.sound_manager_offset,
            p.section_offset,       p.load_wave_bank,
            p.section_data_offset,  p.wave_banks_offset,
            p.wave_arc_state_offset};
}

// ASCII letters, digits, '_', '-' and '.' are kept. Everything else, including spaces, becomes '_'. Unlike
// std::isalnum, this doesn't depend on the C locale.
std::string SafeName(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-' || c == '.';
        out += keep ? c : '_';
    }

    return out;
}

// Windows reserves these device names, whatever follows the first dot of a file name.
bool ReservedOnWindows(const std::string& stem)
{
    const std::string name = AsciiLower(stem);
    const bool numbered =
        name.size() == 4 && (name.starts_with("com") || name.starts_with("lpt")) && name[3] >= '0' && name[3] <= '9';
    return name == "con" || name == "prn" || name == "aux" || name == "nul" || numbered;
}

} // namespace

const std::vector<GameProfile>& KnownProfiles()
{
    return kProfiles;
}

const GameProfile* FindProfile(const disc::GameFiles& game)
{
    const std::string sha1 = HexDigest(Sha1::Of(game.dol));
    for (const GameProfile& profile : kProfiles)
    {
        if (game.GameId() == profile.game_id && sha1 == profile.dol_sha1)
        {
            return &profile;
        }
    }

    return nullptr;
}

std::vector<jaudio::Sound> ListSounds(const std::vector<uint8_t>& sound_archive)
{
    return jaudio::ReadSounds(sound_archive);
}

bool IsMusic(const jaudio::Sound& sound)
{
    return AsciiLower(sound.section) == "bgm";
}

std::optional<std::vector<uint8_t>> BuildLibrary(const disc::GameFiles& game, const GameProfile& profile,
                                                 const Tags& tags, std::string* error)
{
    const auto fail = [error](const std::string& message) -> std::optional<std::vector<uint8_t>>
    {
        if (error)
        {
            *error = message;
        }

        return std::nullopt;
    };

    const std::optional<Dol> dol = Dol::Parse(game.dol);
    if (!dol)
    {
        return fail("main.dol isn't a DOL");
    }

    // The game's code and data, section by section.
    ProgramBuilder program;
    for (const Dol::Section& section : dol->Sections())
    {
        program.AddMemory(section.address, section.data.data(), section.data.size());
    }

    // The driver, with the game's profile in its parameters. A .minigcsf fills in the sound.
    std::vector<uint8_t> driver(std::begin(kDriverBlob), std::end(kDriverBlob));
    const std::vector<uint32_t> parameters = DriverParameters(profile);
    for (std::size_t i = 0; i < parameters.size(); i++)
    {
        StoreBe32(driver.data() + (kDriverParams - kDriverBase) + kParamsProfileOffset + i * 4, parameters[i]);
    }

    program.AddMemory(kDriverBase, driver);

    // main branches to the driver.
    uint8_t branch[4];
    StoreBe32(branch, 0x48000000 | ((kDriverEntry - profile.main) & 0x03fffffc));
    program.AddMemory(profile.main, branch, sizeof(branch));

    // The disc: its header, the start-up archive, the sequence archive and the wave archives that the start-up archive
    // names.
    std::vector<uint8_t> sound_archive;
    if (!game.read_file || !game.read_file(profile.sound_archive, sound_archive))
    {
        return fail(std::string("can't read ") + profile.sound_archive + " from the disc");
    }

    std::vector<std::string> paths = {profile.sequence_archive};
    try
    {
        for (const std::string& name : jaudio::WaveArchiveNames(sound_archive))
        {
            paths.push_back(profile.wave_folder + name);
        }
    }
    catch (const jaudio::Error& e)
    {
        return fail(std::string(profile.sound_archive) + ": " + e.what());
    }

    program.AddFile("sys/boot.bin", game.boot);
    program.AddFile(profile.sound_archive, sound_archive);
    for (const std::string& path : paths)
    {
        std::vector<uint8_t> bytes;
        if (!game.read_file(path, bytes))
        {
            return fail("can't read " + path + " from the disc");
        }

        program.AddFile(path, bytes);
    }

    ProcessDescriptor d;
    d.entry = dol->Entry();
    d.status_address = DriverBlock::kAddress + DriverBlock::kStatusOffset;

    return WritePsf(kPsfVersionGcsf, d.Serialize(), program.Data(), tags);
}

std::vector<uint8_t> BuildMini(uint32_t id, const std::string& lib_name, Tags tags)
{
    uint8_t sound[4];
    StoreBe32(sound, id);
    ProgramBuilder program;
    program.AddMemory(DriverBlock::kAddress + kParamsSoundOffset, sound, sizeof(sound));
    tags["_lib"] = lib_name;

    return WritePsf(kPsfVersionGcsf, {}, program.Data(), tags);
}

std::string FileNames::Library(const std::string& base)
{
    std::string name = SafeName(base);
    const std::size_t stem_length = std::min(name.find('.'), name.size());
    if (ReservedOnWindows(name.substr(0, stem_length)))
    {
        name.insert(stem_length, "_");
    }

    return Unique(name, ".gcsflib");
}

std::string FileNames::Mini(uint32_t index, const std::string& label)
{
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04u ", static_cast<unsigned>(index));

    return Unique(prefix + SafeName(label), ".minigcsf");
}

std::string FileNames::Unique(const std::string& stem, const std::string& extension)
{
    std::string name = stem + extension;
    for (int n = 2; !taken_.insert(AsciiLower(name)).second; n++)
    {
        name = stem + "_" + std::to_string(n) + extension;
    }

    return name;
}

struct LengthAnalyzer::Impl
{
    LoadedSet library;
    GameProfile profile;
};

LengthAnalyzer::LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile)
    : impl_(std::make_unique<Impl>())
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

    impl_->library.memory = std::move(chunks.memory);
    impl_->library.files = std::move(chunks.files);
    impl_->library.descriptor = ProcessDescriptor::Parse(psf.reserved);
    if (!impl_->library.descriptor)
    {
        throw std::runtime_error("the library has no process descriptor");
    }

    impl_->profile = profile;
}

LengthAnalyzer::~LengthAnalyzer() = default;

std::optional<Timing> LengthAnalyzer::Analyze(uint32_t id) const
{
    const GameProfile& profile = impl_->profile;
    LoadedSet set = impl_->library;
    std::vector<uint8_t> sound(4);
    StoreBe32(sound.data(), id);
    set.memory.push_back({DriverBlock::kAddress + kParamsSoundOffset, sound});

    Player player;
    if (!player.Load(std::move(set)) || !player.Start())
    {
        return std::nullopt;
    }

    gc::System& system = *player.System();
    const auto word = [&system](uint32_t address)
    {
        const uint8_t* p = system.Memory().At(ToPhysical(address), 4);
        return p != nullptr ? LoadBe32(p) : 0;
    };

    // What the sequence parser does, with the frame it happens in: the start of every command of every track, and every
    // jump. A track's sequence reader starts the track: its data's start, its position and its stack depth.
    std::vector<TrackEvent> events;
    uint64_t frames = 0;
    const auto on_watch = [&](uint32_t pc)
    {
        const ppc::State& s = system.Cpu().state_;
        const uint32_t track = s.gpr[4];
        const uint32_t position = word(track + 4);
        const uint8_t* command = system.Memory().At(ToPhysical(position), 1);
        const auto kind = pc == profile.parse_command ? TrackEvent::Kind::kCommand : TrackEvent::Kind::kJump;
        const uint32_t to = kind == TrackEvent::Kind::kJump ? word(s.gpr[5]) : 0;
        events.push_back({kind, track, position - word(track), to, word(track + 8),
                          static_cast<uint8_t>(command != nullptr ? *command : 0), frames});
    };
    system.Cpu().Watch({profile.parse_command, profile.jump_handler}, on_watch);

    // What a track waits for, from its sequence control.
    const auto read_wait = [&](uint32_t track) -> TrackWait
    {
        const uint8_t* notes = system.Memory().At(ToPhysical(track + profile.note_wait_offset), 1);
        return {static_cast<int32_t>(word(track + profile.wait_offset)), notes != nullptr && *notes != 0};
    };

    // Play until the sound ends, loops twice, holds, falls silent for good or reaches the limit.
    std::vector<int16_t> block(kAnalysisFrames * 2);
    std::optional<uint64_t> first_sound;
    uint64_t last_sound = 0;
    std::optional<LoopFinder> finder;
    std::optional<uint64_t> released; // the frame at which the game released the sound
    const auto ended = [&]() -> Timing
    {
        if (!first_sound)
        {
            return {kTailMs, 0, true};
        }

        return {FramesToMs(last_sound + 1) + kTailMs, 0, false};
    };

    while (FramesToMs(frames) < kLimitMs)
    {
        events.clear();
        if (player.Render(block.data(), kAnalysisFrames) != kAnalysisFrames)
        {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < kAnalysisFrames; i++)
        {
            if (std::abs(block[2 * i]) > kSoundThreshold || std::abs(block[2 * i + 1]) > kSoundThreshold)
            {
                last_sound = frames + i;
                first_sound = first_sound.value_or(last_sound);
            }
        }

        // The sound's root track, once the sound exists: a sequence's or an effect's, by its JAISound's vtable.
        const uint32_t handle = word(DriverBlock::kAddress + kParamsHandleOffset);
        if (handle && !finder)
        {
            const uint32_t vtable = word(handle);
            const uint32_t offset = vtable == profile.sequence_vtable ? profile.sequence_track_offset
                                    : vtable == profile.effect_vtable ? profile.effect_track_offset
                                                                      : 0;
            if (offset)
            {
                finder.emplace(handle + offset);
            }
        }

        // The tracks' loops. Loops before the first sound don't count.
        const Sounded sounded = {first_sound.has_value(), last_sound};
        for (const TrackEvent& event : events)
        {
            const LoopFinder::Verdict verdict = finder ? finder->Take(event, sounded) : LoopFinder::Verdict::kNone;
            if (verdict == LoopFinder::Verdict::kLoops)
            {
                return Timing{FramesToMs(event.frame), kLoopFadeMs, false};
            }

            if (verdict == LoopFinder::Verdict::kEnded)
            {
                return ended();
            }
        }

        frames += kAnalysisFrames;
        if (finder)
        {
            finder->TakeWaits(read_wait);
        }

        // The game releases the sound when its tracks have ended. Its notes can still sound after that: the output runs
        // about 80 ms behind the game, and a release fades on. The sound ends once 0.5 s has passed without a sound, or
        // 10 s after the release.
        if (!handle && finder && !released)
        {
            released = frames;
        }

        if (released && (FramesToMs(frames - std::max(*released, last_sound)) >= kTailMs ||
                         FramesToMs(frames - *released) >= kStillMs))
        {
            return ended();
        }

        // A sound that holds loops where it holds, if it still sounds.
        if (first_sound && finder && finder->Holds(frames))
        {
            if (FramesToMs(frames - last_sound) <= kHoldingMs)
            {
                return Timing{FramesToMs(frames), kLoopFadeMs, false};
            }

            return ended();
        }

        if (first_sound && FramesToMs(frames - last_sound) >= kSilenceMs)
        {
            return ended();
        }
    }

    if (!first_sound)
    {
        return ended();
    }

    return Timing{kLimitMs, kLoopFadeMs, false};
}

} // namespace gcsf::rip
