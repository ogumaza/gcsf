// SPDX-License-Identifier: MIT

// The GameCube as game mode emulates it: Gekko running the game's code (including its OS), main memory and ARAM, the
// DSP running the game's DSP program, and the hardware that the OS and the sound code use. A Disc provides the disc's
// data.
//
// The system boots the game's main.dol from its entry point. It starts in the state that the IPL and the apploader
// leave: the OS globals in low memory, the BATs that map main memory, and the disc's file system table at the top of
// main memory.

#pragma once

#include <cstdint>

#include "ntdlib/dsp/dsp.h"
#include "ntdlib/flipper/dsp_interface.h"
#include "ntdlib/flipper/exi.h"
#include "ntdlib/flipper/machine.h"
#include "ntdlib/flipper/registers.h"
#include "ntdlib/ppc/cpu.h"
#include "ntdlib/types.h"

#include "gc/di.h"
#include "gc/memory.h"

namespace gcsf
{

namespace ppc = ntdlib::ppc;

} // namespace gcsf

namespace gcsf::gc
{

// The devices with the GameCube's memory.
using DspInterface = ntdlib::flipper::DspInterface<MainMemory>;
using Exi = ntdlib::flipper::Exi<MainMemory>;

// The registers that System models itself. A snapshot of the system copies them (see System::Save). The base class
// holds the blocks that the GameCube shares with the Wii. SystemRegisters gives it the GameCube's rates for the audio
// interface and adds the video interface's fields.
class SystemRegisters : public ntdlib::flipper::SharedRegisters
{
public:
    SystemRegisters() : SharedRegisters(ntdlib::flipper::kGameCubeAudioInterface)
    {
    }

protected:
    uint64_t vi_next_field_ = 0; // the CPU cycle of the video interface's next field
    uint32_t vi_fields_ = 0;     // the fields so far
};

class System final : public ntdlib::flipper::MachineBase<System>, private SystemRegisters
{
public:
    // The CPU's clock, and the bus clock that times the hardware.
    static constexpr uint32_t kCpuClock = 486'000'000;
    static constexpr uint32_t kBusClock = 162'000'000;

    // The whole system's state between runs: memory, the CPU, the DSP and the devices. It can only be restored to the
    // system it came from.
    struct Snapshot
    {
        ntdlib::flipper::DeviceSnapshot<MainMemory> devices;
        DiscInterface::Snapshot disc_interface;
        SystemRegisters registers;
    };

    explicit System(Disc& disc);

    // Puts the system, with the game's code in memory, in the state in which the apploader starts the game at `entry`.
    // That covers the OS globals, the disc's file system table and the CPU's registers. Throws std::runtime_error if
    // the disc's header or file system table can't be read.
    void Start(uint32_t entry);

    // Takes a snapshot between runs, and restores one. A snapshot shares its unchanged memory pages with `previous`.
    // MachineBase provides Run and on_unknown_io.
    Snapshot Save(const Snapshot* previous = nullptr) const;
    void Restore(const Snapshot& snapshot);

    ppc::Cpu& Cpu()
    {
        return cpu_;
    }

    MainMemory& Memory()
    {
        return memory_;
    }

    dsp::Dsp<MainMemory>& Dsp()
    {
        return dsp_;
    }

    DspInterface& DspRegisters()
    {
        return dsp_interface_;
    }

    // ppc::Bus. MachineBase provides RamPage.
    bool ReadIo(ntdlib::PAddr address, int size, uint32_t& value) override;
    bool WriteIo(ntdlib::PAddr address, int size, uint32_t value) override;

private:
    friend class ntdlib::flipper::MachineBase<System>;

    // The interrupt causes of the processor interface (physical 0x0C003000).
    uint32_t InterruptCauses() const;

    // The video interface's display interrupts (physical 0x0C002030-0x0C00203F). The OS sets two of them to come once a
    // frame each: DI0 at the start of the second field and DI1 at the start of the first. Every field raises the one
    // whose turn it is, if it's enabled. Its handler clears it and counts a retrace.
    void Field();
    bool DisplayInterruptPending() const;

    // The next device event, and the device events due by now.
    uint64_t NextEvent() const;
    void Advance();

    Disc& disc_;
    MainMemory memory_;
    ppc::Cpu cpu_{*this};
    dsp::Dsp<MainMemory> dsp_{memory_};
    DspInterface dsp_interface_{dsp_, memory_, cpu_, ntdlib::flipper::kGameCubeDspInterface};
    DiscInterface disc_interface_{disc_, memory_};
    Exi exi_{memory_, ntdlib::flipper::kGameCubeExi};
};

} // namespace gcsf::gc
