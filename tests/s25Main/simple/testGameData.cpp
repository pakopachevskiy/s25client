// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "helpers/EnumRange.h"
#include "gameTypes/GameTypesOutput.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/JobConsts.h"
#include "gameData/ToolConsts.h"
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <boost/test/unit_test.hpp>

/// Tests of static game data, i.e. the constants defined in the source files
BOOST_AUTO_TEST_SUITE(StaticGameDataTests)

BOOST_AUTO_TEST_CASE(ToolMappingMatches)
{
#define TEST_TOOLMAP_SINGLE(s, _, Enumerator)                             \
    static_assert(TOOL_TO_GOOD[Tool::Enumerator] == GoodType::Enumerator, \
                  "Mismatch for " BOOST_PP_STRINGIZE(Enumerator));
    // Generate a static assert for each enumerator
#define TEST_TOOLMAP(...) BOOST_PP_SEQ_FOR_EACH(TEST_TOOLMAP_SINGLE, 0, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__))

    TEST_TOOLMAP(Tongs, Hammer, Axe, Saw, PickAxe, Shovel, Crucible, RodAndLine, Scythe, Cleaver, Rollingpin, Bow);
    BOOST_TEST(true);

#undef TEST_TOOLMAP_SINGLE
#undef TEST_TOOLMAP
}

BOOST_AUTO_TEST_CASE(NationSpecificJobBobs)
{
    // Helper is not nation specific
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Helper].getBobId(Nation::Vikings)
               == JOB_SPRITE_CONSTS[Job::Helper].getBobId(Nation::Africans));
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Helper].getBobId(Nation::Vikings)
               == JOB_SPRITE_CONSTS[Job::Helper].getBobId(Nation::Babylonians));
    // Soldiers are
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Private].getBobId(Nation::Vikings)
               != JOB_SPRITE_CONSTS[Job::Private].getBobId(Nation::Africans));
    // Non native nations come after native ones
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Private].getBobId(Nation::Vikings)
               < JOB_SPRITE_CONSTS[Job::Private].getBobId(Nation::Babylonians));
    // Same for scouts
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Scout].getBobId(Nation::Vikings)
               != JOB_SPRITE_CONSTS[Job::Scout].getBobId(Nation::Africans));
    BOOST_TEST(JOB_SPRITE_CONSTS[Job::Scout].getBobId(Nation::Vikings)
               < JOB_SPRITE_CONSTS[Job::Scout].getBobId(Nation::Babylonians));
}

BOOST_AUTO_TEST_CASE(ProductionBuildingsAreNobUsual)
{
    for(const auto bld : helpers::enumRange<BuildingType>())
    {
        if(!BuildingProperties::IsValid(bld))
            continue;
        // Only nobUsuals can produce wares (though not all do)
        if(BLD_WORK_DESC[bld].producedWare)
        {
            BOOST_TEST_INFO("bld: " << bld);
            BOOST_TEST(BuildingProperties::IsUsual(bld));
        }
    }
}

BOOST_AUTO_TEST_CASE(FarmhandWorkRadiusMatchesWorkerSearch)
{
    BOOST_TEST(GetFarmhandWorkRadius(Job::Carpenter) == 0u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Hunter) == 2u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Farmer) == 2u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Winegrower) == 2u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::CharBurner) == 3u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Woodcutter) == 6u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Forester) == 6u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Fisher) == 7u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Stonemason) == 8u);
    BOOST_TEST(GetFarmhandWorkRadius(Job::Baker) == 0u);
}

BOOST_AUTO_TEST_CASE(BuildingWorkerRadiusMatchesBuildingJob)
{
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Woodcutter) == 6u);
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Forester) == 6u);
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Fishery) == 7u);
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Quarry) == 8u);
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Sawmill) == 0u);
    BOOST_TEST(GetBuildingWorkerRadius(BuildingType::Well) == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
