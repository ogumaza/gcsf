// SPDX-License-Identifier: MIT

// The length analysis of a game's sounds (LengthAnalyzer): the library's set, the sound's set, and the analysis of the
// profile's sound library.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "ntdlib/flipper/z2_params.h"
#include "ntdlib/jaudio/wave_system.h"
#include "xsflib/psf/psf.h"
#include "xsflib/rip/length.h"

#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

struct LengthAnalyzer::Impl
{
    LoadedSet library;
    GameProfile profile;
};

namespace
{

// Whether the sound that `player` has loaded stops the emulated machine with an error before the sound makes a sound,
// once the sound has started. Plays the sound again from its start until the machine stops, the sound makes a sound,
// or the analysis's limit passes. The emulation runs the same way each time.
bool StopsBeforeSound(Player& player)
{
    if (!player.Start())
    {
        return false;
    }

    std::vector<int16_t> block(2 * kLengthRules.block_frames);
    for (uint64_t frames = 0; frames < kLengthRules.limit_ms * kLengthRules.sample_rate / 1000;
         frames += kLengthRules.block_frames)
    {
        const std::size_t rendered = player.Render(block.data(), kLengthRules.block_frames);
        for (std::size_t i = 0; i < rendered; i++)
        {
            if (xsflib::rip::Audible(std::span<const int16_t>(&block[2 * i], 2)))
            {
                return false;
            }
        }

        if (rendered != kLengthRules.block_frames)
        {
            return player.GetState() == Player::State::kError;
        }
    }

    return false;
}

// Measures sound `id` of `library` while `player` plays it, by the analysis of the profile's sound library. `found` and
// `lookups_sound` are as in LengthAnalyzer::Play. Returns nullopt when the analysis fails.
std::optional<Timing> Measure(Player& player, const GameProfile& profile, const LoadedSet& library, uint32_t id,
                              std::set<jaudio::WaveUse>* found, bool lookups_sound)
{
    switch (profile.library)
    {
    case SoundLibrary::kMusyX:
        return MeasureMusyXSound(player, profile, library, id);
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

} // namespace

LengthAnalyzer::LengthAnalyzer(const std::vector<uint8_t>& library, const GameProfile& profile)
    : impl_(std::make_unique<Impl>())
{
    impl_->library = LibrarySet(library);
    impl_->profile = profile;
}

LengthAnalyzer::~LengthAnalyzer() = default;

std::optional<Timing> LengthAnalyzer::Analyze(uint32_t id, const FileChunk* file, const std::vector<uint32_t>* waves,
                                              std::vector<jaudio::WaveUse>* played, std::string* stop,
                                              const xsflib::psf::Variables* ports,
                                              const ntdlib::flipper::z2::PieceState* state) const
{
    std::set<jaudio::WaveUse> found;
    std::optional<Timing> timing = Play(id, file, waves, played ? &found : nullptr, false, stop, ports, state);
    if (played)
    {
        played->assign(found.begin(), found.end());
    }

    return timing;
}

std::optional<std::vector<jaudio::WaveUse>> LengthAnalyzer::FindWaves(
    uint32_t id, const FileChunk* file, const xsflib::psf::Variables* ports,
    const ntdlib::flipper::z2::PieceState* state) const
{
    std::set<jaudio::WaveUse> found;
    if (!Play(id, file, nullptr, &found, true, nullptr, ports, state))
    {
        return std::nullopt;
    }

    return std::vector<jaudio::WaveUse>(found.begin(), found.end());
}

std::optional<std::vector<uint32_t>> LengthAnalyzer::FindWaveGroups(
    uint32_t id, const FileChunk* file, const std::vector<jaudio::WaveSystem>& systems, std::size_t* group_count,
    const xsflib::psf::Variables* ports, const ntdlib::flipper::z2::PieceState* state, bool* searched) const
{
    std::optional<std::vector<uint32_t>> groups = GameWaveGroups(impl_->profile, id);
    if (searched)
    {
        *searched = !groups;
    }

    if (!groups)
    {
        const std::optional<std::vector<jaudio::WaveUse>> waves = FindWaves(id, file, ports, state);
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
                                           std::set<jaudio::WaveUse>* found, bool lookups_sound, std::string* stop,
                                           const xsflib::psf::Variables* ports,
                                           const ntdlib::flipper::z2::PieceState* state) const
{
    Player player;
    if (!player.Load(SoundSet(impl_->library, id, file, waves, ports, state)) || !player.Start())
    {
        return std::nullopt;
    }

    const std::optional<Timing> timing = Measure(player, impl_->profile, impl_->library, id, found, lookups_sound);
    if (timing || player.GetState() != Player::State::kError)
    {
        return timing;
    }

    // The machine stopped while the sound played.
    const std::string error = player.Error();
    if (!StopsBeforeSound(player))
    {
        return std::nullopt;
    }

    if (stop)
    {
        *stop = error;
    }

    return Timing{.length_ms = 0, .fade_ms = 0, .silent = true};
}

} // namespace gcsf::rip
