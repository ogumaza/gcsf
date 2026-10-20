// SPDX-License-Identifier: MIT

// The ripper's parts for games whose MusyX is under Hudson's sound manager: the songs of the game's sound test, with
// their names from the game's messages, and the library's sound files. The length analysis is MusyX's
// (MeasureMusyXSound).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "xsflib/binary.h"

#include "gcsf/format.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// The sound test's kinds of sound that aren't songs.
constexpr uint32_t kStreamKind = 1;
constexpr uint32_t kEffectKind = 28;

// A stream's sound ID: this bit and the stream's number.
constexpr uint32_t kStreamBit = 0x80000000;

// The bytes of an entry of the sound test.
constexpr uint32_t kEntrySize = 16;

// The offset in the stream file's header of the offset at which the streams' samples start.
constexpr std::size_t kSampleOffset = 0x1c;

// The character that the font draws for a byte of a message from 0x30 on. Digits and Latin letters keep their ASCII
// codes. A few punctuation marks have other codes. A byte without a glyph here becomes an underscore.
char Glyph(uint8_t c)
{
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    {
        return static_cast<char>(c);
    }

    struct Mark
    {
        uint8_t code;
        char mark;
    };
    static constexpr Mark kMarks[] = {{0x3d, '-'}, {0x5c, '\''}, {0x7e, '&'}, {0x82, ','},
                                      {0x85, '.'}, {0xc2, '!'},  {0xc3, '?'}};
    const auto found = std::find_if(std::begin(kMarks), std::end(kMarks), [c](const Mark& m) { return m.code == c; });

    return found != std::end(kMarks) ? found->mark : '_';
}

// The text of the message `id` in the message file at `path`. The ID holds the bank in its top 16 bits and the
// message's number in the bank in its low 16 bits. The file starts with the number of its banks and the offset of its
// table of banks. The table gives each bank's ID and slot in two halfwords. The word at 4 + 4 times the slot gives the
// bank's offset in the file. The slots start at 1. A bank starts with the number of its messages. A word for each
// message then gives its offset from the bank's start. A message ends with a zero byte.
//
// Below 0x30 the bytes are control codes. Four of them become a space: 0x0A (a new line), 0x0C (the next tab stop),
// 0x10 and 0x20 (spaces). 0x0E, 0x1C, 0x1E and 0x1F take the byte after them: a symbol, a voice, a colour, or another
// message to insert. They're left out with that byte. The other control codes are left out too. The text has no spaces
// at either end. Throws xsflib::FormatError if the file has no such message or the message runs past the file's end.
std::string MessageText(std::span<const uint8_t> file, uint32_t id, const char* path)
{
    const std::string where =
        std::string(path) + "'s message " + std::to_string(id >> 16) + "-" + std::to_string(id & 0xffff);
    const std::string past_end = where + " runs past the file's end";
    const xsflib::Reader r(file, true, {.bounds_message = xsflib::BoundsMessage::kFixed, .fixed_message = past_end});

    const uint32_t banks = r.U32(0);
    const uint32_t table = r.U32(4);
    std::optional<uint32_t> bank;
    for (uint32_t i = 0; i < banks && !bank; i++)
    {
        const std::size_t entry = table + std::size_t{4} * i;
        if (r.U16(entry) == id >> 16)
        {
            bank = r.U32(4 + std::size_t{4} * r.U16(entry + 2));
        }
    }

    if (!bank || (id & 0xffff) >= r.U32(*bank))
    {
        throw xsflib::FormatError(where + " is missing");
    }

    std::string text;
    std::size_t at = *bank + std::size_t{r.U32(*bank + 4 + std::size_t{4} * (id & 0xffff))};
    for (uint8_t c = r.U8(at); c != 0; c = r.U8(++at))
    {
        if (c == 0x0a || c == 0x0c || c == 0x10 || c == 0x20)
        {
            text += ' ';
        }
        else if (c == 0x0e || c == 0x1c || c == 0x1e || c == 0x1f)
        {
            at++;
        }
        else if (c >= 0x30)
        {
            text += Glyph(c);
        }
    }

    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos)
    {
        return {};
    }

    return text.substr(first, text.find_last_not_of(' ') + 1 - first);
}

} // namespace

std::vector<Sound> MsmSongs(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<uint8_t> module = ReadGameFile(game, profile.sound_test_module);
    const std::vector<uint8_t> messages = ReadGameFile(game, profile.messages);
    const std::string where = std::string(profile.sound_test_module) + "'s sound test runs past the file's end";
    const xsflib::Reader r(module, true, {.bounds_message = xsflib::BoundsMessage::kFixed, .fixed_message = where});

    std::vector<Sound> sounds;
    for (uint32_t i = 0; i < profile.sound_test_count; i++)
    {
        const std::size_t entry = profile.sound_test + std::size_t{kEntrySize} * i;
        const uint32_t kind = r.U16(entry + 8);
        if (r.U32(entry) == 0 || kind == kEffectKind)
        {
            continue;
        }

        const uint32_t number = r.U32(entry + 12);
        std::string label = MessageText(messages, r.U32(entry + 4), profile.messages);
        if (kind == kStreamKind)
        {
            sounds.push_back({kStreamBit | number, std::move(label), true, true});
        }
        else
        {
            sounds.push_back({uint32_t{r.U16(entry + 10)} << 16 | (number & 0xffff), std::move(label), true});
        }
    }

    return sounds;
}

std::optional<std::string> AddMsmFiles(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<std::string> banks(profile.bank_files.begin(), profile.bank_files.end());
    if (const std::optional<std::string> problem = AddDiscFiles(program, game, banks))
    {
        return problem;
    }

    try
    {
        std::vector<uint8_t> streams = ReadGameFile(game, profile.stream_file);
        const xsflib::Reader r(streams, true);
        const uint32_t samples = r.U32(kSampleOffset);
        if (samples > streams.size())
        {
            return std::string(profile.stream_file) + "'s samples start past the file's end";
        }

        streams.resize(samples);
        program.AddFile(profile.stream_file, streams);
    }
    catch (const xsflib::FormatError& e)
    {
        return std::string(profile.stream_file) + ": " + e.what();
    }

    return std::nullopt;
}

} // namespace gcsf::rip
