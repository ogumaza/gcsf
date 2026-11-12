// SPDX-License-Identifier: MIT

// The list of a game's sounds (ListGameSounds): the sounds of its sound library, then the variants and the states that
// its profile adds.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ntdlib/disc/game.h"
#include "ntdlib/flipper/z2_params.h"
#include "ntdlib/jaudio/jungle_beat_states.h"
#include "ntdlib/jaudio/z2_starts.h"
#include "ntdlib/jaudio/z2_states.h"
#include "xsflib/binary.h"
#include "xsflib/psf/psf.h"

#include "rip/rip.h"
#include "rip/sound_libraries.h"

namespace gcsf::rip
{

namespace
{

// The sounds of a game without a start-up archive, from its code or its files (see ListGameSounds). A game with a
// start-up archive gets none here. Throws xsflib::FormatError if main.dol, a table or a file that lists the sounds
// can't be read.
std::vector<Sound> SoundsFromCodeOrFiles(const disc::GameFiles& game, const GameProfile& profile)
{
    if (UsesMusyXCatalogue(profile))
    {
        return CatalogueSounds(game, profile);
    }

    if (profile.driver == Driver::kMarioParty)
    {
        return MsmSounds(game, profile);
    }

    switch (profile.library)
    {
    case SoundLibrary::kMusyX:
        return MusyXSongs(game, profile);

    case SoundLibrary::kRed:
        return RedSounds(game, profile);

    case SoundLibrary::kHal:
        return HalEffects(game, profile);

    case SoundLibrary::kNintendo64:
        return N64Sounds(game, profile);

    case SoundLibrary::kJac:
        {
            std::vector<Sound> sounds = JacPieces(game, profile);
            std::vector<Sound> effects = JacEffects(game, profile);
            sounds.insert(sounds.end(), std::make_move_iterator(effects.begin()),
                          std::make_move_iterator(effects.end()));
            return sounds;
        }

    case SoundLibrary::kJAudio2:
    case SoundLibrary::kJAudio1:
        break;
    }

    return {};
}

// Adds the variants of the profile's port effects to `sounds`, each after its effect (GameProfile::port_effects). A
// variant's label is the effect's, a space and its gcsf_var tag, such as "se3-0000 6=1".
void AddPortVariants(const GameProfile& profile, std::vector<Sound>& sounds)
{
    if (profile.port_effects.empty())
    {
        return;
    }

    std::vector<Sound> listed;
    for (Sound& sound : sounds)
    {
        const auto effect = std::find_if(profile.port_effects.begin(), profile.port_effects.end(),
                                         [&sound](const PortEffect& e) { return e.id == sound.id; });
        listed.push_back(std::move(sound));
        if (effect == profile.port_effects.end())
        {
            continue;
        }

        const Sound base = listed.back();
        for (const int16_t value : effect->values)
        {
            Sound variant = base;
            variant.ports = {{effect->port, value}};
            variant.label = base.label + " " + xsflib::psf::FormatVariables(variant.ports);
            variant.shares_number = true;
            listed.push_back(std::move(variant));
        }
    }

    sounds = std::move(listed);
}

// Puts the variants of the profile's random effects in place of their effects in `sounds`
// (GameProfile::random_effects). A variant's label is the effect's, then " key " and the key, such as "se4-0254 key
// 48". The variants share the effect's number. Gives the profile's random starts their draws
// (GameProfile::random_starts).
void AddRandomVariants(const GameProfile& profile, std::vector<Sound>& sounds)
{
    if (profile.random_effects.empty() && profile.random_starts.empty())
    {
        return;
    }

    std::vector<Sound> listed;
    for (Sound& sound : sounds)
    {
        const auto start = std::find_if(profile.random_starts.begin(), profile.random_starts.end(),
                                        [&sound](const RandomStart& s) { return s.id == sound.id; });
        if (start != profile.random_starts.end())
        {
            sound.id |= uint32_t{start->draws} << kRandomDrawsShift;
        }

        const auto effect = std::find_if(profile.random_effects.begin(), profile.random_effects.end(),
                                         [&sound](const RandomEffect& e) { return e.id == sound.id; });
        if (effect == profile.random_effects.end())
        {
            listed.push_back(std::move(sound));
            continue;
        }

        for (std::size_t i = 0; i < effect->keys.size(); i++)
        {
            Sound variant = sound;
            variant.id = sound.id | uint32_t{effect->keys[i].draws} << kRandomDrawsShift;
            variant.label = sound.label + " key " + std::to_string(effect->keys[i].key);
            variant.shares_number = i > 0;
            listed.push_back(std::move(variant));
        }
    }

    sounds = std::move(listed);
}

// Puts the profile's singing states in place of their piece in `sounds` (GameProfile::singing_states). A state's label
// is the piece's, a space and the state's name, such as "LuigiSings hallway 100 HP". The states share the piece's
// number.
void AddSingingStates(const GameProfile& profile, std::vector<Sound>& sounds)
{
    const auto piece = std::find_if(sounds.begin(), sounds.end(),
                                    [&profile](const Sound& sound) { return sound.id == profile.singing_piece; });
    if (profile.singing_states.empty() || piece == sounds.end())
    {
        return;
    }

    const Sound base = *piece;
    std::vector<Sound> states;
    for (std::size_t i = 0; i < profile.singing_states.size(); i++)
    {
        Sound state = base;
        state.id = base.id | static_cast<uint32_t>(i + 1) << kSingingStateShift;
        state.label = base.label + " " + profile.singing_states[i].name;
        state.shares_number = i > 0;
        states.push_back(std::move(state));
    }

    const auto at = sounds.erase(piece);
    sounds.insert(at, states.begin(), states.end());
}

// Puts the second set of the profile's piece after the piece in `sounds` (GameProfile::second_set_piece). Its label is
// the piece's, a space and the set's name, such as "JA_BGM_SELECT map". The set shares the piece's number.
void AddSecondSet(const GameProfile& profile, std::vector<Sound>& sounds)
{
    const auto piece = std::find_if(sounds.begin(), sounds.end(),
                                    [&profile](const Sound& sound) { return sound.id == profile.second_set_piece; });
    if (profile.second_set_id == 0 || piece == sounds.end())
    {
        return;
    }

    Sound set = *piece;
    set.id = profile.second_set_id;
    set.label += std::string(" ") + profile.second_set_name;
    set.shares_number = true;
    sounds.insert(piece + 1, std::move(set));
}

// Adds a variant to `sounds` after each piece for each of `states` whose `piece` is the piece's ID. `variant` takes a
// state and gives the variant's ID and the text that follows the piece's label. The variants share the piece's number.
template <typename State, typename Variant>
void AddStatesOfPieces(std::span<const State> states, std::vector<Sound>& sounds, const Variant& variant)
{
    if (states.empty())
    {
        return;
    }

    std::vector<Sound> listed;
    for (Sound& sound : sounds)
    {
        listed.push_back(std::move(sound));
        const std::size_t piece = listed.size() - 1;
        for (const State& state : states)
        {
            if (state.piece != listed[piece].id)
            {
                continue;
            }

            const auto [id, suffix] = variant(state);
            Sound added = listed[piece];
            added.id = id;
            added.label += suffix;
            added.shares_number = true;
            listed.push_back(std::move(added));
        }
    }

    sounds = std::move(listed);
}

// Adds the profile's states of pieces of music to `sounds`, each after its piece (GameProfile::music_states). A state's
// label is the piece's, a space and the state's name, such as "enemy2 near". The states share the piece's number.
void AddMusicStates(const GameProfile& profile, std::vector<Sound>& sounds)
{
    AddStatesOfPieces(profile.music_states, sounds,
                      [](const MusicState& state) { return std::pair(state.id, std::string(" ") + state.name); });
}

// Adds the states that the game's code sets for its music during play to `sounds`, each after its piece
// (GameProfile::layer_states). A state's label is the label of the piece, "_" and the suffix of the state, such as
// "JA_BGM_MAIN_01_combo1". The states share the piece's number.
void AddLayerStates(const GameProfile& profile, std::vector<Sound>& sounds)
{
    AddStatesOfPieces(profile.layer_states, sounds, [](const jaudio::JungleBeatState& state)
                      { return std::pair(state.piece | state.bits, std::string("_") + state.suffix); });
}

// Adds the chord effects that the game plays beside its music to `sounds`, each after its piece
// (GameProfile::chord_layers). A layer's label is the label of the piece, "_" and the suffix of the layer, such as
// "JA_BGM_MAIN_01_enemy_sleeping". The layers share the piece's number.
void AddChordLayers(const GameProfile& profile, std::vector<Sound>& sounds)
{
    AddStatesOfPieces(profile.chord_layers, sounds, [&profile](const jaudio::JungleBeatChordLayer& layer)
                      {
                          const auto index = static_cast<uint32_t>(&layer - profile.chord_layers.data());
                          return std::pair(jaudio::ChordLayerId(layer.piece, index), std::string("_") + layer.suffix);
                      });
}

// Gives the pieces whose tracks the game mutes as it starts them the children to mute (GameProfile::muted_starts). A
// start with a suffix becomes a sound after its piece. The sound's label is the piece's with the suffix, such as
// "Z2BGM_FIELD_LINK_DAY_riding", and it shares the piece's number.
void AddMutedStarts(const GameProfile& profile, std::vector<Sound>& sounds)
{
    if (profile.muted_starts.empty())
    {
        return;
    }

    std::vector<Sound> listed;
    for (Sound& sound : sounds)
    {
        const std::size_t piece = listed.size();
        listed.push_back(std::move(sound));
        for (const ntdlib::jaudio::MutedStart& start : profile.muted_starts)
        {
            if (start.sound != listed[piece].id || listed[piece].stream)
            {
                continue;
            }

            if (start.suffix.empty())
            {
                listed[piece].piece_state.muted_children = start.muted_children;
                continue;
            }

            Sound variant = listed[piece];
            variant.label += start.suffix;
            variant.piece_state.muted_children = start.muted_children;
            variant.shares_number = true;
            listed.push_back(std::move(variant));
        }
    }

    sounds = std::move(listed);
}

// Gives the pieces their states (GameProfile::piece_states). A state without a suffix is the piece's. A state with a
// suffix becomes a sound after the piece and its other starts. The sound's label is the piece's with the suffix, such
// as "Z2BGM_TOAL_VILLEGE_status1", and it shares the piece's number.
void AddPieceStates(const GameProfile& profile, std::vector<Sound>& sounds)
{
    if (profile.piece_states.empty())
    {
        return;
    }

    std::vector<Sound> listed;
    for (std::size_t i = 0; i < sounds.size(); i++)
    {
        const std::size_t piece = listed.size();
        listed.push_back(std::move(sounds[i]));
        if (listed[piece].stream || listed[piece].shares_number)
        {
            continue;
        }

        std::vector<Sound> states;
        for (const ntdlib::jaudio::MusicState& state : profile.piece_states)
        {
            if (state.sound != listed[piece].id)
            {
                continue;
            }

            if (state.suffix.empty())
            {
                listed[piece].piece_state = ntdlib::flipper::z2::PieceStateOf(state);
                continue;
            }

            Sound variant = listed[piece];
            variant.label += state.suffix;
            variant.piece_state = ntdlib::flipper::z2::PieceStateOf(state);
            variant.shares_number = true;
            states.push_back(std::move(variant));
        }

        // The piece's other starts stay right after it.
        while (i + 1 < sounds.size() && sounds[i + 1].shares_number)
        {
            listed.push_back(std::move(sounds[++i]));
        }

        listed.insert(listed.end(), std::make_move_iterator(states.begin()), std::make_move_iterator(states.end()));
    }

    sounds = std::move(listed);
}

} // namespace

std::optional<GameSounds> ListGameSounds(const disc::GameFiles& game, const GameProfile& profile, std::string* error)
{
    GameSounds listed;
    std::optional<std::string> problem;
    if (HasStartUpArchive(profile))
    {
        problem = ListJAudioSounds(game, profile, listed);
    }
    else
    {
        try
        {
            listed.sounds = SoundsFromCodeOrFiles(game, profile);
        }
        catch (const xsflib::FormatError& e)
        {
            problem = e.what();
        }
    }

    if (problem)
    {
        if (error)
        {
            *error = std::move(*problem);
        }

        return std::nullopt;
    }

    AddMusicStates(profile, listed.sounds);
    AddChordLayers(profile, listed.sounds); // before the states, which then come first after each piece
    AddLayerStates(profile, listed.sounds);
    AddPortVariants(profile, listed.sounds);
    AddRandomVariants(profile, listed.sounds);
    AddSingingStates(profile, listed.sounds);
    AddSecondSet(profile, listed.sounds);
    AddMutedStarts(profile, listed.sounds);
    AddPieceStates(profile, listed.sounds);

    return listed;
}

} // namespace gcsf::rip
