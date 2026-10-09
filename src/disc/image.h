// SPDX-License-Identifier: MIT

// GameCube disc images in the common container formats: ISO (including NKit), GCZ, CISO, WDF, WIA and RVZ, whole or
// split into pieces. An Image provides the disc's bytes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace gcsf::disc
{

// A damaged or unsupported image, or a read that failed.
class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Random access to a disc image's bytes.
class Image
{
public:
    virtual ~Image() = default;

    // The container's name for messages: "ISO", "GCZ", "CISO", "WDF", "WIA" or "RVZ".
    virtual std::string Format() const = 0;

    // Reads `size` bytes of the disc at `offset` into `out`. Bytes that the image doesn't store read as zeros. These
    // include scrubbed areas and anything past the end of the image. Throws Error.
    virtual void Read(uint64_t offset, uint8_t* out, size_t size) = 0;
};

// Opens an image file in any of the formats above. The format is detected from the file's contents. The name doesn't
// matter. A split image is opened by its first piece. The other pieces append .1, .01 or .001 and onwards to the name.
// Wiimm's tools split images this way. Throws Error.
std::unique_ptr<Image> OpenImage(const std::filesystem::path& path);

} // namespace gcsf::disc
