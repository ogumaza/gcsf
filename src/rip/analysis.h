// SPDX-License-Identifier: MIT

// The parts of the length analysis that the files of the sound libraries share: the analysis's rules, GCSF's player as
// ntdlib's analysis reads it, a watch of the CPU for a scope, and a sound as xsflib's MeasureLength plays it.

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "ntdlib/first_sample.h"
#include "ntdlib/flipper/game_source.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/ppc/cpu.h"
#include "xsflib/rip/length.h"

#include "gc/system.h"
#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/rip.h"

namespace gcsf::rip
{

// The length analysis's rules: ntdlib's defaults (a 600 s limit, two loops and a 7 s fade) at GCSF's output rate, 1 ms
// at a time.
inline constexpr ntdlib::jaudio::GameLengthRules kLengthRules{.sample_rate = kPlayerSampleRate, .block_frames = 32};

// What the shared length analysis reads of GCSF's player (see ntdlib::jaudio::AnalyzeGameSound).
class AnalysisMachine
{
public:
    explicit AnalysisMachine(Player& player) : player_(player), system_(*player.System())
    {
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        return player_.Render(out, frames);
    }

    uint32_t Word(uint32_t address) const
    {
        return ntdlib::flipper::WordAt(system_.Memory(), address).value_or(0);
    }

    std::optional<uint8_t> Byte(uint32_t address) const
    {
        return ntdlib::flipper::ByteAt(system_.Memory(), address);
    }

    ppc::Cpu& Cpu()
    {
        return system_.Cpu();
    }

private:
    Player& player_;
    gc::System& system_;
};

// Watches `pcs` with `on_watch` while it lives (see ppc::Cpu::Watch). A class whose handler reads its members holds
// its ScopedWatch as its last member. The watch then starts after the other members and ends before them.
class ScopedWatch
{
public:
    ScopedWatch(ppc::Cpu& cpu, std::vector<uint32_t> pcs, std::function<void(uint32_t)> on_watch) : cpu_(cpu)
    {
        cpu_.Watch(std::move(pcs), std::move(on_watch));
    }

    ScopedWatch(const ScopedWatch&) = delete;
    ScopedWatch& operator=(const ScopedWatch&) = delete;

    ~ScopedWatch()
    {
        cpu_.Watch({}, nullptr);
    }

private:
    ppc::Cpu& cpu_;
};

// A MusyX, Red or HAL sound in game mode, as xsflib's MeasureLength plays it (see xsflib/rip/length.h). The adapter
// follows the sound through its sound library's state. A derived class reads that state: whether the sound plays, and
// whether it has gone back to the start of its loop. A frame is one block of the analysis's output
// (kLengthRules.block_frames). The sound also counts as playing before it first plays. Its loop has one target, 0.
// MusyX's and Red's output keeps a constant level on each channel once the sound has stopped. A sample therefore counts
// as sound when it differs by more than xsflib's kSilenceLevel from its channel's recent level. That level is a running
// average over about 50 ms. An output that rests at 0 (`rests_at_zero`) counts a sample louder than kSilenceLevel as
// sound, as the other analyses do. The sound rings while it has sounded in the last 0.5 s. The analysis therefore keeps
// the end of a release. The first sample that isn't 0 on either channel is the first sound that a player hears
// (FirstSampleMs). The adapter follows no voices and finds no steady state. A derived class that follows voices
// overrides VoiceCount, Voice, Settled, Stuck, ChangedAt and ActiveChannels.
class MeasuredSound
{
public:
    explicit MeasuredSound(Player& player, bool rests_at_zero = false) : machine_(player), rests_at_zero_(rests_at_zero)
    {
    }

    virtual ~MeasuredSound() = default;

    bool RunFrame()
    {
        int16_t block[2 * kLengthRules.block_frames];
        if (machine_.Render(block, kLengthRules.block_frames) != kLengthRules.block_frames)
        {
            return false;
        }

        first_sample_ms_ = ntdlib::FirstSampleMs(block, kPairMs);
        sounded_ = false;
        for (std::size_t i = 0; i < 2 * kLengthRules.block_frames; i++)
        {
            double& level = levels_[i % 2];
            sounded_ = sounded_ || std::abs(block[i] - (rests_at_zero_ ? 0.0 : level)) > xsflib::rip::kSilenceLevel;
            level += (block[i] - level) / kLevelSamples;
        }

        if (sounded_)
        {
            last_sound_ = frame_;
        }

        playing_ = Playing();
        started_ = started_ || playing_;
        if (Looped() && on_loop_)
        {
            on_loop_(0, 0);
        }

        ++frame_;

        return true;
    }

    bool Sounded() const
    {
        return sounded_;
    }

    // The milliseconds from the start of the last frame to its first sample that isn't 0, or nothing if every sample
    // of the frame is 0.
    std::optional<double> FirstSampleMs() const
    {
        return first_sample_ms_;
    }

    bool Ringing() const
    {
        return last_sound_ && frame_ - *last_sound_ <= kRingingFrames;
    }

    virtual std::size_t VoiceCount() const
    {
        return 0;
    }

    virtual xsflib::rip::VoicePlace Voice(std::size_t) const
    {
        return {};
    }

    bool Active() const
    {
        return playing_;
    }

    virtual bool Settled() const
    {
        return false;
    }

    virtual bool Stuck() const
    {
        return false;
    }

    bool Moving() const
    {
        return false;
    }

    virtual uint64_t ChangedAt() const
    {
        return 0;
    }

    bool Busy() const
    {
        return playing_ || (!started_ && frame_ < kStartFrames);
    }

    bool ChannelsChanging() const
    {
        return false;
    }

    virtual int ActiveChannels() const
    {
        return 0;
    }

    template <typename F>
    void OnLoop(F f)
    {
        on_loop_ = std::move(f);
    }

    // Whether the sound's other tracks let its fade start (see xsflib's MeasureLength). Its loops count as track 0's.
    bool OthersLooped(int)
    {
        return OthersDone();
    }

protected:
    // Whether the sound plays in the sound library.
    virtual bool Playing() = 0;

    // Whether the sound has gone back to the start of its loop since the last frame.
    virtual bool Looped() = 0;

    // Whether the tracks that loop apart from the sound's loops have each looped twice, or have stopped. A sound whose
    // tracks all loop together has none.
    virtual bool OthersDone()
    {
        return true;
    }

    // The word and the byte at `address` in the game's memory, or 0 where no memory is.
    uint32_t Word(uint32_t address) const
    {
        return machine_.Word(address);
    }

    uint8_t Byte(uint32_t address) const
    {
        return machine_.Byte(address).value_or(0);
    }

private:
    // The output's running average takes a sample with this weight's inverse: about 50 ms.
    static constexpr double kLevelSamples = 1600.0;

    // A pair of samples lasts this long.
    static constexpr double kPairMs = 1000.0 / kLengthRules.sample_rate;

    // The sound rings for 0.5 s after its last sound. A sound that hasn't started in 5 s never will.
    static constexpr uint64_t kRingingFrames =
        kLengthRules.ring_ms * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;
    static constexpr uint64_t kStartFrames = 5000 * kLengthRules.sample_rate / 1000 / kLengthRules.block_frames;

    AnalysisMachine machine_;
    bool rests_at_zero_;
    double levels_[2] = {0.0, 0.0};
    std::function<void(int, uint32_t)> on_loop_;
    std::optional<uint64_t> last_sound_;
    std::optional<double> first_sample_ms_;
    uint64_t frame_ = 0;
    bool sounded_ = false;
    bool started_ = false;
    bool playing_ = false;
};

// The loops of an effect whose parts each loop on their own: MusyX's voices or Red's tracks. The first part that loops
// gives the effect's loops. Another part holds the fade back until it has looped twice too.
class PartLoops
{
public:
    explicit PartLoops(std::size_t parts) : loops_(parts, 0)
    {
    }

    // Part `i` has gone back to the start of its loop.
    void Count(uint32_t i)
    {
        ++loops_[i];
        if (!first_)
        {
            first_ = i;
        }

        if (i == *first_)
        {
            looped_ = true;
        }
    }

    // Whether the first part to loop has looped since the last call.
    bool Looped()
    {
        return std::exchange(looped_, false);
    }

    // Whether part `i` holds the fade back while it plays: it isn't the first part to loop, and it hasn't looped twice.
    bool HoldsBack(uint32_t i) const
    {
        return i != first_ && loops_[i] < xsflib::rip::kLengthLoops;
    }

private:
    std::vector<int> loops_;        // each part's loops so far
    std::optional<uint32_t> first_; // the first part that looped
    bool looped_ = false;           // whether that part has looped since the last call of Looped
};

// The rules of a sound's length: two loops and a 7 s fade, or its last sound. The analysis stops at 600 s, or with
// `limit` kLong at 3,600 s. A sound at either limit gets 600 s and the fade.
inline xsflib::rip::LengthRules SoundRules(xsflib::rip::Limit limit = xsflib::rip::Limit::kDefault)
{
    xsflib::rip::LengthRules rules;
    rules.frame_ms = 1000.0 * kLengthRules.block_frames / kLengthRules.sample_rate;
    rules.max_frames = static_cast<uint64_t>(kLengthRules.limit_ms / rules.frame_ms);
    rules.silent_frames = static_cast<uint64_t>(kLengthRules.silence_ms / rules.frame_ms);
    rules.max_ms = static_cast<double>(kLengthRules.limit_ms);
    rules.fade_ms = static_cast<double>(kLengthRules.loop_fade_ms);

    return limit == xsflib::rip::Limit::kLong ? xsflib::rip::WithLongLimit(rules) : rules;
}

// The length of the sound that `sound` follows, up to `limit` (see SoundRules).
inline std::optional<Timing> MeasureSound(MeasuredSound& sound, xsflib::rip::Limit limit = xsflib::rip::Limit::kDefault)
{
    return xsflib::rip::MeasureLength(sound, SoundRules(limit));
}

} // namespace gcsf::rip
