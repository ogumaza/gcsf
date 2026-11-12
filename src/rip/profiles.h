// SPDX-License-Identifier: MIT

// The driver profiles of the games that GCSF rips, one file for each sound library (src/rip/profiles_*.cpp).
// KnownProfiles() in profiles.cpp lists them.

#pragma once

#include "rip/rip.h"

namespace gcsf::rip
{

// JAudio2 (profiles_jaudio2.cpp)
extern const GameProfile kFourSwordsUsa;
extern const GameProfile kFourSwordsJpn;
extern const GameProfile kFourSwordsEur;
extern const GameProfile kTwilightPrincessUsa;
extern const GameProfile kTwilightPrincessEur;
extern const GameProfile kTwilightPrincessJpn;
extern const GameProfile kJungleBeatUsa;
extern const GameProfile kDoubleDashUsa;
extern const GameProfile kCollectorsEditionUsa;

// JAudio1 and Jac (profiles_jaudio1.cpp)
extern const GameProfile kWindWakerUsa;
extern const GameProfile kWindWakerJpn;
extern const GameProfile kLuigisMansionUsa;
extern const GameProfile kSuperMarioSunshineUsa;
extern const GameProfile kPikminUsa;
extern const GameProfile kPikmin2Usa;
extern const GameProfile kPacManVsUsa;

// MusyX, with GCSF's MusyX drivers (profiles_musyx.cpp)
extern const GameProfile kMetroidPrimeUsa;
extern const GameProfile kTalesOfSymphoniaUsa;
extern const GameProfile kStarFoxAdventuresUsa;
extern const GameProfile kRogueLeaderUsa;
extern const GameProfile kColosseumUsa;
extern const GameProfile kResidentEvilZeroUsa;
extern const GameProfile kResidentEvilUsa;
extern const GameProfile kMetroidPrime2Usa;
extern const GameProfile kPaperMarioUsa;
extern const GameProfile kRebelStrikeUsa;
extern const GameProfile kPokemonChannelUsa;
extern const GameProfile kPuzzleCollectionJpn;
extern const GameProfile kNdDemoDisc;

// MusyX, with a driver for the game (profiles_musyx_game_drivers.cpp)
extern const GameProfile kMarioGolfUsa;
extern const GameProfile kMarioPowerTennisUsa;
extern const GameProfile kWarioWareUsa;
extern const GameProfile kMarioParty4Usa;
extern const GameProfile kMarioParty4UsaRev0;
extern const GameProfile kMarioParty4Eur;
extern const GameProfile kMarioParty4Jpn;

// the Nintendo 64's sound library (profiles_n64.cpp)
extern const GameProfile kAnimalCrossingUsa;
extern const GameProfile kDoubutsuNoMoriPlusJpn;
extern const GameProfile kDoubutsuNoMoriPlusJpnV0;
extern const GameProfile kDoubutsuNoMoriEPlusJpn;
extern const GameProfile kDoubutsuNoMoriEPlusJpnV0;

// Red (profiles_red.cpp)
extern const GameProfile kCrystalChroniclesUsa;

// HAL Laboratory's sound library (profiles_hal.cpp)
extern const GameProfile kColosseumBonusUsa;

} // namespace gcsf::rip
