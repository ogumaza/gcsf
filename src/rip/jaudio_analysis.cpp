// SPDX-License-Identifier: MIT

// The length analysis of a sound of a JAudio2, JAudio1 or Jac game through ntdlib's AnalyzeGameSound, and the wave
// groups that a sound loads.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "ntdlib/jaudio/beside_tracks.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/jaudio/jungle_beat_states.h"
#include "ntdlib/jaudio/pikmin2_pieces.h"
#include "ntdlib/jaudio/wave_system.h"

#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// The piece of Pikmin 2's that sound `id` plays, or null for a sound that isn't one of them.
const jaudio::Pikmin2Piece* PieceOf(const GameProfile& profile, uint32_t id)
{
    return profile.pikmin2_pieces ? jaudio::Pikmin2PieceOf(id) : nullptr;
}

// Whether sound `id` is one of Pikmin 2's AutoBgm pieces (see WatchOf).
bool Conducted(const GameProfile& profile, uint32_t id)
{
    const jaudio::Pikmin2Piece* piece = PieceOf(profile, id);
    return piece &&
           (piece->kind == jaudio::Pikmin2Piece::Kind::kAuto || piece->kind == jaudio::Pikmin2Piece::Kind::kAutoFree);
}

// What the length analysis watches for sound `id`: the profile's `watch`, with `jaudio1` from its sound library. The
// conductor of one of Pikmin 2's AutoBgm pieces picks what the tracks play, at random. The piece never repeats. Once
// its root track idles in its polling routine, the passes of its other tracks would still look like a loop's. Its root
// track's polls therefore stay moves. They hold back every loop, and the piece plays to the analysis's limit.
ntdlib::jaudio::GameWatch WatchOf(const GameProfile& profile, uint32_t id)
{
    ntdlib::jaudio::GameWatch watch = profile.watch;
    watch.jaudio1 = profile.library == SoundLibrary::kJAudio1 || profile.library == SoundLibrary::kJac;
    if (Conducted(profile, id))
    {
        watch.idle_in_calls = false;
    }

    return watch;
}

// GCSF's player as ntdlib's analysis reads it, without the tracks of the sound that the driver plays beside the ripped
// one (GameProfile::background_sound), and without the effects of a chord layer (GameProfile::chord_layers).
class JAudioMachine
{
public:
    JAudioMachine(Player& player, const GameProfile& profile, uint32_t id)
        : machine_(player),
          profile_(profile),
          chord_layer_(!profile.chord_layers.empty() && (id & jaudio::kChordLayer) != 0),
          cpu_(player.System()->Cpu(), [this](uint32_t pc) { return OnBackgroundTrack(pc); })
    {
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        return machine_.Render(out, frames);
    }

    uint32_t Word(uint32_t address) const
    {
        return machine_.Word(address);
    }

    std::optional<uint8_t> Byte(uint32_t address) const
    {
        return machine_.Byte(address);
    }

    ntdlib::jaudio::SkippingCpu<ppc::Cpu>& Cpu()
    {
        return cpu_;
    }

private:
    // Whether the watched instruction at `pc` parses a track of the sound that the driver plays beside the ripped one,
    // or of a chord layer's effect. A chord layer's effects play beside the music that the driver reports. The analysis
    // follows the music alone.
    bool OnBackgroundTrack(uint32_t pc) const
    {
        if (profile_.background_sound < 0)
        {
            return false;
        }

        const std::optional<uint32_t> track = ntdlib::jaudio::ParsedTrack(profile_.watch, pc, cpu_.state_.gpr);
        if (!track)
        {
            return false;
        }

        const auto word = [this](uint32_t address)
        {
            return Word(address);
        };
        const uint32_t sound = Word(DriverBlock::kAddress + DriverBlock::kListedProfileOffset +
                                    4 * static_cast<uint32_t>(profile_.background_sound));

        return ntdlib::jaudio::IsTrackBeside(profile_.watch, sound, *track, profile_.track_parent_offset, chord_layer_,
                                             word);
    }

    AnalysisMachine machine_;
    const GameProfile& profile_;
    bool chord_layer_;
    ntdlib::jaudio::SkippingCpu<ppc::Cpu> cpu_;
};

} // namespace

std::optional<std::vector<uint32_t>> GameWaveGroups(const GameProfile& profile, uint32_t id)
{
    const jaudio::Pikmin2Piece* piece = PieceOf(profile, id);
    if (!piece || piece->wave_scene == jaudio::Pikmin2Piece::kFindWaves)
    {
        return std::nullopt;
    }

    // Group 0 holds the effects and stays loaded. The driver loads the scene's group.
    std::vector<uint32_t> groups = {0};
    if (piece->wave_scene != 0)
    {
        groups.push_back(piece->wave_scene);
    }

    return groups;
}

std::optional<Timing> MeasureJAudioSound(Player& player, const GameProfile& profile, uint32_t id,
                                         std::set<jaudio::WaveUse>* found, bool lookups_sound, xsflib::rip::Limit limit)
{
    // A conducted piece plays to the default limit by design.
    const bool conducted = Conducted(profile, id);
    const ntdlib::jaudio::GameLengthRules rules =
        ntdlib::jaudio::WithLimit(kLengthRules, conducted ? xsflib::rip::Limit::kDefault : limit);

    const uint32_t handle = DriverBlock::kAddress + DriverBlock::kHandleOffset;
    std::optional<Timing> timing;
    if (profile.background_sound >= 0)
    {
        JAudioMachine machine(player, profile, id);
        timing =
            ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), rules, found, lookups_sound, id);
    }
    else
    {
        AnalysisMachine machine(player);
        timing =
            ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), rules, found, lookups_sound, id);
    }

    // A conducted piece at the limit gets no warning. Its timing therefore isn't at the limit. Its tags would then
    // count the 600 s from the first sound and lose the time before it. Its length therefore runs 600 s from the first
    // sound, as the tags of a timing at the limit do (xsflib's TagsOfTiming): `length=10:00.000` and the fade of a
    // loop.
    if (timing && conducted && timing->at_limit)
    {
        timing->at_limit = false;
        timing->length_ms += timing->first_sample_ms.value_or(0);
    }

    return timing;
}

} // namespace gcsf::rip
