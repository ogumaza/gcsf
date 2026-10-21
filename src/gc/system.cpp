// SPDX-License-Identifier: MIT

#include "gc/system.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "ntdlib/ppc/cpu.h"
#include "ntdlib/types.h"

#include "gc/di.h"

namespace gcsf::gc
{
namespace
{

// The top of the OS's arena. GCSF's driver lies above it (at 0x81700000), and the disc's file system table above that.
constexpr uint32_t kArenaTop = 0x81700000;

// The console type that the SDK's OSGetConsoleType reports for a retail console.
constexpr uint32_t kRetailConsole = 3;

// The hardware page at physical 0x0C000000, and the graphics FIFO's block in it. MachineBase gives the offsets of the
// blocks that the GameCube shares with the Wii.
constexpr uint32_t kHardwareBlock = 0x0c000000;
constexpr uint32_t kGraphicsFifo = 0x8000;
constexpr uint32_t kGraphicsFifoEnd = 0x9000;

// The processor interface's interrupt causes: DI (2), EXI (4), DSP (6), VI (8), and the reset switch's state (16: not
// pressed).
constexpr uint32_t kCauseDi = 1u << 2;
constexpr uint32_t kCauseExi = 1u << 4;
constexpr uint32_t kCauseDsp = 1u << 6;
constexpr uint32_t kCauseVi = 1u << 8;
constexpr uint32_t kResetSwitchUp = 1u << 16;

// The video interface: an NTSC field every 1001/60000 s, and the display interrupt registers. Each register's high half
// holds the interrupt's flag (bit 15) and its enable (bit 12).
constexpr uint64_t kFieldCycles = uint64_t{System::kCpuClock} * 1001 / 60000;
constexpr uint32_t kDisplayInterrupt0 = 0x30;
constexpr uint32_t kDisplayInterrupt1 = 0x34;
constexpr uint32_t kDisplayInterruptCount = 4;
constexpr uint32_t kDisplayInterruptFlag = 0x8000;
constexpr uint32_t kDisplayInterruptEnable = 0x1000;

} // namespace

System::System(Disc& disc) : disc_(disc)
{
    vi_next_field_ = kFieldCycles;
}

void System::Start(uint32_t entry)
{
    // The part of the boot that the GameCube shares with the Wii, then the CPU (see MachineBase).
    LoadBootInfo({1, kArenaTop, kRetailConsole, kBusClock, kCpuClock});
    StartCpu(entry);
}

System::Snapshot System::Save(const Snapshot* previous) const
{
    return {SaveDevices(previous ? &previous->devices : nullptr), disc_interface_.Save(), *this};
}

void System::Restore(const Snapshot& snapshot)
{
    RestoreDevices(snapshot.devices);
    disc_interface_.Restore(snapshot.disc_interface);
    SystemRegisters::operator=(snapshot.registers);
    UpdateInterrupt();
}

uint64_t System::NextEvent() const
{
    return std::min({dsp_interface_.NextEvent(), disc_interface_.NextEvent(), vi_next_field_});
}

void System::Advance()
{
    const uint64_t now = cpu_.Cycles();
    dsp_interface_.Advance(now);
    disc_interface_.Advance(now);
    while (now >= vi_next_field_)
    {
        Field();
        vi_next_field_ += kFieldCycles;
    }

    UpdateInterrupt();
}

void System::Field()
{
    const uint32_t reg = (vi_fields_++ % 2) ? kDisplayInterrupt0 : kDisplayInterrupt1;
    const uint32_t value = vi_.Read(reg, 2);
    if (value & kDisplayInterruptEnable)
    {
        vi_.Write(reg, 2, value | kDisplayInterruptFlag);
    }
}

bool System::DisplayInterruptPending() const
{
    for (uint32_t i = 0; i < kDisplayInterruptCount; i++)
    {
        const uint32_t value = vi_.Read(kDisplayInterrupt0 + i * 4, 2);
        if ((value & kDisplayInterruptFlag) && (value & kDisplayInterruptEnable))
        {
            return true;
        }
    }

    return false;
}

bool System::ReadIo(ntdlib::PAddr address, int size, uint32_t& value)
{
    if ((address & 0xffff0000) != kHardwareBlock)
    {
        return false;
    }

    // MachineBase handles the blocks that the GameCube shares with the Wii. The disc interface and the graphics FIFO
    // are handled here.
    const uint32_t offset = address & 0xffff;
    value = 0;
    std::optional<bool> known = ReadShared(offset, size, value);
    if (!known && offset >= kDiscInterface && offset < kSerialInterface)
    {
        // 32-bit registers. A narrower read takes part of one.
        const uint32_t word = disc_interface_.Read(offset & 0x3fc);
        value = size == 4 ? word : (word >> (8 * (4 - size - (offset & 3)))) & ((1u << (8 * size)) - 1);
        known = size == 4;
    }
    else if (!known && offset >= kGraphicsFifo && offset < kGraphicsFifoEnd)
    {
        known = false;
    }
    else if (!known)
    {
        return false;
    }

    if (!*known)
    {
        ReportUnknown(address, size, false, 0);
    }

    return true;
}

bool System::WriteIo(ntdlib::PAddr address, int size, uint32_t value)
{
    if ((address & 0xffff0000) != kHardwareBlock)
    {
        return false;
    }

    const uint32_t offset = address & 0xffff;
    std::optional<bool> known = WriteShared(offset, size, value);
    if (!known && offset >= kDiscInterface && offset < kSerialInterface)
    {
        // The SDK writes these registers whole. A narrower write is reported and ignored.
        known = size == 4;
        if (*known)
        {
            disc_interface_.Write(offset & 0x3fc, value, cpu_.Cycles());
        }
    }
    else if (!known && offset >= kGraphicsFifo && offset < kGraphicsFifoEnd)
    {
        known = false;
    }
    else if (!known)
    {
        return false;
    }

    FinishWrite(address, size, value, *known);

    return true;
}

uint32_t System::InterruptCauses() const
{
    uint32_t causes = kResetSwitchUp;
    causes |= disc_interface_.InterruptPending() ? kCauseDi : 0;
    causes |= exi_.InterruptPending() ? kCauseExi : 0;
    causes |= dsp_interface_.InterruptPending() ? kCauseDsp : 0;
    causes |= DisplayInterruptPending() ? kCauseVi : 0;

    return causes;
}

} // namespace gcsf::gc
