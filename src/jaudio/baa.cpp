// SPDX-License-Identifier: MIT

#include "jaudio/baa.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/bytes.h"

namespace gcsf::jaudio
{
namespace
{

// The commands of the archive. Each is four characters and its arguments, 32-bit words.
constexpr uint32_t kStart = 0x41415f3c;           // "AA_<"
constexpr uint32_t kEnd = 0x3e5f4141;             // ">_AA"
constexpr uint32_t kWaveSystem = 0x77732020;      // "ws  ": id, offset, flag
constexpr uint32_t kBank = 0x626e6b20;            // "bnk ": id, offset
constexpr uint32_t kBankList = 0x626c5f3c;        // "bl_<": two arguments
constexpr uint32_t kBankListEnd = 0x3e5f626c;     // ">_bl"
constexpr uint32_t kCollection = 0x62736320;      // "bsc ": start, end
constexpr uint32_t kTable = 0x62737420;           // "bst ": start, end
constexpr uint32_t kNames = 0x6273746e;           // "bstn": start, end
constexpr uint32_t kSequence = 0x626d7320;        // "bms ": id, start, end
constexpr uint32_t kArchiveSequence = 0x626d7361; // "bmsa": id
constexpr uint32_t kVoiceBank = 0x76626e6b;       // "vbnk": two arguments
constexpr uint32_t kSequenceBlock = 0x64737162;   // "dsqb": size
constexpr uint32_t kStreamTable = 0x62736674;     // "bsft": offset
constexpr uint32_t kSectionInfo = 0x73656374;     // "sect": four bytes

// The magic words of the sound table and the name table.
constexpr uint32_t kTableMagic = 0x42535420;      // "BST "
constexpr uint32_t kNamesMagic = 0x4253544e;      // "BSTN"
constexpr uint32_t kWaveSystemMagic = 0x57535953; // "WSYS"
constexpr uint32_t kWaveInfoMagic = 0x57494e46;   // "WINF"

// A wave group's archive name takes this many bytes.
constexpr std::size_t kArchiveNameSize = 0x70;

// The blocks that the command list names.
struct Blocks
{
    std::size_t table = 0;
    std::size_t names = 0;
    std::vector<std::size_t> wave_systems;
};

// Reads the archive's words with checks on its bounds.
class Reader
{
public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data)
    {
    }

    uint32_t Word(std::size_t offset) const
    {
        if (offset > data_.size() || data_.size() - offset < 4)
        {
            throw Error("the BAA archive is damaged");
        }

        return LoadBe32(data_.data() + offset);
    }

    uint8_t Byte(std::size_t offset) const
    {
        if (offset >= data_.size())
        {
            throw Error("the BAA archive is damaged");
        }

        return data_[offset];
    }

    // The NUL-terminated string at `offset`, at most `limit` bytes.
    std::string String(std::size_t offset, std::size_t limit = 0x100) const
    {
        if (offset >= data_.size())
        {
            throw Error("the BAA archive is damaged");
        }

        const auto begin = data_.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(std::min(limit, data_.size() - offset));

        return std::string(begin, std::find(begin, end, uint8_t{0}));
    }

private:
    const std::vector<uint8_t>& data_;
};

// The number of argument words that follow each command.
int ArgumentWords(uint32_t command)
{
    switch (command)
    {
    case kWaveSystem:
    case kSequence:
        return 3;
    case kBank:
    case kBankList:
    case kCollection:
    case kTable:
    case kNames:
    case kVoiceBank:
        return 2;
    case kArchiveSequence:
    case kSequenceBlock:
    case kStreamTable:
    case kSectionInfo:
        return 1;
    case kBankListEnd:
        return 0;
    default:
        throw Error("the BAA archive has a command that isn't known");
    }
}

Blocks FindBlocks(const Reader& reader)
{
    if (reader.Word(0) != kStart)
    {
        throw Error("it isn't a BAA archive");
    }

    Blocks blocks;
    for (std::size_t at = 4;;)
    {
        const uint32_t command = reader.Word(at);
        if (command == kEnd)
        {
            return blocks;
        }

        if (command == kTable)
        {
            blocks.table = reader.Word(at + 4);
        }
        else if (command == kNames)
        {
            blocks.names = reader.Word(at + 4);
        }
        else if (command == kWaveSystem)
        {
            blocks.wave_systems.push_back(reader.Word(at + 8));
        }

        at += 4 + 4 * static_cast<std::size_t>(ArgumentWords(command));
    }
}

// The offset of a table's root, checked against the table's magic. Both tables give it at +0xC, from their start.
std::size_t TableRoot(const Reader& reader, std::size_t table, uint32_t magic)
{
    if (reader.Word(table) != magic)
    {
        throw Error("the BAA archive's sound table is damaged");
    }

    return table + reader.Word(table + 12);
}

} // namespace

std::vector<Sound> ReadSounds(const std::vector<uint8_t>& baa)
{
    const Reader reader(baa);
    const Blocks blocks = FindBlocks(reader);
    if (!blocks.table)
    {
        throw Error("the BAA archive has no sound table");
    }

    // The sound table: sections, each with groups, each with items. A group gives each item's type in the top byte of a
    // word and the item's offset in the low 24 bits. An item's third byte is the sound's volume.
    const std::size_t root = TableRoot(reader, blocks.table, kTableMagic);
    const std::size_t names_root = blocks.names ? TableRoot(reader, blocks.names, kNamesMagic) : 0;
    const uint32_t sections = reader.Word(root);
    std::vector<Sound> sounds;
    for (uint32_t s = 0; s < sections && s < 0x100; s++)
    {
        const uint32_t section_offset = reader.Word(root + 4 + 4 * std::size_t{s});
        if (!section_offset)
        {
            continue;
        }

        // The name table has the same tree. A section gives its group count, its name, then its groups. A group gives
        // its item count, its name, then its items' names.
        const std::size_t section = blocks.table + section_offset;
        const std::size_t names_section =
            names_root && s < reader.Word(names_root) ? blocks.names + reader.Word(names_root + 4 + 4 * s) : 0;
        const std::string section_name =
            names_section ? reader.String(blocks.names + reader.Word(names_section + 4)) : "";
        const uint32_t groups = reader.Word(section);
        for (uint32_t g = 0; g < groups && g < 0x100; g++)
        {
            const uint32_t group_offset = reader.Word(section + 4 + 4 * std::size_t{g});
            if (!group_offset)
            {
                continue;
            }

            const std::size_t group = blocks.table + group_offset;
            const std::size_t names_group = names_section && g < reader.Word(names_section)
                                                ? blocks.names + reader.Word(names_section + 8 + 4 * g)
                                                : 0;
            const std::string group_name =
                names_group ? reader.String(blocks.names + reader.Word(names_group + 4)) : "";
            const uint32_t items = reader.Word(group);
            for (uint32_t i = 0; i < items && i < 0x10000; i++)
            {
                const uint32_t entry = reader.Word(group + 8 + 4 * std::size_t{i});
                if (!(entry & 0xffffff))
                {
                    continue;
                }

                Sound sound;
                sound.id = s << 24 | g << 16 | i;
                sound.type = static_cast<uint8_t>(entry >> 24);
                sound.volume = reader.Byte(blocks.table + (entry & 0xffffff) + 2);
                if (names_group && i < reader.Word(names_group))
                {
                    sound.label = reader.String(blocks.names + reader.Word(names_group + 8 + 4 * std::size_t{i}));
                }

                sound.section = section_name;
                sound.group = group_name;
                sounds.push_back(std::move(sound));
            }
        }
    }

    if (sounds.empty())
    {
        throw Error("the BAA archive's sound table lists no sounds");
    }

    return sounds;
}

std::vector<std::string> WaveArchiveNames(const std::vector<uint8_t>& baa)
{
    // A wave system gives its WINF block at +0x10. WINF counts the wave groups and gives each group's offset. Both
    // offsets are from the wave system's start. A group starts with its wave archive's file name.
    const Reader reader(baa);
    std::vector<std::string> names;
    for (const std::size_t system : FindBlocks(reader).wave_systems)
    {
        if (reader.Word(system) != kWaveSystemMagic)
        {
            throw Error("a wave system in the BAA archive is damaged");
        }

        const std::size_t info = system + reader.Word(system + 0x10);
        if (reader.Word(info) != kWaveInfoMagic)
        {
            throw Error("a wave system in the BAA archive is damaged");
        }

        const uint32_t groups = reader.Word(info + 4);
        for (uint32_t i = 0; i < groups && i < 0x100; i++)
        {
            const std::string name =
                reader.String(system + reader.Word(info + 8 + 4 * std::size_t{i}), kArchiveNameSize);
            if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }
    }

    return names;
}

} // namespace gcsf::jaudio
