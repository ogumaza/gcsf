// SPDX-License-Identifier: MIT

// GCSF player core (see player.h).

#include "gcsf/player.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ntdlib/disc/header.h"
#include "ntdlib/flipper/file_disc.h"
#include "ntdlib/flipper/game_source.h"
#include "ntdlib/ppc/cpu.h"

#include "gc/system.h"

namespace gcsf
{
namespace
{

// Emulated time run per step while producing output: 1 ms of CPU cycles.
constexpr uint64_t kStepCycles = gc::System::kCpuClock / 1000;

// Longest time allowed from boot until the driver reports that the sound plays.
constexpr uint64_t kBootTimeout = uint64_t{10} * gc::System::kCpuClock;

// The disc's header, stored in the set as a FILE chunk. The disc starts with it.
constexpr char kHeaderPath[] = "sys/boot.bin";

} // namespace

// The emulation's part of a snapshot.
struct Player::SourceSnapshot
{
    gc::System::Snapshot system;
};

Player::Player() = default;

Player::~Player() = default;

void Player::Unload()
{
    system_.reset();
    disc_.reset();
}

uint32_t Player::DriverStatus() const
{
    if (!system_)
    {
        return 1;
    }

    return ntdlib::flipper::DriverStatus(system_->Memory(), set_.descriptor ? set_.descriptor->status_address : 0);
}

bool Player::StartSource()
{
    // The disc: its header and the set's files.
    system_.reset();
    disc_ = ntdlib::flipper::DiscFromFiles(set_.files, kHeaderPath, ntdlib::disc::kGameCubeMagicOffset,
                                           ntdlib::disc::kGameCubeMagic, 1);
    system_ = std::make_unique<gc::System>(*disc_);

    // The set's memory, then the boot.
    if (!ntdlib::flipper::LoadMemChunks(system_->Memory(), set_.memory))
    {
        return Fail("a MEM chunk lies outside the GameCube's main memory");
    }

    const ProcessDescriptor& d = *set_.descriptor;
    system_->Start(d.entry);
    output_.clear();
    system_->DspRegisters().on_audio = [this](std::span<const uint8_t, 32> block)
    {
        ntdlib::flipper::AppendAudioBlock(output_, block);
    };

    // Run until the driver reports that the sound plays. Discard the silence before that point.
    return RunUntilSoundPlays(kBootTimeout);
}

bool Player::RunStep()
{
    if (const auto message = ntdlib::flipper::StopMessage(system_->Run(kStepCycles)))
    {
        return Fail(*message);
    }

    return true;
}

uint64_t Player::EmulatedCycles() const
{
    return system_->Cpu().Cycles();
}

std::shared_ptr<const Player::SourceSnapshot> Player::SaveSource(const SourceSnapshot* previous) const
{
    if (!system_)
    {
        return nullptr;
    }

    return std::make_shared<const SourceSnapshot>(
        SourceSnapshot{system_->Save(previous ? &previous->system : nullptr)});
}

bool Player::CanRestore(const SourceSnapshot* source) const
{
    return system_ && source != nullptr;
}

void Player::RestoreSource(const SourceSnapshot& source)
{
    system_->Restore(source.system);
}

} // namespace gcsf
