// SPDX-License-Identifier: MIT

#include "gc/di.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "gc/memory.h"

namespace gcsf::gc
{
namespace
{

// DISR's bits. Writing 1 to an interrupt flag clears it. The masks take the value written.
constexpr uint32_t kBreak = 1u << 0;
constexpr uint32_t kErrorMask = 1u << 1;
constexpr uint32_t kErrorInterrupt = 1u << 2;
constexpr uint32_t kTransferMask = 1u << 3;
constexpr uint32_t kTransferInterrupt = 1u << 4;
constexpr uint32_t kBreakMask = 1u << 5;
constexpr uint32_t kBreakInterrupt = 1u << 6;
constexpr uint32_t kStatusMasks = kErrorMask | kTransferMask | kBreakMask;
constexpr uint32_t kStatusFlags = kErrorInterrupt | kTransferInterrupt | kBreakInterrupt;

// DICVR's bits. Bit 0 is the cover's state, 0 when it's closed. The emulated cover stays closed.
constexpr uint32_t kCoverMask = 1u << 1;
constexpr uint32_t kCoverInterrupt = 1u << 2;

// DICR's start bit. It reads 1 until the command completes.
constexpr uint32_t kStart = 1u << 0;

// The registers' offsets from 0x0C006000.
constexpr uint32_t kStatusRegister = 0x00;
constexpr uint32_t kCoverRegister = 0x04;
constexpr uint32_t kCommandRegisters = 0x08;
constexpr uint32_t kDmaAddressRegister = 0x14;
constexpr uint32_t kDmaLengthRegister = 0x18;
constexpr uint32_t kControlRegister = 0x1c;
constexpr uint32_t kImmediateRegister = 0x20;

// The drive's commands, by the top byte of the first command register.
constexpr uint32_t kInquiry = 0x12;
constexpr uint32_t kRead = 0xa8;
constexpr uint32_t kSeek = 0xab;
constexpr uint32_t kRequestError = 0xe0;
constexpr uint32_t kAudioStream = 0xe1;
constexpr uint32_t kAudioStatus = 0xe2;
constexpr uint32_t kStopMotor = 0xe3;
constexpr uint32_t kAudioBufferConfig = 0xe4;

// A read whose first command word ends in 0x40 reads the disc's ID: the first 32 bytes of the disc.
constexpr uint32_t kReadDiscId = 0x40;
constexpr uint32_t kDiscIdSize = 0x20;

// The errors that RequestError reports: an unknown command, and a read past the disc's end.
constexpr uint32_t kInvalidCommand = 0x052000;
constexpr uint32_t kBlockOutOfRange = 0x052100;

// The drive answers every command this many CPU cycles after it starts.
constexpr uint64_t kCommandCycles = 20'000;

// What an Inquiry returns: the drive's revision, device code and release date, then padding.
constexpr std::array<uint8_t, 0x20> kDriveInfo = {0x00, 0x02, 0x00, 0x00, 0x20, 0x02, 0x04, 0x02};

} // namespace

DiscInterface::DiscInterface(Disc& disc, MainMemory& memory) : disc_(disc), memory_(memory)
{
}

uint32_t DiscInterface::Read(uint32_t offset) const
{
    switch (offset)
    {
    case kStatusRegister:
        return status_;
    case kCoverRegister:
        return cover_;
    case kCommandRegisters:
    case kCommandRegisters + 4:
    case kCommandRegisters + 8:
        return command_[(offset - kCommandRegisters) / 4];
    case kDmaAddressRegister:
        return dma_address_;
    case kDmaLengthRegister:
        return dma_length_;
    case kControlRegister:
        return control_;
    case kImmediateRegister:
        return immediate_;
    default:
        return 0;
    }
}

void DiscInterface::Write(uint32_t offset, uint32_t value, uint64_t now)
{
    switch (offset)
    {
    case kStatusRegister:
        status_ = (status_ & kStatusFlags & ~value) | (value & kStatusMasks);

        // A break stops the command in progress. The command then ends with the break's interrupt.
        if ((value & kBreak) && done_ != UINT64_MAX)
        {
            done_ = UINT64_MAX;
            control_ &= ~kStart;
            status_ |= kBreakInterrupt;
        }
        break;

    case kCoverRegister:
        cover_ = (cover_ & kCoverInterrupt & ~value) | (value & kCoverMask);
        break;

    case kCommandRegisters:
    case kCommandRegisters + 4:
    case kCommandRegisters + 8:
        command_[(offset - kCommandRegisters) / 4] = value;
        break;

    case kDmaAddressRegister:
        dma_address_ = value & 0x03ffffe0;
        break;

    case kDmaLengthRegister:
        dma_length_ = value & ~0x1fu;
        break;

    case kControlRegister:
        if ((value & kStart) && done_ == UINT64_MAX)
        {
            control_ = value & 7;
            Start(now);
        }
        break;

    case kImmediateRegister:
        immediate_ = value;
        break;

    default:
        break;
    }
}

bool DiscInterface::InterruptPending() const
{
    return ((status_ & kErrorInterrupt) && (status_ & kErrorMask)) ||
           ((status_ & kTransferInterrupt) && (status_ & kTransferMask)) ||
           ((status_ & kBreakInterrupt) && (status_ & kBreakMask)) ||
           ((cover_ & kCoverInterrupt) && (cover_ & kCoverMask));
}

void DiscInterface::Advance(uint64_t now)
{
    if (now >= done_)
    {
        Complete();
    }
}

void DiscInterface::Start(uint64_t now)
{
    done_ = now + kCommandCycles;
}

void DiscInterface::Complete()
{
    // The command's work happens as it completes: a read's data arrives, and an immediate result is set.
    bool ok = true;
    switch (command_[0] >> 24)
    {
    case kRead:
        if ((command_[0] & 0xff) == kReadDiscId)
        {
            ok = ReadIntoMemory(0, kDiscIdSize);
        }
        else
        {
            ok = ReadIntoMemory(uint64_t{command_[1]} << 2, dma_length_);
        }

        if (!ok)
        {
            error_ = kBlockOutOfRange;
        }
        break;

    case kInquiry:
        if (uint8_t* to = memory_.At(dma_address_, kDriveInfo.size()))
        {
            std::copy(kDriveInfo.begin(), kDriveInfo.end(), to);
        }

        dma_address_ += static_cast<uint32_t>(kDriveInfo.size());
        dma_length_ = 0;
        break;

    case kRequestError:
        immediate_ = error_;
        error_ = 0;
        break;

    case kAudioStatus:
        immediate_ = 0;
        break;

    case kSeek:
    case kAudioStream:
    case kStopMotor:
    case kAudioBufferConfig:
        break;

    default:
        error_ = kInvalidCommand;
        ok = false;
        break;
    }

    status_ |= ok ? kTransferInterrupt : kErrorInterrupt;
    control_ &= ~kStart;
    done_ = UINT64_MAX;
}

bool DiscInterface::ReadIntoMemory(uint64_t offset, uint32_t length)
{
    // The data goes to main memory at the DMA address. The registers end with the end address and a length of 0.
    uint8_t* to = memory_.At(dma_address_, length);
    if (to == nullptr || !disc_.Read(offset, to, length))
    {
        return false;
    }

    dma_address_ += length;
    dma_length_ = 0;

    return true;
}

} // namespace gcsf::gc
