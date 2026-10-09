// SPDX-License-Identifier: MIT

// The disc interface at physical 0x0C006000: the registers through which the SDK's DVD library drives the disc drive.
// The library writes a command into the three command registers, the destination and length of the data into the DMA
// registers, and starts the transfer through the control register. The drive answers with an interrupt. The emulated
// drive reads a Disc. It answers every command after a fixed delay and moves a read's data at that time.

#pragma once

#include <cstddef>
#include <cstdint>

#include "dsp/main_memory.h"

namespace gcsf::gc
{

// The data on the disc in the form that the drive reads.
class Disc
{
public:
    virtual ~Disc() = default;

    // Reads `size` bytes at byte `offset` of the disc. Returns false if any of them lies past the disc's end.
    virtual bool Read(uint64_t offset, void* out, std::size_t size) = 0;
};

// The interface's registers and the command in progress. A snapshot of the system copies them (see
// DiscInterface::Save).
class DiscInterfaceState
{
protected:
    uint32_t status_ = 0;   // DISR: the interrupt flags and their masks
    uint32_t cover_ = 0;    // DICVR: the cover's state, and its interrupt flag and mask
    uint32_t command_[3]{}; // DICMDBUF0-2
    uint32_t dma_address_ = 0;
    uint32_t dma_length_ = 0;
    uint32_t control_ = 0; // DICR
    uint32_t immediate_ = 0;
    uint32_t error_ = 0;         // what the next RequestError reports
    uint64_t done_ = UINT64_MAX; // the CPU cycle at which the command in progress completes
    bool failed_ = false;        // whether it completes with an error
};

class DiscInterface : private DiscInterfaceState
{
public:
    using Snapshot = DiscInterfaceState;

    DiscInterface(Disc& disc, MainMemory& memory);

    // A 32-bit register at `offset` from 0x0C006000. `now` is the CPU's cycle count.
    uint32_t Read(uint32_t offset) const;
    void Write(uint32_t offset, uint32_t value, uint64_t now);

    // Whether an enabled interrupt is pending: the DI line to the processor interface.
    bool InterruptPending() const;

    // The CPU cycle of the command's completion, and the completion once it's due.
    uint64_t NextEvent() const
    {
        return done_;
    }

    void Advance(uint64_t now);

    Snapshot Save() const
    {
        return *this;
    }

    void Restore(const Snapshot& snapshot)
    {
        DiscInterfaceState::operator=(snapshot);
    }

private:
    void Start(uint64_t now);
    void Complete();
    bool ReadIntoMemory(uint64_t offset, uint32_t length);

    Disc& disc_;
    MainMemory& memory_;
};

} // namespace gcsf::gc
