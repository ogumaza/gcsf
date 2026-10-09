// SPDX-License-Identifier: MIT

// The GameCube's memory: main memory (24 MiB at physical 0) and ARAM (16 MiB of auxiliary memory). The CPU and the
// DSP's DMA share main memory. The DSP's accelerator reads samples from ARAM, and the CPU moves data in and out of ARAM
// with the ARAM DMA. JAudio keeps its wave archives in ARAM.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

#include "common/common_types.h"
#include "common/memory_image.h"

namespace gcsf
{

constexpr PAddr kMem1Base = 0x00000000;
constexpr std::size_t kMem1Size = 24 * 1024 * 1024;
constexpr std::size_t kAramSize = 16 * 1024 * 1024;

// The CPU addresses memory through cached (0x80000000 and up) and uncached (0xc0000000 and up) mirrors of the physical
// address space (OSCachedToPhysical, OSPhysicalToCached).
constexpr PAddr ToPhysical(uint32_t address)
{
    return address & 0x1fffffff;
}

constexpr uint32_t ToCached(PAddr address)
{
    return address | 0x80000000;
}

class MainMemory
{
public:
    // The memory's contents, for a snapshot of the system that holds it.
    struct Snapshot
    {
        MemoryImage mem1;
        MemoryImage aram;
    };

    // Both start zeroed. The memory comes from calloc. calloc leaves the zeroing to the system's lazy mapping of pages.
    // A system that touches little of the memory is cheap to create.
    MainMemory() : mem1_(Allocate(kMem1Size)), aram_(Allocate(kAramSize))
    {
    }

    // Takes a snapshot. If `previous` is given, the snapshot shares unchanged pages with it.
    Snapshot Save(const Snapshot* previous = nullptr) const
    {
        return {MemoryImage(mem1_.get(), kMem1Size, previous ? &previous->mem1 : nullptr),
                MemoryImage(aram_.get(), kAramSize, previous ? &previous->aram : nullptr)};
    }

    void Restore(const Snapshot& snapshot)
    {
        snapshot.mem1.Restore(mem1_.get());
        snapshot.aram.Restore(aram_.get());
    }

    // The `size` bytes at physical address `address` of main memory, or nullptr if any of them lies outside it.
    uint8_t* At(PAddr address, std::size_t size = 1)
    {
        static_assert(kMem1Base == 0);
        if (address > kMem1Size || size > kMem1Size - address)
        {
            return nullptr;
        }

        return mem1_.get() + address;
    }

    const uint8_t* At(PAddr address, std::size_t size = 1) const
    {
        return const_cast<MainMemory*>(this)->At(address, size);
    }

    // The `size` bytes at `address` in ARAM, or nullptr if any of them lies outside it.
    uint8_t* Aram(uint32_t address, std::size_t size = 1)
    {
        if (address > kAramSize || size > kAramSize - address)
        {
            return nullptr;
        }

        return aram_.get() + address;
    }

    const uint8_t* Aram(uint32_t address, std::size_t size = 1) const
    {
        return const_cast<MainMemory*>(this)->Aram(address, size);
    }

private:
    struct Free
    {
        void operator()(uint8_t* bytes) const
        {
            std::free(bytes);
        }
    };

    using Bytes = std::unique_ptr<uint8_t, Free>;

    static Bytes Allocate(std::size_t size)
    {
        Bytes bytes(static_cast<uint8_t*>(std::calloc(size, 1)));
        if (!bytes)
        {
            throw std::bad_alloc();
        }

        return bytes;
    }

    Bytes mem1_;
    Bytes aram_;
};

} // namespace gcsf
