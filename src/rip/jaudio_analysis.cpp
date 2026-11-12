// SPDX-License-Identifier: MIT

// The length analysis of a sound of a JAudio2, JAudio1 or Jac game through ntdlib's AnalyzeGameSound, and the wave
// groups that a sound loads.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

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

// What the length analysis watches for sound `id`: the profile's `watch`, with `jaudio1` from its sound library. The
// conductor of one of Pikmin 2's AutoBgm pieces picks what the tracks play, at random. The piece never repeats. Once
// its root track idles in its polling routine, the passes of its other tracks would still look like a loop's. Its root
// track's polls therefore stay moves. They hold back every loop, and the piece plays to the analysis's limit.
ntdlib::jaudio::GameWatch WatchOf(const GameProfile& profile, uint32_t id)
{
    ntdlib::jaudio::GameWatch watch = profile.watch;
    watch.jaudio1 = profile.library == SoundLibrary::kJAudio1 || profile.library == SoundLibrary::kJac;
    const jaudio::Pikmin2Piece* piece = PieceOf(profile, id);
    if (piece &&
        (piece->kind == jaudio::Pikmin2Piece::Kind::kAuto || piece->kind == jaudio::Pikmin2Piece::Kind::kAutoFree))
    {
        watch.idle_in_calls = false;
    }

    return watch;
}

// The most levels of parents that the test of a track's sound climbs.
constexpr uint32_t kMaxTrackDepth = 16;

// The CPU as ntdlib's analysis reads it: the registers, and the watch of the game's code. The watch leaves out the
// events at the places for which `skip` returns true. The registers keep the name that ppc::Cpu gives them. The
// analysis reads them by that name.
class SkippingCpu
{
public:
    SkippingCpu(ppc::Cpu& cpu, std::function<bool(uint32_t pc)> skip)
        : state_(cpu.state_), cpu_(cpu), skip_(std::move(skip))
    {
    }

    void Watch(std::vector<uint32_t> pcs, std::function<void(uint32_t pc)> on_watch)
    {
        if (!on_watch)
        {
            cpu_.Watch(std::move(pcs), nullptr);
            return;
        }

        cpu_.Watch(std::move(pcs),
                   [this, callback = std::move(on_watch)](uint32_t pc)
                   {
                       if (!skip_(pc))
                       {
                           callback(pc);
                       }
                   });
    }

    ppc::State& state_;

private:
    ppc::Cpu& cpu_;
    std::function<bool(uint32_t pc)> skip_;
};

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

    SkippingCpu& Cpu()
    {
        return cpu_;
    }

private:
    // Whether the watched instruction at `pc` parses a track of the sound that the driver plays beside the ripped one,
    // or of a chord layer's effect. A track belongs to that sound if it or one of its parents is the sound's root
    // track. The register that holds the track is the one that ntdlib's analysis reads.
    bool OnBackgroundTrack(uint32_t pc) const
    {
        const ntdlib::jaudio::GameWatch& watch = profile_.watch;
        const bool conditional = watch.conditional_jump != 0 && pc == watch.conditional_jump;
        if (profile_.background_sound < 0 || (pc != watch.parse_command && pc != watch.jump_handler && !conditional))
        {
            return false;
        }

        const uint32_t reg = pc == watch.parse_command ? watch.command_track_register
                             : conditional             ? watch.conditional_track_register
                                                       : watch.jump_track_register;
        const uint32_t track = cpu_.state_.gpr[reg];
        const uint32_t sound = Word(DriverBlock::kAddress + DriverBlock::kListedProfileOffset +
                                    4 * static_cast<uint32_t>(profile_.background_sound));
        if (sound == 0 || Word(sound + watch.vtable_offset) != watch.sequence_vtable)
        {
            return chord_layer_ && OnEffect(track);
        }

        const uint32_t root = sound + watch.sequence_track_offset;
        for (uint32_t t = track, level = 0; t != 0 && level < kMaxTrackDepth; level++)
        {
            if (t == root)
            {
                return true;
            }

            t = Word(t + profile_.track_parent_offset);
        }

        return false;
    }

    // The root of `track`: the track that its parents lead to.
    uint32_t RootOf(uint32_t track) const
    {
        for (uint32_t level = 0; track != 0 && level < kMaxTrackDepth; level++)
        {
            const uint32_t parent = Word(track + profile_.track_parent_offset);
            if (parent == 0)
            {
                return track;
            }

            track = parent;
        }

        return 0;
    }

    // Whether `track` belongs to an effect. A chord layer's effects play beside the music that the driver reports. The
    // analysis follows the music alone.
    bool OnEffect(uint32_t track) const
    {
        const ntdlib::jaudio::GameWatch& watch = profile_.watch;
        const uint32_t root = RootOf(track);
        return root != 0 && Word(root - watch.effect_track_offset + watch.vtable_offset) == watch.effect_vtable;
    }

    AnalysisMachine machine_;
    const GameProfile& profile_;
    bool chord_layer_;
    SkippingCpu cpu_;
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
                                         std::set<jaudio::WaveUse>* found, bool lookups_sound)
{
    const uint32_t handle = DriverBlock::kAddress + DriverBlock::kHandleOffset;
    if (profile.background_sound >= 0)
    {
        JAudioMachine machine(player, profile, id);
        return ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), kLengthRules, found,
                                                lookups_sound, id);
    }

    AnalysisMachine machine(player);
    return ntdlib::jaudio::AnalyzeGameSound(machine, handle, WatchOf(profile, id), kLengthRules, found, lookups_sound,
                                            id);
}

} // namespace gcsf::rip
