// SPDX-License-Identifier: MIT

#include "gc/system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/bytes.h"
#include "dsp/main_memory.h"
#include "ppc/cpu.h"

namespace gcsf::gc
{
namespace
{

constexpr uint32_t kPageSize = 0x1000;

// The OS globals in low memory that the IPL and the apploader leave (the SDK's OSBootInfo), as cached addresses.
namespace low
{

constexpr uint32_t kBootMagic = 0x80000020;
constexpr uint32_t kBootVersion = 0x80000024;
constexpr uint32_t kMemorySize = 0x80000028;
constexpr uint32_t kConsoleType = 0x8000002c;
constexpr uint32_t kArenaLo = 0x80000030;
constexpr uint32_t kArenaHi = 0x80000034;
constexpr uint32_t kFst = 0x80000038;
constexpr uint32_t kFstMaxSize = 0x8000003c;
constexpr uint32_t kSimulatedMemorySize = 0x800000f0;
constexpr uint32_t kBusClock = 0x800000f8;
constexpr uint32_t kCpuClock = 0x800000fc;

} // namespace low

// The disc header's offsets: the file system table's offset and size, in bytes.
constexpr uint32_t kHeaderFstOffset = 0x424;
constexpr uint32_t kHeaderFstSize = 0x428;

// The top of the OS's arena. GCSF's driver lies above it (at 0x81700000), and the disc's file system table above that.
constexpr uint32_t kArenaTop = 0x81700000;

// The console type that the SDK's OSGetConsoleType reports for a retail console.
constexpr uint32_t kRetailConsole = 3;

// The hardware blocks, by offset from physical 0x0C000000. The command processor's block comes first, at 0.
constexpr uint32_t kHardwareBlock = 0x0c000000;
constexpr uint32_t kPixelEngine = 0x1000;
constexpr uint32_t kVideoInterface = 0x2000;
constexpr uint32_t kProcessorInterface = 0x3000;
constexpr uint32_t kMemoryInterface = 0x4000;
constexpr uint32_t kDspInterface = 0x5000;
constexpr uint32_t kDiscInterface = 0x6000;
constexpr uint32_t kSerialInterface = 0x6400;
constexpr uint32_t kExternalInterface = 0x6800;
constexpr uint32_t kAudioInterface = 0x6c00;
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
    LoadLowMemory();

    // The CPU: translation on through the BATs that map main memory cached (0x80000000) and uncached (0xc0000000),
    // floating point on, and the entry point. The uncached mapping also reaches the hardware registers.
    ppc::State& s = cpu_.state_;
    s.msr = ppc::msr::kIr | ppc::msr::kDr | ppc::msr::kRi | ppc::msr::kFp;

    const auto set_bat = [&s](int base, uint32_t upper, uint32_t lower)
    {
        s.spr[base] = upper;
        s.spr[base + 1] = lower;
    };

    set_bat(ppc::spr::kIbat0u, 0x80001fff, 0x00000002);
    set_bat(ppc::spr::kIbat0u + 8, 0x80001fff, 0x00000002);
    set_bat(ppc::spr::kIbat0u + 10, 0xc0001fff, 0x0000002a);
    cpu_.InvalidateTranslation();
    s.pc = entry;
}

void System::LoadLowMemory()
{
    const auto put32 = [this](uint32_t address, uint32_t value)
    {
        StoreBe32(memory_.At(ToPhysical(address), 4), value);
    };

    // The disc header's first 32 bytes: the game's ID, its version, and the GameCube disc magic.
    std::array<uint8_t, 0x440> header{};
    if (!disc_.Read(0, header.data(), header.size()))
    {
        throw std::runtime_error("the disc's header can't be read");
    }

    std::copy(header.begin(), header.begin() + 0x20, memory_.At(0, 0x20));

    // The file system table, at the top of main memory. The DVD library looks files up there.
    const uint32_t fst_offset = LoadBe32(&header[kHeaderFstOffset]);
    const uint32_t fst_size = LoadBe32(&header[kHeaderFstSize]);
    const uint32_t fst_address = (ToCached(kMem1Size) - fst_size) & ~0x1fu;
    std::vector<uint8_t> fst(fst_size);
    if (fst_size > 0x100000 || !disc_.Read(fst_offset, fst.data(), fst.size()))
    {
        throw std::runtime_error("the disc's file system table can't be read");
    }

    if (fst_address < kArenaTop + 0x10000)
    {
        throw std::runtime_error("the disc's file system table is too large");
    }

    std::copy(fst.begin(), fst.end(), memory_.At(ToPhysical(fst_address), fst.size()));

    put32(low::kBootMagic, 0x0d15ea5e);
    put32(low::kBootVersion, 1);
    put32(low::kMemorySize, kMem1Size);
    put32(low::kConsoleType, kRetailConsole);
    put32(low::kArenaLo, 0);
    put32(low::kArenaHi, kArenaTop);
    put32(low::kFst, fst_address);
    put32(low::kFstMaxSize, fst_size);
    put32(low::kSimulatedMemorySize, kMem1Size);
    put32(low::kBusClock, kBusClock);
    put32(low::kCpuClock, kCpuClock);
}

ppc::StopReason System::Run(uint64_t cycles)
{
    // The CPU runs up to the next device event. Then the event happens and the interrupt line is updated.
    const uint64_t end = cpu_.Cycles() + cycles;
    while (cpu_.Cycles() < end)
    {
        const uint64_t until = std::min(end, std::max(NextEvent(), cpu_.Cycles() + 1));
        const ppc::StopReason reason = cpu_.Run(until - cpu_.Cycles());
        Advance();
        if (reason != ppc::StopReason::kBudget)
        {
            return reason;
        }
    }

    return ppc::StopReason::kBudget;
}

System::Snapshot System::Save(const Snapshot* previous) const
{
    return {memory_.Save(previous ? &previous->memory : nullptr),
            cpu_.Save(),
            dsp_,
            dsp_interface_.Save(),
            disc_interface_.Save(),
            exi_.Save(),
            *this};
}

void System::Restore(const Snapshot& snapshot)
{
    memory_.Restore(snapshot.memory);
    cpu_.Restore(snapshot.cpu);
    dsp_ = snapshot.dsp;
    dsp_interface_.Restore(snapshot.dsp_interface);
    disc_interface_.Restore(snapshot.disc_interface);
    exi_.Restore(snapshot.exi);
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

uint8_t* System::RamPage(PAddr page)
{
    return memory_.At(page, kPageSize);
}

bool System::ReadIo(PAddr address, int size, uint32_t& value)
{
    if ((address & 0xffff0000) != kHardwareBlock)
    {
        return false;
    }

    const uint32_t offset = address & 0xffff;
    bool known = true;
    value = 0;
    if (offset < kPixelEngine)
    {
        value = cp_.Read(offset & 0xff, size);
        known = false;
    }
    else if (offset < kVideoInterface)
    {
        value = pe_.Read(offset & 0xff, size);
        known = false;
    }
    else if (offset < kProcessorInterface)
    {
        value = vi_.Read(offset & 0xff, size);
        known = false;
    }
    else if (offset < kMemoryInterface)
    {
        value = ReadPi(offset & 0xff);
    }
    else if (offset < kDspInterface)
    {
        value = mi_.Read(offset & 0xff, size);
        known = false;
    }
    else if (offset < kDiscInterface)
    {
        // 16-bit registers. A 32-bit read takes the high half first.
        const uint32_t reg = offset & 0x1fe;
        value = size == 4 ? uint32_t{dsp_interface_.Read(reg)} << 16 | dsp_interface_.Read(reg + 2)
                          : dsp_interface_.Read(reg);
    }
    else if (offset < kSerialInterface)
    {
        // 32-bit registers. A narrower read takes part of one.
        const uint32_t word = disc_interface_.Read(offset & 0x3fc);
        value = size == 4 ? word : (word >> (8 * (4 - size - (offset & 3)))) & ((1u << (8 * size)) - 1);
        known = size == 4;
    }
    else if (offset < kExternalInterface)
    {
        value = si_.Read(offset & 0x3ff, size);
        known = false;
    }
    else if (offset < kAudioInterface)
    {
        value = exi_.Read(offset & 0x3f);
    }
    else if (offset < kAudioInterface + 0x400)
    {
        value = ReadAi(offset & 0x1f);
    }
    else if (offset >= kGraphicsFifo && offset < kGraphicsFifoEnd)
    {
        known = false;
    }
    else
    {
        return false;
    }

    if (!known)
    {
        ReportUnknown(address, size, false, 0);
    }

    return true;
}

bool System::WriteIo(PAddr address, int size, uint32_t value)
{
    if ((address & 0xffff0000) != kHardwareBlock)
    {
        return false;
    }

    const uint32_t offset = address & 0xffff;
    bool known = true;
    if (offset < kPixelEngine)
    {
        cp_.Write(offset & 0xff, size, value);
        known = false;
    }
    else if (offset < kVideoInterface)
    {
        pe_.Write(offset & 0xff, size, value);
        known = false;
    }
    else if (offset < kProcessorInterface)
    {
        vi_.Write(offset & 0xff, size, value);
        known = false;
    }
    else if (offset < kMemoryInterface)
    {
        WritePi(offset & 0xff, value);
    }
    else if (offset < kDspInterface)
    {
        mi_.Write(offset & 0xff, size, value);
        known = false;
    }
    else if (offset < kDiscInterface)
    {
        const uint32_t reg = offset & 0x1fe;
        if (size == 4)
        {
            dsp_interface_.Write(reg, static_cast<uint16_t>(value >> 16));
            dsp_interface_.Write(reg + 2, static_cast<uint16_t>(value));
        }
        else
        {
            dsp_interface_.Write(reg, static_cast<uint16_t>(value));
        }
    }
    else if (offset < kSerialInterface)
    {
        // The SDK writes these registers whole. A narrower write is reported and ignored.
        known = size == 4;
        if (known)
        {
            disc_interface_.Write(offset & 0x3fc, value, cpu_.Cycles());
        }
    }
    else if (offset < kExternalInterface)
    {
        WriteSi(offset & 0x3ff, size, value);
        known = false;
    }
    else if (offset < kAudioInterface)
    {
        exi_.Write(offset & 0x3f, value);
    }
    else if (offset < kAudioInterface + 0x400)
    {
        WriteAi(offset & 0x1f, value);
    }
    else if (offset >= kGraphicsFifo && offset < kGraphicsFifoEnd)
    {
        known = false;
    }
    else
    {
        return false;
    }

    if (!known)
    {
        ReportUnknown(address, size, true, value);
    }

    // A write may have scheduled an event, or raised or cleared an interrupt.
    cpu_.LimitRun(NextEvent());
    UpdateInterrupt();

    return true;
}

uint32_t System::ReadPi(uint32_t offset)
{
    switch (offset)
    {
    case 0x00:
        return InterruptCauses();
    case 0x04:
        return pi_mask_;
    default:
        return pi_.Read(offset, 4);
    }
}

void System::WritePi(uint32_t offset, uint32_t value)
{
    switch (offset)
    {
    case 0x00: // the causes clear at their sources
        break;

    case 0x04:
        pi_mask_ = value;
        break;

    default:
        pi_.Write(offset, 4, value);
        ReportUnknown(kHardwareBlock + kProcessorInterface + offset, 4, true, value);
        break;
    }
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

void System::UpdateInterrupt()
{
    cpu_.SetExternalInterrupt((InterruptCauses() & pi_mask_ & 0xffff) != 0);
}

uint32_t System::ReadAi(uint32_t offset)
{
    switch (offset)
    {
    case 0x00:
        return ai_control_;

    case 0x04:
        return ai_volume_;

    case 0x08:
        AdvanceSampleCounter();
        return ai_samples_;

    case 0x0c:
        return ai_interrupt_timing_;

    default:
        return 0;
    }
}

void System::WriteAi(uint32_t offset, uint32_t value)
{
    // AICR's bits: PSTAT (0) runs the sample counter. AISFR (1) selects its rate (48 kHz when set). AIINT (3) clears
    // when written with 1. SCRESET (5) resets the counter and reads back 0. AIDFR (6) selects the DMA's rate (32 kHz
    // when set). The GameCube's rates are 108 MHz / 2,248 and 108 MHz / 3,372.
    constexpr uint32_t kPlay = 1u << 0;
    constexpr uint32_t kRate48k = 1u << 1;
    const auto period = [](uint32_t control)
    {
        return static_cast<double>(kCpuClock) * ((control & kRate48k) ? 2'248.0 : 3'372.0) / 108'000'000.0;
    };

    switch (offset)
    {
    case 0x00:
        {
            AdvanceSampleCounter();
            const uint32_t old = ai_control_;
            ai_control_ = (ai_control_ & 0x08 & ~value) | (value & ~0x28u);
            if (value & 0x20)
            {
                ai_samples_ = 0;
            }

            if ((ai_control_ & kPlay) && (!(old & kPlay) || ((ai_control_ ^ old) & kRate48k)))
            {
                ai_period_ = period(ai_control_);
                ai_next_count_ = static_cast<double>(cpu_.Cycles()) + ai_period_;
            }

            dsp_interface_.SetDmaRate32k((value & 0x40) != 0);
            break;
        }

    case 0x04:
        ai_volume_ = value;
        break;

    case 0x08:
        AdvanceSampleCounter();
        ai_samples_ = value & 0x7fffffff;
        break;

    case 0x0c:
        ai_interrupt_timing_ = value;
        break;

    default:
        break;
    }
}

void System::AdvanceSampleCounter()
{
    const auto now = static_cast<double>(cpu_.Cycles());
    if (!(ai_control_ & 1) || now < ai_next_count_)
    {
        return;
    }

    const double counts = std::floor((now - ai_next_count_) / ai_period_) + 1.0;
    ai_samples_ = (ai_samples_ + static_cast<uint32_t>(counts)) & 0x7fffffff;
    ai_next_count_ += counts * ai_period_;
}

void System::WriteSi(uint32_t offset, int size, uint32_t value)
{
    // SICOMCSR (0x34): a transfer started with TSTART finishes at once, with its completion flag set.
    if (offset == 0x34 && (value & 1))
    {
        value = (value & ~1u) | 0x80000000;
    }
    si_.Write(offset, size, value);
}

template <std::size_t N>
uint32_t SystemRegisters::RegisterFile<N>::Read(uint32_t offset, int size) const
{
    uint32_t value = 0;
    for (int i = 0; i < size; i++)
    {
        value = value << 8 | (offset + i < N ? bytes[offset + i] : 0);
    }

    return value;
}

template <std::size_t N>
void SystemRegisters::RegisterFile<N>::Write(uint32_t offset, int size, uint32_t value)
{
    for (int i = size - 1; i >= 0; i--, value >>= 8)
    {
        if (offset + i < N)
        {
            bytes[offset + i] = static_cast<uint8_t>(value);
        }
    }
}

void System::ReportUnknown(PAddr address, int size, bool write, uint32_t value)
{
    if (!on_unknown_io || !reported_.insert(address).second)
    {
        return;
    }

    char line[80];
    if (write)
    {
        std::snprintf(line, sizeof(line), "write%d %08x = %08x at pc %08x", size * 8, address, value, cpu_.stop_pc_);
    }
    else
    {
        std::snprintf(line, sizeof(line), "read%d %08x at pc %08x", size * 8, address, cpu_.stop_pc_);
    }
    on_unknown_io(line);
}

} // namespace gcsf::gc
