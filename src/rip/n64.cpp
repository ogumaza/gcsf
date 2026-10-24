// SPDX-License-Identifier: MIT

// The ripper's parts for games whose sound library is the Nintendo 64's: the pieces and the effects, the library's
// sound files, and the length analysis of a sound through ntdlib's LoopFinder.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/dol.h"
#include "ntdlib/jaudio/game_lengths.h"
#include "ntdlib/jaudio/loop_finder.h"
#include "xsflib/binary.h"
#include "xsflib/bytes.h"
#include "xsflib/rip/length.h"

#include "gcsf/format.h"
#include "gcsf/player.h"
#include "rip/analysis.h"
#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// The weathers for which the game arranges a piece of music that plays the hour: its numbers for them, and the names
// that its tables of masks give them (BGM_MUTE_TABLE_FINE, _SNOW and _SAKURA). The game plays another piece in the
// rain, its weather 1. The game's falling leaves count as its weather 3.
struct N64Weather
{
    uint8_t number;
    const char* name;
};

constexpr N64Weather kN64Weathers[] = {{0, "fine"}, {2, "snow"}, {3, "sakura"}};

// The game's numbers of its pieces of music that play the hour.
constexpr uint32_t kN64FirstHour = 1;
constexpr uint32_t kN64LastHour = 24;

// The top byte of an effect's sound ID with the Nintendo 64's sound library. The driver takes the low 16 bits as the
// game's number of the effect.
constexpr uint32_t kN64EffectKind = 1;

// The most effects in a bank of the Nintendo 64's sound library. The game's table of the effects' priorities has an
// entry for each place in a bank (TRGPRIO, 128 bytes).
constexpr uint32_t kN64BankEffects = 128;

// The museum's piece of music, and its rooms by the game's numbers for them. Each room mutes some of the piece's
// channels (BGM_MUTE_TABLE_MUSEUM). The game plays the piece only in the museum.
constexpr uint32_t kN64Museum = 88;
constexpr const char* kN64MuseumRooms[] = {"lobby", "paintings", "fish", "insects", "fossils"};

// The sound ID of the game's piece `number` in the weather `weather` and the museum's room `room`, with the islander
// on Doubutsu no Mori e+'s island or not. The fields follow driver/animal_crossing.c's layouts.
uint32_t N64PieceId(const GameProfile& profile, uint32_t number, uint32_t weather, uint32_t room, bool islander)
{
    if (profile.n64_wide_numbers)
    {
        return number | weather << 16 | room << 20 | uint32_t{islander} << 23;
    }

    return number | weather << 8 | room << 16;
}

// Whether the game's start of a piece of music (Sou_BgmStart) plays its number `number` as a piece. Number 0 stops the
// music. Animal Crossing and Doubutsu no Mori e+ play 83 to 87 as effects of the start-up screen
// (GameProfile::n64_effect_numbers). Doubutsu no Mori+ plays them as pieces. Numbers 112 to 121 are the second parts of
// Kapp'n's songs. The first part's script starts its second part. The game's driver plays a second part as a piece.
bool N64PlaysPiece(const GameProfile& profile, uint32_t number)
{
    const auto [first_effect, last_effect] = profile.n64_effect_numbers;
    return number != 0 && !(first_effect != 0 && number >= first_effect && number <= last_effect);
}

// A sequence of the game's table of sequences (GameProfile::sequence_table): its number, and its offset and size in the
// sound data.
struct N64TableSequence
{
    uint32_t number;
    uint32_t offset;
    uint32_t size;
};

// The sequence that number `number` of the game's table of sequences plays. The table gives each sequence's offset and
// size. A sequence of size 0 gives the number of the sequence that it plays in place of its offset. Throws
// xsflib::FormatError for a number outside the table, or for a chain that doesn't end.
N64TableSequence N64Sequence(const ntdlib::Dol& dol, const GameProfile& profile, uint32_t number)
{
    constexpr uint32_t kEntries = 0x10;
    constexpr uint32_t kEntrySize = 0x10;
    constexpr int kMaxChain = 8;
    const std::span<const uint8_t> table = DolBytes(dol, profile.sequence_table);
    const uint32_t count = table.size() >= 2 ? xsflib::LoadBe16(table.data()) : 0;
    for (int chain = 0; chain < kMaxChain; chain++)
    {
        const uint32_t entry = kEntries + number * kEntrySize;
        if (number >= count || entry + kEntrySize > table.size())
        {
            break;
        }

        const uint32_t offset = xsflib::LoadBe32(table.data() + entry);
        const uint32_t size = xsflib::LoadBe32(table.data() + entry + 4);
        if (size != 0)
        {
            return {number, offset, size};
        }

        number = offset;
    }

    throw xsflib::FormatError("main.dol's table of sequences lacks sequence " + std::to_string(number));
}

// A game's pieces of music with the Nintendo 64's sound library, by the game's numbers for them (see N64Sounds). A
// piece that plays the hour is listed once for each of kN64Weathers. The museum's piece is listed once for each of its
// rooms. Doubutsu no Mori e+'s island piece is listed with the islander and without. Another number is listed if the
// game plays it as a piece (N64PlaysPiece) and no lower such number plays the same sequence. A piece's label is "bgm"
// and its number in three digits. For a piece of the hour, of the museum or of the island, a hyphen and the weather's,
// the room's or the islander's name follow. Throws xsflib::FormatError if a table lies outside the game's code.
std::vector<Sound> N64Pieces(const ntdlib::Dol& dol, const GameProfile& profile)
{
    const uint32_t entry_size = profile.n64_wide_numbers ? 2 : 1;
    const std::span<const uint8_t> table = DolBytes(dol, profile.music_table);
    if (table.size() < uint64_t{profile.n64_pieces} * entry_size)
    {
        throw xsflib::FormatError("main.dol's table of music ends early");
    }

    std::vector<Sound> sounds;
    std::set<uint32_t> sequences;
    for (uint32_t number = 0; number < profile.n64_pieces; number++)
    {
        const uint32_t entry = profile.n64_wide_numbers ? xsflib::LoadBe16(table.data() + 2 * number) : table[number];
        const uint32_t sequence = N64Sequence(dol, profile, entry).number;
        std::string label = "bgm" + ZeroPadded(number, 3);
        if (number >= kN64FirstHour && number <= kN64LastHour)
        {
            for (const N64Weather& weather : kN64Weathers)
            {
                sounds.push_back(
                    {N64PieceId(profile, number, weather.number, 0, false), label + "-" + weather.name, true});
            }
        }
        else if (number == kN64Museum)
        {
            for (uint32_t room = 1; room <= std::size(kN64MuseumRooms); room++)
            {
                sounds.push_back(
                    {N64PieceId(profile, number, 0, room, false), label + "-" + kN64MuseumRooms[room - 1], true});
            }
        }
        else if (number != 0 && number == profile.n64_island_piece)
        {
            sounds.push_back({N64PieceId(profile, number, 0, 0, true), label + "-islander", true});
            sounds.push_back({N64PieceId(profile, number, 0, 0, false), label + "-no-islander", true});
        }
        else if (N64PlaysPiece(profile, number) && sequences.insert(sequence).second)
        {
            sounds.push_back({N64PieceId(profile, number, 0, 0, false), std::move(label), true});
        }
    }

    return sounds;
}

// A game's effects with the Nintendo 64's sound library (see GameProfile::n64_effects). A bank's table ends where
// another bank's table or an effect's script starts, or after 128 effects. An effect is named after its bank and its
// place, for example `se2-017`. The game's effect 0 is none: the game's slot of an effect holds 0 when it's free.
// Throws xsflib::FormatError if the tables lie outside the sound data.
std::vector<Sound> N64Effects(const disc::GameFiles& game, const ntdlib::Dol& dol, const GameProfile& profile)
{
    const std::vector<uint8_t> rom = ReadGameFile(game, profile.sound_archive);
    const N64TableSequence effects = N64Sequence(dol, profile, profile.n64_effects);
    const uint64_t offset = effects.offset;
    const uint64_t size = effects.size;
    if (offset + size > rom.size() || profile.n64_effect_banks + 2 > size)
    {
        throw xsflib::FormatError("the effects sequence lies outside the sound data");
    }

    const std::span<const uint8_t> sequence(rom.data() + offset, size);
    const auto half = [&sequence](uint32_t at)
    {
        return uint32_t{xsflib::LoadBe16(sequence.data() + at)};
    };

    // The table of banks ends where the first bank's table starts.
    std::vector<uint32_t> banks;
    uint32_t end = static_cast<uint32_t>(size);
    for (uint32_t at = profile.n64_effect_banks; at + 2 <= end && banks.size() < 16; at += 2)
    {
        const uint32_t bank = half(at);
        if (bank <= profile.n64_effect_banks || bank >= size)
        {
            throw xsflib::FormatError("the effects sequence's table of banks is damaged");
        }

        banks.push_back(bank);
        end = std::min(end, bank);
    }

    std::vector<Sound> sounds;
    for (uint32_t number = 0; number < banks.size(); number++)
    {
        const uint32_t start = banks[number];
        uint32_t stop = static_cast<uint32_t>(size);
        for (const uint32_t other : banks)
        {
            if (other > start)
            {
                stop = std::min(stop, other);
            }
        }

        for (uint32_t place = 0; place < kN64BankEffects && start + 2 * place + 2 <= stop; place++)
        {
            const uint32_t script = half(start + 2 * place);
            if (script > start)
            {
                stop = std::min(stop, script);
            }

            if (start + 2 * place + 2 > stop)
            {
                break;
            }

            if (number == 0 && place == 0)
            {
                continue;
            }

            sounds.push_back({kN64EffectKind << 24 | number << 8 | place,
                              "se" + std::to_string(number) + "-" + ZeroPadded(place, 3), false});
        }
    }

    return sounds;
}

} // namespace

std::vector<Sound> N64Sounds(const disc::GameFiles& game, const GameProfile& profile)
{
    const ntdlib::Dol dol = ntdlib::Dol::ParseMain(game.dol);
    std::vector<Sound> sounds = N64Pieces(dol, profile);
    if (profile.n64_effects)
    {
        std::vector<Sound> effects = N64Effects(game, dol, profile);
        sounds.insert(sounds.end(), std::make_move_iterator(effects.begin()), std::make_move_iterator(effects.end()));
    }

    return sounds;
}

std::optional<std::string> AddN64Files(ProgramBuilder& program, const disc::GameFiles& game, const GameProfile& profile)
{
    return AddDiscFiles(program, game, {profile.sound_archive});
}

namespace
{

// A script of the Nintendo 64's sound library: a sequence player's, a channel's or a layer's. Their order is that of
// GameProfile::n64_command_reads and n64_flow_returns.
enum class N64Script : uint8_t
{
    kPlayer,
    kChannel,
    kLayer,
};

// Where the Nintendo 64's sound library keeps what the length analysis reads. The builds in Animal Crossing, Doubutsu
// no Mori+ and Doubutsu no Mori e+ keep it in the same places, apart from a sequence player's fields
// (GameProfile::n64_player_fields). A script's state holds its program counter at +0, the depth of its calls and loops
// at +0x18, and its value at +0x19. A channel holds its script's state at +0x64, its player at +0x50 and the ticks left
// to wait at +0x22. A layer holds its script's state at +0x64, its channel at +0x60 and the ticks left to wait at
// +0x1a.
constexpr uint32_t kN64Pc = 0x00;
constexpr uint32_t kN64Depth = 0x18;
constexpr uint32_t kN64Value = 0x19;
constexpr uint32_t kN64ChannelScript = 0x64;
constexpr uint32_t kN64ChannelPlayer = 0x50;
constexpr uint32_t kN64ChannelWait = 0x22;
constexpr uint32_t kN64LayerScript = 0x64;
constexpr uint32_t kN64LayerChannel = 0x60;
constexpr uint32_t kN64LayerWait = 0x1a;

// The commands of the Nintendo 64's sound library that end a script at depth 0, or return from a call. From 0xf2 to
// 0xfe, the commands jump, call, loop and wait in all three kinds of script.
constexpr uint8_t kN64End = 0xff;
constexpr uint8_t kN64FirstFlow = 0xf2;

// The codes that the analysis passes to ntdlib's LoopFinder in place of a command: a command that changes the sound and
// one that doesn't. The end of a script at depth 0 passes jaudio::kFinishCommand.
constexpr uint8_t kN64Changes = 0x00;
constexpr uint8_t kN64Still = 0xfe;

// Whether a command of a script changes the sound: a note, a channel or layer started or stopped, or a parameter of the
// sound. Waits, jumps, calls, loops and the commands on ports and values don't. A layer's commands are notes up to
// 0xbf. 0xc0 waits.
bool N64ChangesSound(N64Script script, uint8_t command)
{
    if (command >= kN64FirstFlow)
    {
        return false;
    }

    switch (script)
    {
    case N64Script::kPlayer:
        // 0x00 to 0x3f test the channels, and 0x50 to 0x8f use the ports and loads. 0x40 to 0x4f stop channels, and
        // 0x90 to 0xaf start them. 0xb0 to 0xc1 load sequences, call the game and set nothing. 0xc2 jumps through a
        // table, 0xc5 counts, and 0xc7 to 0xcf work on the value. 0xc3 mutes channels, and 0xc4 and 0xc6 start and
        // stop sequences. From 0xd0, the commands set the player's parameters.
        if (command < 0x40 || (command >= 0x50 && command < 0x90) || (command >= 0xb0 && command < 0xc2))
        {
            return false;
        }

        return command != 0xc2 && command != 0xc5 && !(command >= 0xc7 && command <= 0xcf) && command != 0xef;
    case N64Script::kChannel:
        // 0x00 to 0x1f wait and load voices. 0x20 to 0x2f start channels. 0x30 to 0x77 use the ports and the value.
        // 0x78 to 0x7b start layers, and 0x80 to 0x83 test them. 0x88 to 0x9b start and stop layers. 0xa0 to 0xbe
        // work on the value and the channel's state, apart from the filters, the random variation and the sample's
        // start (0xa4, 0xb0, 0xb1, 0xb3, 0xb9 to 0xbb and 0xbd). From 0xc1, the commands set the channel's instrument
        // and parameters, apart from the jumps through tables (0xc2, 0xc5, 0xe4) and the work on the value (0xc7 to
        // 0xcf but 0xca and 0xcd).
        if (command < 0x20 || (command >= 0x30 && command < 0x78) || (command >= 0x80 && command < 0x88))
        {
            return false;
        }

        if (command < 0xa0)
        {
            return true;
        }

        if (command < 0xc1)
        {
            return command == 0xa4 || command == 0xb0 || command == 0xb1 || command == 0xb3 ||
                   (command >= 0xb9 && command <= 0xbb) || command == 0xbd;
        }

        return command != 0xc2 && command != 0xc5 && command != 0xe4 &&
               !(command >= 0xc7 && command <= 0xcf && command != 0xca && command != 0xcd);
    case N64Script::kLayer:
        return command != 0xc0;
    }

    return true;
}

// The scripts of a sound of the Nintendo 64's sound library as the tracks of ntdlib's LoopFinder (see AnalyzeN64Sound).
// A script's state stands for its track. A track's position is an offset in the sequence's data. Once the driver has
// started the sound, a watch of the library's command reads and of its flow function passes each place to OnWatch.
// That adds an event for each command that a script of the sound starts and for each jump that one takes.
class N64Tracks
{
public:
    N64Tracks(AnalysisMachine& machine, const GameProfile& profile, bool effect)
        : machine_(machine), profile_(profile), effect_(effect)
    {
    }

    // Follows the sound in the driver's handle: an effect's channel, or a piece's sequence player. Returns the root
    // track: the channel's script or the player's.
    uint32_t Start(uint32_t handle)
    {
        root_ = handle;
        const uint32_t track = root_ + (effect_ ? kN64ChannelScript : player_script_);
        data_ = machine_.Word((effect_ ? machine_.Word(root_ + kN64ChannelPlayer) : root_) + player_data_);
        scripts_.emplace(track, effect_ ? N64Script::kChannel : N64Script::kPlayer);

        return track;
    }

    // Takes the watched place `pc` in frame `frame`: a read of a command, or the flow function.
    void OnWatch(uint32_t pc, uint64_t frame)
    {
        if (root_ == 0)
        {
            return;
        }

        if (pc == profile_.n64_read_byte)
        {
            ReadCommand(frame);
        }
        else
        {
            TakeFlow(frame);
        }
    }

    // The events so far. The play loop clears them after each block.
    std::vector<ntdlib::jaudio::TrackEvent>& Events()
    {
        return events_;
    }

    // What a track waits for: the ticks left to wait. No script of the library waits for its notes.
    ntdlib::jaudio::TrackWait Wait(uint32_t track) const
    {
        const auto it = scripts_.find(track);
        if (it == scripts_.end())
        {
            return {0, false};
        }

        switch (it->second)
        {
        case N64Script::kPlayer:
            return {static_cast<int32_t>(machine_.Word(track - player_script_ + player_wait_) >> 16), false};
        case N64Script::kChannel:
            return {static_cast<int32_t>(machine_.Word(track - kN64ChannelScript + kN64ChannelWait) >> 16), false};
        case N64Script::kLayer:
            return {static_cast<int16_t>(machine_.Word(track - kN64LayerScript + kN64LayerWait) >> 16), false};
        }

        return {0, false};
    }

private:
    using TrackEvent = ntdlib::jaudio::TrackEvent;

    // The script that a call of the library's functions came from, by its return address.
    static std::optional<N64Script> ScriptOf(uint32_t lr, const std::array<uint32_t, 3>& returns)
    {
        const auto it = std::find(returns.begin(), returns.end(), lr);
        return it != returns.end() ? std::optional(static_cast<N64Script>(it - returns.begin())) : std::nullopt;
    }

    // The channel that a script's state belongs to, or 0 for a sequence player's.
    uint32_t ChannelOf(N64Script script, uint32_t state) const
    {
        switch (script)
        {
        case N64Script::kPlayer:
            return 0;
        case N64Script::kChannel:
            return state - kN64ChannelScript;
        case N64Script::kLayer:
            return machine_.Word(state - kN64LayerScript + kN64LayerChannel);
        }

        return 0;
    }

    // Whether a script belongs to the sound: to its sequence player, or to its channel.
    bool Belongs(N64Script script, uint32_t state) const
    {
        const uint32_t channel = ChannelOf(script, state);
        if (effect_)
        {
            return channel != 0 && channel == root_;
        }

        const uint32_t owner = channel ? machine_.Word(channel + kN64ChannelPlayer) : state - player_script_;
        return owner == root_;
    }

    // A command's start, from a read of the command. The script's state is in r3.
    void ReadCommand(uint64_t frame)
    {
        const auto& s = machine_.Cpu().state_;
        const std::optional<N64Script> script = ScriptOf(s.lr, profile_.n64_command_reads);
        const uint32_t state = s.gpr[3];
        if (!script || !Belongs(*script, state))
        {
            return;
        }

        const uint32_t position = machine_.Word(state + kN64Pc);
        const uint8_t command = machine_.Byte(position).value_or(0);
        const uint8_t depth = machine_.Byte(state + kN64Depth).value_or(0);
        const uint8_t code = command == kN64End                  ? (depth == 0 ? jaudio::kFinishCommand : kN64Still)
                             : N64ChangesSound(*script, command) ? kN64Changes
                                                                 : kN64Still;
        scripts_.emplace(state, *script);
        events_.push_back({TrackEvent::Kind::kCommand, state, position - data_, 0, depth, code, frame});
    }

    // A jump that the flow function takes. The script's state is in r4, the command in r5 and its argument in r6. A
    // jump's position is after its arguments. A relative jump counts from there.
    void TakeFlow(uint64_t frame)
    {
        const auto& s = machine_.Cpu().state_;
        const std::optional<N64Script> script = ScriptOf(s.lr, profile_.n64_flow_returns);
        const uint32_t state = s.gpr[4];
        if (!script || !Belongs(*script, state))
        {
            return;
        }

        const uint32_t position = machine_.Word(state + kN64Pc);
        const auto value = static_cast<int8_t>(machine_.Byte(state + kN64Value).value_or(0));
        const uint32_t argument = s.gpr[6];
        const uint32_t absolute = data_ + (argument & 0xffff);
        const uint32_t relative = position + static_cast<uint32_t>(static_cast<int8_t>(argument & 0xff));
        std::optional<uint32_t> target;
        switch (s.gpr[5] & 0xff)
        {
        case 0xfb: // jump
            target = absolute;
            break;
        case 0xfa: // jump if the value is 0
            target = value == 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf9: // jump if it's negative
            target = value < 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf5: // jump if it isn't
            target = value >= 0 ? std::optional(absolute) : std::nullopt;
            break;
        case 0xf4: // relative jump
            target = relative;
            break;
        case 0xf3: // relative jump if the value is 0
            target = value == 0 ? std::optional(relative) : std::nullopt;
            break;
        case 0xf2: // relative jump if it's negative
            target = value < 0 ? std::optional(relative) : std::nullopt;
            break;
        default:
            break;
        }

        if (target)
        {
            const uint8_t depth = machine_.Byte(state + kN64Depth).value_or(0);
            events_.push_back({TrackEvent::Kind::kJump, state, position - data_, *target - data_, depth,
                               static_cast<uint8_t>(s.gpr[5]), frame});
        }
    }

    AnalysisMachine& machine_;
    const GameProfile& profile_;
    const uint32_t player_script_ = profile_.n64_player_fields[0]; // a sequence player's script's state
    const uint32_t player_data_ = profile_.n64_player_fields[1];   // its sequence's data
    const uint32_t player_wait_ = profile_.n64_player_fields[2];   // its ticks left to wait
    const bool effect_;
    uint32_t root_ = 0;                     // the sound's sequence player or channel, once the driver has started it
    uint32_t data_ = 0;                     // the sequence's data
    std::map<uint32_t, N64Script> scripts_; // each track's kind, by its script's state
    std::vector<TrackEvent> events_;        // the events of this block
};

// The length of a sound of the Nintendo 64's sound library, which the driver has started (see LengthAnalyzer::Analyze).
// For a piece of music, the driver's handle holds the piece's sequence player. The analysis follows the player's
// script, its channels' and their layers' through ntdlib's LoopFinder (see N64Tracks), as it follows a JAudio sound's
// tracks (ntdlib::jaudio::AnalyzeGameSound). A script is a track. The player's script is the root. For an effect, the
// handle holds the channel of the effects sequence that plays the effect. The channel's script is the root, and its
// layers follow. The sound ends when the driver clears the handle.
std::optional<Timing> AnalyzeN64Sound(Player& player, const GameProfile& profile, uint32_t handle_address, bool effect)
{
    using ntdlib::jaudio::FramesToMs;
    using ntdlib::jaudio::LoopFinder;
    using ntdlib::jaudio::TrackEvent;
    const auto ms = [](uint64_t frames)
    {
        return FramesToMs(frames, kLengthRules.sample_rate);
    };

    AnalysisMachine machine(player);
    N64Tracks tracks(machine, profile, effect);
    uint64_t frames = 0;
    const ScopedWatch watch(machine.Cpu(), {profile.n64_read_byte, profile.n64_flow},
                            [&tracks, &frames](uint32_t pc) { tracks.OnWatch(pc, frames); });
    const auto read_wait = [&tracks](uint32_t track)
    {
        return tracks.Wait(track);
    };

    // Play until the sound ends, loops twice, holds, falls silent for good or reaches the limit.
    std::vector<int16_t> block(kLengthRules.block_frames * 2);
    std::optional<LoopFinder> finder;
    std::optional<uint64_t> first_sound;
    uint64_t last_sound = 0;
    std::optional<uint64_t> released;
    const auto ended = [&]() -> Timing
    {
        if (!first_sound)
        {
            return {kLengthRules.tail_ms, 0, true};
        }

        return {ms(last_sound + 1) + kLengthRules.tail_ms, 0, false};
    };

    const auto changes = [](uint8_t code)
    {
        return code != kN64Still;
    };

    while (ms(frames) < kLengthRules.limit_ms)
    {
        tracks.Events().clear();
        if (machine.Render(block.data(), kLengthRules.block_frames) != kLengthRules.block_frames)
        {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < kLengthRules.block_frames; i++)
        {
            if (xsflib::rip::Audible(std::span<const int16_t>(&block[2 * i], 2)))
            {
                last_sound = frames + i;
                first_sound = first_sound.value_or(last_sound);
            }
        }

        const uint32_t handle = machine.Word(handle_address);
        if (handle && !finder)
        {
            finder.emplace(tracks.Start(handle), kLengthRules.sample_rate, +changes);
        }

        // The tracks' loops. Loops before the first sound don't count.
        const ntdlib::jaudio::Sounded sounded = {first_sound.has_value(), last_sound};
        for (const TrackEvent& event : tracks.Events())
        {
            const LoopFinder::Verdict verdict = finder ? finder->Take(event, sounded) : LoopFinder::Verdict::kNone;
            if (verdict == LoopFinder::Verdict::kLoops)
            {
                return Timing{ms(event.frame), kLengthRules.loop_fade_ms, false};
            }

            if (verdict == LoopFinder::Verdict::kEnded)
            {
                return ended();
            }
        }

        frames += kLengthRules.block_frames;
        if (finder)
        {
            finder->TakeWaits(read_wait);
        }

        // The driver clears the handle when the player stops. The notes can still sound after that: the output runs
        // behind the sequence, and a release fades on.
        if (!handle && finder && !released)
        {
            released = frames;
        }

        if (released && (ms(frames - std::max(*released, last_sound)) >= kLengthRules.tail_ms ||
                         ms(frames - *released) >= ntdlib::jaudio::kStillMs))
        {
            return ended();
        }

        // A sound that holds loops where it holds, if it still sounds.
        if (first_sound && finder && finder->Holds(frames))
        {
            if (ms(frames - last_sound) <= kLengthRules.holding_ms)
            {
                return Timing{ms(frames), kLengthRules.loop_fade_ms, false};
            }

            return ended();
        }

        if (first_sound && ms(frames - last_sound) >= kLengthRules.silence_ms)
        {
            return ended();
        }
    }

    if (!first_sound)
    {
        return ended();
    }

    return Timing{kLengthRules.limit_ms, kLengthRules.loop_fade_ms, false};
}

} // namespace

std::optional<Timing> MeasureN64Sound(Player& player, const GameProfile& profile, uint32_t id)
{
    return AnalyzeN64Sound(player, profile, DriverBlock::kAddress + DriverBlock::kHandleOffset,
                           id >> 24 == kN64EffectKind);
}

} // namespace gcsf::rip
