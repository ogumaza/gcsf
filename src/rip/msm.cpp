// SPDX-License-Identifier: MIT

// The ripper's parts for games whose MusyX is under Hudson's sound manager: the songs of the game's sound test, with
// their names from the game's messages, the effects of the sound manager's file, and the library's sound files. The
// length analysis is MusyX's (MeasureMusyXSound).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "xsflib/binary.h"
#include "xsflib/utf8.h"

#include "gcsf/format.h"
#include "rip/rip.h"
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

// An effect's sound ID: the group to load in the top 16 bits, with kOneGroup, or kNoGroupSet for none, then kEffectBit
// and the effect's number (driver/mario_party.c).
constexpr uint32_t kNoGroupSet = 0xffff, kOneGroup = 0x8000, kEffectBit = 0x8000;

// The character that the English font draws for a byte of a message from 0x30 on, or 0 for none. Digits and Latin
// letters keep their ASCII codes. A few punctuation marks have other codes. The accented letters from 0x91 and 0xD1
// are those of the European release's languages. The American release's font draws them at the same codes.
uint32_t EnglishGlyph(uint8_t c)
{
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    {
        return c;
    }

    static constexpr std::u32string_view kCapitals = U"ÄÖÜßÀÁÈÉÌÍÒÓÙÚÑ";
    static constexpr std::u32string_view kSmall = U"àâäçèéêëîïôöûùüáìíòóúñŒœ";
    if (c >= 0x91 && c < 0x91 + kCapitals.size())
    {
        return kCapitals[c - 0x91];
    }

    if (c >= 0xd1 && c < 0xd1 + kSmall.size())
    {
        return kSmall[c - 0xd1];
    }

    struct Mark
    {
        uint8_t code;
        char32_t mark;
    };
    static constexpr Mark kMarks[] = {{0x3d, U'-'}, {0x5c, U'\''}, {0x7e, U'&'}, {0x82, U','},
                                      {0x85, U'.'}, {0xc2, U'!'},  {0xc3, U'?'}, {0xc4, U'⁉'},
                                      {0xc5, U'‼'}, {0xc6, U'¿'},  {0xc7, U'¡'}, {0xfe, U'…'}};
    const auto found = std::find_if(std::begin(kMarks), std::end(kMarks), [c](const Mark& m) { return m.code == c; });
    return found != std::end(kMarks) ? static_cast<uint32_t>(found->mark) : 0;
}

// The character that the Japanese font draws for a byte of a message from 0x30 on, or 0 for none. The font's texture
// holds 16 glyphs a row from code 0x30 on. Digits and Latin letters keep their ASCII codes. 0x80 and 0x81 are the
// voiced and semi-voiced marks, drawn after the kana that they mark. Hiragana run from 0x91 and katakana from 0xD1, in
// the order of the syllabary without wo, which is 0x86 and 0xC6. The small kana come before each run.
uint32_t JapaneseGlyph(uint8_t c)
{
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    {
        return c;
    }

    static constexpr char32_t kHigh[] =
        U"\u3099\u309a、。ー・をぁぃぅぇぉゃゅょっ"
        U"\0あいうえおかきくけこさしすせそ"
        U"たちつてとなにぬねのはひふへほま"
        U"みむめもやゆよらりるれろわん\0\0"
        U"『』!?⁉‼ヲァィゥェォャュョッ"
        U"\0アイウエオカキクケコサシスセソ"
        U"タチツテトナニヌネノハヒフヘホマ"
        U"ミムメモヤユヨラリルレロワン…▼";
    static_assert(std::size(kHigh) == 0x81);
    if (c >= 0x80)
    {
        return kHigh[c - 0x80];
    }

    struct Mark
    {
        uint8_t code;
        char32_t mark;
    };
    static constexpr Mark kMarks[] = {{0x3b, U'☆'}, {0x3c, U'+'},  {0x3d, U'-'},  {0x3e, U'×'}, {0x3f, U'→'},
                                      {0x5b, U'”'}, {0x5c, U'\''}, {0x5d, U'('},  {0x5e, U')'}, {0x5f, U'/'},
                                      {0x7b, U':'}, {0x7c, U'%'},  {0x7d, U'・'}, {0x7e, U'~'}};
    const auto found = std::find_if(std::begin(kMarks), std::end(kMarks), [c](const Mark& m) { return m.code == c; });
    return found != std::end(kMarks) ? static_cast<uint32_t>(found->mark) : 0;
}

// The kana that the voiced mark (U+3099) or the semi-voiced mark (U+309A) makes of kana `c`, or 0 if it makes none.
// Unicode puts each voiced kana one after its kana, and each semi-voiced kana two after.
uint32_t MarkedKana(uint32_t c, uint32_t mark)
{
    static constexpr std::u32string_view kVoiced =
        U"かきくけこさしすせそたちつてとはひふへほ"
        U"カキクケコサシスセソタチツテトハヒフヘホ";
    static constexpr std::u32string_view kHalfVoiced = U"はひふへほハヒフヘホ";
    if (mark == 0x3099 && (c == U'ウ' || c == U'う'))
    {
        return c == U'ウ' ? U'ヴ' : U'ゔ';
    }

    const std::u32string_view& kana = mark == 0x3099 ? kVoiced : kHalfVoiced;
    return kana.find(static_cast<char32_t>(c)) != std::u32string_view::npos ? c + (mark == 0x3099 ? 1 : 2) : 0;
}

// The text of the message `id` in the message file at `path`. The ID holds the bank in its top 16 bits and the
// message's number in the bank in its low 16 bits. The file starts with the number of its banks and the offset of its
// table of banks. The table gives each bank's ID and slot in two halfwords. The word at 4 + 4 times the slot gives the
// bank's offset in the file. The slots start at 1. A bank starts with the number of its messages. A word for each
// message then gives its offset from the bank's start. A message ends with a zero byte.
//
// Below 0x30 the bytes are control codes. Four of them become a space: 0x0A (a new line), 0x0C (the next tab stop),
// 0x10 and 0x20 (spaces). 0x0E, 0x1C, 0x1E and 0x1F take the byte after them: a symbol, a voice, a colour, or another
// message to insert. They're left out with that byte. The other control codes are left out too. A byte from 0x30 on is
// a glyph of the English font, or of the Japanese font with `japanese`. A voiced or semi-voiced mark joins the kana
// before it. A byte without a glyph becomes an underscore. The text is UTF-8, with no spaces at either end. Throws
// xsflib::FormatError if the file has no such message or the message runs past the file's end.
std::string MessageText(std::span<const uint8_t> file, uint32_t id, const char* path, bool japanese)
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

    std::u32string chars;
    std::size_t at = *bank + std::size_t{r.U32(*bank + 4 + std::size_t{4} * (id & 0xffff))};
    for (uint8_t c = r.U8(at); c != 0; c = r.U8(++at))
    {
        if (c == 0x0a || c == 0x0c || c == 0x10 || c == 0x20)
        {
            chars += U' ';
        }
        else if (c == 0x0e || c == 0x1c || c == 0x1e || c == 0x1f)
        {
            at++;
        }
        else if (c >= 0x30)
        {
            const uint32_t glyph = japanese ? JapaneseGlyph(c) : EnglishGlyph(c);
            const bool mark = glyph == 0x3099 || glyph == 0x309a;
            const uint32_t marked = mark && !chars.empty() ? MarkedKana(chars.back(), glyph) : 0;
            if (marked)
            {
                chars.back() = static_cast<char32_t>(marked);
            }
            else
            {
                chars += glyph ? static_cast<char32_t>(glyph) : U'_';
            }
        }
    }

    const std::size_t first = chars.find_first_not_of(U' ');
    if (first == std::u32string::npos)
    {
        return {};
    }

    std::string text;
    for (const char32_t c : chars.substr(first, chars.find_last_not_of(U' ') + 1 - first))
    {
        xsflib::AppendUtf8(text, c);
    }

    return text;
}

// The effects of the sound manager's file `msm`, in the order of its table of effects (see MsmSounds). `names` gives
// the names of some effects by their numbers. The file's header gives the offset of its information at 0x10, the
// offset and the size of its table of groups at 0x20 and 0x24, and the offset of its table of effects at 0x30, in
// big-endian words. The information gives the number of effects in a big-endian half at +6, the number of base groups
// in the byte at +0x28, and the base groups' numbers in the bytes from +0x29. A group's entry has 0x20 bytes and starts
// with the group's MusyX ID in a big-endian half. An effect's entry has 0x10 bytes and starts with the MusyX ID of the
// group that holds the effect, in a big-endian half. MusyX's number of the effect follows in a big-endian half. An
// entry with the number 0xffff holds no effect, and MusyX can't start it. Throws xsflib::FormatError if a table lies
// outside the file.
std::vector<Sound> MsmEffects(std::span<const uint8_t> msm, const std::string& path,
                              const std::map<uint32_t, std::string>& names)
{
    constexpr std::size_t kInfo = 0x10, kGroups = 0x20, kEffects = 0x30;
    constexpr std::size_t kEffectCount = 6, kBaseCount = 0x28, kBase = 0x29;
    constexpr uint32_t kMaxBase = 23, kGroupSize = 0x20, kEffectSize = 0x10, kNoEffect = 0xffff;

    const std::string past_end = path + "'s tables run past the file's end";
    const xsflib::Reader r(msm, true, {.bounds_message = xsflib::BoundsMessage::kFixed, .fixed_message = past_end});
    const uint32_t info = r.U32(kInfo);
    const uint32_t groups = r.U32(kGroups), group_count = r.U32(kGroups + 4) / kGroupSize;
    const uint32_t effects = r.U32(kEffects);
    const uint32_t effect_count = r.U16(info + kEffectCount);
    const uint32_t base_count = std::min<uint32_t>(r.U8(info + kBaseCount), kMaxBase);

    // The base groups, by their numbers.
    std::vector<uint32_t> base;
    for (uint32_t i = 0; i < base_count; i++)
    {
        base.push_back(r.U8(info + kBase + i));
    }

    // Each effect's group: the first group from 1 on with the effect's group ID. Group 0 holds nothing.
    std::vector<Sound> sounds;
    for (uint32_t i = 0; i < effect_count; i++)
    {
        const std::size_t entry = effects + std::size_t{kEffectSize} * i;
        if (r.U16(entry + 2) == kNoEffect)
        {
            continue;
        }

        const uint32_t id = r.U16(entry);
        uint32_t group = 1;
        while (group < group_count && r.U16(groups + std::size_t{kGroupSize} * group) != id)
        {
            group++;
        }

        if (group == group_count)
        {
            continue;
        }

        const bool in_base = std::find(base.begin(), base.end(), group) != base.end();
        const uint32_t load = in_base ? kNoGroupSet : kOneGroup | group;
        const auto name = names.find(i);
        std::string label = "se-" + ZeroPadded(i, 4);
        if (name != names.end() && !name->second.empty())
        {
            label += " " + name->second;
        }

        sounds.push_back({load << 16 | kEffectBit | i, std::move(label), false});
    }

    return sounds;
}

} // namespace

std::vector<Sound> MsmSounds(const disc::GameFiles& game, const GameProfile& profile)
{
    const std::vector<uint8_t> module = ReadGameFile(game, profile.sound_test_module);
    const std::vector<uint8_t> messages = ReadGameFile(game, profile.messages);
    const bool japanese = profile.japanese_messages;
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
        std::string label = MessageText(messages, r.U32(entry + 4), profile.messages, japanese);
        if (kind == kStreamKind)
        {
            sounds.push_back({kStreamBit | number, std::move(label), true, true});
        }
        else
        {
            sounds.push_back({uint32_t{r.U16(entry + 10)} << 16 | (number & 0xffff), std::move(label), true});
        }
    }

    // The list of voices names effects by their numbers.
    std::map<uint32_t, std::string> names;
    for (uint32_t i = 0; i < profile.sound_test_voice_count; i++)
    {
        const std::size_t entry = profile.sound_test_voices + std::size_t{kEntrySize} * i;
        if (r.U32(entry) != 0 && r.U16(entry + 8) == kEffectKind)
        {
            names[r.U32(entry + 12)] = MessageText(messages, r.U32(entry + 4), profile.messages, japanese);
        }
    }

    const std::string msm = profile.bank_files[0];
    std::vector<Sound> effects = MsmEffects(ReadGameFile(game, msm), msm, names);
    sounds.insert(sounds.end(), effects.begin(), effects.end());

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
