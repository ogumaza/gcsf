// SPDX-License-Identifier: MIT

// GCSF player core. Loads a .gcsflib/.minigcsf set and produces its audio. It runs the game's code, including its sound
// code and OS, on an emulated GameCube, with the game's DSP program on the emulated DSP. Portable C++ with no platform
// audio or UI. The CLI renderer and the foobar2000 component use it.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/player/player.h"

#include "gcsf/format.h"

namespace ntdlib::flipper
{

class FileDisc;

} // namespace ntdlib::flipper

namespace gcsf
{
namespace gc
{

class System;

} // namespace gc

class Player : public xsflib::player::PlayerBase<Player, LoadedSet>
{
public:
    // The rate players report, the audio interface's DMA rate rounded down: 32,028 Hz.
    static constexpr int kSampleRate = kPlayerSampleRate;

    // A snapshot of the player between emulation steps, with the audio that the player has rendered but not returned
    // yet. Unchanged memory pages are shared with the snapshot taken before it. The snapshot points at this player's
    // emulation and can only be restored to this player before the next Start.
    struct SourceSnapshot;
    using Snapshot = xsflib::player::Snapshot<SourceSnapshot>;

    Player();
    ~Player();

    // Loading, Start, Render, the state, the error, the tags, LengthMs, FadeMs, Volume, Save and Restore come from
    // PlayerBase.

    // The emulated GameCube while the player plays, for tools that follow the game, such as the ripper's length
    // analysis. Null before Start.
    gc::System* System()
    {
        return system_.get();
    }

private:
    friend class xsflib::player::PlayerBase<Player, LoadedSet>;

    // The error for a set without a process descriptor, and the starts of the messages for a start and a step that
    // threw.
    static constexpr const char* kNotPlayable = "no process descriptor";
    static constexpr const char* kStartFailure = "emulation failed: ";
    static constexpr const char* kStepFailure = "emulation failed: ";

    static std::optional<std::string> LoadFile(const std::string& path, const FileReader& reader, Set& set)
    {
        return LoadSet(path, reader, set);
    }

    void Unload();

    bool Playable() const
    {
        return set_.descriptor.has_value();
    }

    // Boots the emulated GameCube and runs it until the sound starts (the silence before is skipped).
    bool StartSource();
    bool RunStep();

    // The driver reports status 2 once the sound has finished.
    bool Finished() const
    {
        return DriverStatus() == 2;
    }

    std::vector<int16_t>& Output()
    {
        return output_;
    }

    uint32_t DriverStatus() const;
    uint64_t EmulatedCycles() const;
    std::shared_ptr<const SourceSnapshot> SaveSource(const SourceSnapshot* previous) const;
    bool CanRestore(const SourceSnapshot* source) const;
    void RestoreSource(const SourceSnapshot& source);

    std::unique_ptr<ntdlib::flipper::FileDisc> disc_; // declared before the system that reads it
    std::unique_ptr<gc::System> system_;
    std::vector<int16_t> output_; // the samples that the emulation has produced, two per frame
};

} // namespace gcsf
