// SPDX-License-Identifier: MIT

// GCSF player core (see player.h).

#include "gcsf/player.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ntdlib/disc/header.h"
#include "ntdlib/flipper/file_disc.h"
#include "ntdlib/flipper/game_source.h"
#include "ntdlib/ppc/cpu.h"
#include "xsflib/bytes.h"
#include "xsflib/player/player.h"
#include "xsflib/psf/psf.h"

#include "gc/memory.h"
#include "gc/system.h"
#include "gcsf/format.h"

namespace gcsf
{
namespace
{

// Emulated time run per step while producing output: 1 ms of CPU cycles.
constexpr uint64_t kStepCycles = gc::System::kCpuClock / 1000;

// Longest time allowed from boot until the driver reports that the sound plays.
constexpr uint64_t kBootTimeout = uint64_t{10} * gc::System::kCpuClock;

// Writes the ports that the gcsf_var tag in `tags` sets into the port list of the driver in `memory` (PortList). A tag
// that doesn't parse sets no port. Neither does a driver without a list.
void SetPorts(MainMemory& memory, const Tags& tags)
{
    const auto tag = tags.find(kVariablesTag);
    if (tag == tags.end())
    {
        return;
    }

    const std::optional<xsflib::psf::Variables> ports = PortList::Parse(tag->second);
    const uint8_t* pointer = memory.At(ToPhysical(PortList::kPointer), 4);
    if (!ports || pointer == nullptr || xsflib::LoadBe32(pointer) == 0)
    {
        return;
    }

    const std::vector<uint8_t> list = PortList::Data(*ports);
    uint8_t* to = memory.At(ToPhysical(xsflib::LoadBe32(pointer)), list.size());
    if (to != nullptr)
    {
        std::copy(list.begin(), list.end(), to);
    }
}

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
        return xsflib::player::kDriverPlaying;
    }

    return ntdlib::flipper::DriverStatus(system_->Memory(), set_.descriptor ? set_.descriptor->status_address : 0);
}

bool Player::StartSource()
{
    // The disc: its header and the set's files.
    system_.reset();
    disc_ = ntdlib::flipper::DiscFromFiles(set_.files, kDiscHeaderPath, ntdlib::disc::kGameCubeMagicOffset,
                                           ntdlib::disc::kGameCubeMagic, 1);
    system_ = std::make_unique<gc::System>(*disc_);

    // The set's memory and the ports that its tag sets, then the boot.
    if (!ntdlib::flipper::LoadMemChunks(system_->Memory(), set_.memory))
    {
        return Fail("a MEM chunk lies outside the GameCube's main memory");
    }

    SetPorts(system_->Memory(), set_.tags);

    const ProcessDescriptor& d = *set_.descriptor;
    system_->Start(d.entry);
    output_.clear();
    system_->DspRegisters().on_audio = [this](std::span<const uint8_t, 32> block)
    {
        ntdlib::flipper::AppendAudioBlock(output_, block);
    };

    if (prepare_game_)
    {
        prepare_game_(*system_);
    }

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
