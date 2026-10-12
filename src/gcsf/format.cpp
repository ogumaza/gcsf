// SPDX-License-Identifier: MIT

// The GCSF file format (see format.h and docs/gcsf.md).

#include "gcsf/format.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/descriptor.h"
#include "xsflib/psf/psf.h"

namespace gcsf
{
namespace
{

using xsflib::psf::CheckDescriptorFlags;
using xsflib::psf::CheckDescriptorVersion;
using xsflib::psf::Put32;

} // namespace

std::vector<uint8_t> ProcessDescriptor::Serialize() const
{
    std::vector<uint8_t> v;
    Put32(v, kMagic);
    Put32(v, version);
    Put32(v, entry);
    Put32(v, status_address);
    Put32(v, flags);

    return v;
}

std::optional<ProcessDescriptor> ProcessDescriptor::Parse(const std::vector<uint8_t>& data)
{
    if (data.size() < 20 || xsflib::LoadLe32(data.data()) != kMagic)
    {
        return std::nullopt;
    }

    ProcessDescriptor d;
    d.version = xsflib::LoadLe32(&data[4]);
    d.entry = xsflib::LoadLe32(&data[8]);
    d.status_address = xsflib::LoadLe32(&data[12]);
    d.flags = xsflib::LoadLe32(&data[16]);

    return d;
}

std::optional<std::string> ProcessDescriptor::Check() const
{
    if (auto err = CheckDescriptorVersion("process", version, kVersion))
    {
        return err;
    }

    return CheckDescriptorFlags("process", flags, kFlagDspRequired);
}

} // namespace gcsf
