// SPDX-License-Identifier: MIT

// The list of the driver profiles of the games that GCSF rips (see GameProfile in rip/rip.h), and the lookup of a
// game's profile. Each sound library's games have a file of profiles (see rip/profiles.h).

#include "rip/profiles.h"

#include <string>
#include <vector>

#include "ntdlib/disc/game.h"
#include "xsflib/sha1.h"

#include "rip/rip.h"

namespace gcsf::rip
{

// The profiles in the order in which the error for an unknown game lists them. The list is built on first use: the
// profiles are defined in other files, whose initialisation order is unspecified.
const std::vector<GameProfile>& KnownProfiles()
{
    static const std::vector<GameProfile> profiles = {kFourSwordsUsa,
                                                      kFourSwordsJpn,
                                                      kFourSwordsEur,
                                                      kTwilightPrincessUsa,
                                                      kTwilightPrincessEur,
                                                      kTwilightPrincessJpn,
                                                      kWindWakerUsa,
                                                      kWindWakerJpn,
                                                      kMarioGolfUsa,
                                                      kMarioPowerTennisUsa,
                                                      kWarioWareUsa,
                                                      kLuigisMansionUsa,
                                                      kSuperMarioSunshineUsa,
                                                      kPikminUsa,
                                                      kJungleBeatUsa,
                                                      kDoubleDashUsa,
                                                      kPikmin2Usa,
                                                      kPacManVsUsa,
                                                      kCollectorsEditionUsa,
                                                      kCrystalChroniclesUsa,
                                                      kAnimalCrossingUsa,
                                                      kDoubutsuNoMoriPlusJpn,
                                                      kDoubutsuNoMoriPlusJpnV0,
                                                      kDoubutsuNoMoriEPlusJpn,
                                                      kDoubutsuNoMoriEPlusJpnV0,
                                                      kColosseumBonusUsa,
                                                      kMetroidPrimeUsa,
                                                      kTalesOfSymphoniaUsa,
                                                      kStarFoxAdventuresUsa,
                                                      kRogueLeaderUsa,
                                                      kColosseumUsa,
                                                      kResidentEvilZeroUsa,
                                                      kResidentEvilUsa,
                                                      kMetroidPrime2Usa,
                                                      kPaperMarioUsa,
                                                      kRebelStrikeUsa,
                                                      kPokemonChannelUsa,
                                                      kMarioParty4Usa,
                                                      kMarioParty4UsaRev0,
                                                      kMarioParty4Eur,
                                                      kMarioParty4Jpn,
                                                      kPuzzleCollectionJpn,
                                                      kNdDemoDisc};

    return profiles;
}

const GameProfile* FindProfile(const disc::GameFiles& game)
{
    const std::string sha1 = xsflib::Sha1::Hex(xsflib::Sha1::Of(game.dol));
    const std::string id = game.GameId();
    for (const GameProfile& profile : KnownProfiles())
    {
        const bool named = id == profile.game_id || (profile.program_id && id == profile.program_id);
        if (named && sha1 == profile.dol_sha1)
        {
            return &profile;
        }
    }

    return nullptr;
}

} // namespace gcsf::rip
