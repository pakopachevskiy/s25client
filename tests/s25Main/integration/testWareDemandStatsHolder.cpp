// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareDemandStatsHolder.h"

#include "GamePlayer.h"
#include "WareProductionStatsHolder.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobUsual.h"
#include "factories/BuildingFactory.h"
#include "gameData/BuildingConsts.h"
#include "gameData/JobConsts.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include <boost/test/unit_test.hpp>

namespace {

using DemandWorld = WorldWithGCExecution<1, 24, 22>;

struct WareDemandStatsHolderFixture : DemandWorld
{
    WareDemandStatsHolderFixture() { WareDemandStatsHolder::Reset(); }
    ~WareDemandStatsHolderFixture() { WareDemandStatsHolder::Reset(); }
};

MapPoint GetRouteEnd(const GameWorldBase& world, MapPoint pt, const std::vector<Direction>& route)
{
    for(const Direction dir : route)
        pt = world.GetNeighbour(pt, dir);
    return pt;
}

nobUsual& CreateStaffedBuilding(DemandWorld& fixture, const BuildingType type)
{
    const MapPoint hqFlag = fixture.world.GetNeighbour(fixture.hqPos, Direction::SouthEast);
    const std::vector<Direction> route(3, Direction::East);
    fixture.BuildRoad(hqFlag, false, route);
    const MapPoint buildingFlag = GetRouteEnd(fixture.world, hqFlag, route);
    auto* building = dynamic_cast<nobUsual*>(BuildingFactory::CreateBuilding(
      fixture.world, type, fixture.world.GetNeighbour(buildingFlag, Direction::NorthWest), fixture.curPlayer,
      Nation::Romans));
    BOOST_TEST_REQUIRE(building);

    const unsigned executedGfs = rttr_exec_till_ct_gf(fixture.em, 500, [&] { return building->HasWorker(); });
    (void)executedGfs;
    BOOST_TEST_REQUIRE(building->HasWorker());
    return *building;
}

unsigned CyclesPerWindow(const BuildingType type)
{
    const JobConst& jobConst = JOB_CONSTS[*BLD_WORK_DESC[type].job];
    const unsigned cycleGf = jobConst.wait1_length + jobConst.work_length + jobConst.wait2_length + 40u;
    return (WareProductionStatsHolder::WINDOW_SIZE_GF + cycleGf - 1u) / cycleGf;
}

MapPoint FindBuildablePoint(const GameWorldBase& world, const BuildingType type)
{
    const MapPoint hqPos = world.GetPlayer(0).GetHQPos();
    for(const MapPoint pt : world.GetPointsInRadiusWithCenter(hqPos, 9))
    {
        if(world.CalcDistance(pt, hqPos) < 5)
            continue;
        if(canUseBq(world.GetNode(pt).bq, BUILDING_SIZE[type]))
            return pt;
    }
    return MapPoint::Invalid();
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(WareDemandStatsHolderTests, WareDemandStatsHolderFixture)

BOOST_AUTO_TEST_CASE(RecurringDemandIsCachedPer5000GfWindowAndResetClearsCache)
{
    nobUsual& sawmill = CreateStaffedBuilding(*this, BuildingType::Sawmill);

    const WareDemandSnapshot& initialDemand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);
    BOOST_TEST(initialDemand.demand[GoodType::Wood] == CyclesPerWindow(BuildingType::Sawmill));

    sawmill.SetProductionEnabled(false);
    const WareDemandSnapshot& cachedDemand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 100, nullptr);
    BOOST_TEST(cachedDemand.demand[GoodType::Wood] == CyclesPerWindow(BuildingType::Sawmill));

    const WareDemandSnapshot& nextWindowDemand =
      WareDemandStatsHolder::GetCurrentDemand(world, 0, WareProductionStatsHolder::WINDOW_SIZE_GF, nullptr);
    BOOST_TEST(nextWindowDemand.demand[GoodType::Wood] == 0u);

    sawmill.SetProductionEnabled(true);
    const WareDemandSnapshot& cachedNextWindowDemand =
      WareDemandStatsHolder::GetCurrentDemand(world, 0, WareProductionStatsHolder::WINDOW_SIZE_GF + 1u, nullptr);
    BOOST_TEST(cachedNextWindowDemand.demand[GoodType::Wood] == 0u);

    WareDemandStatsHolder::Reset();
    const WareDemandSnapshot& resetDemand =
      WareDemandStatsHolder::GetCurrentDemand(world, 0, WareProductionStatsHolder::WINDOW_SIZE_GF + 1u, nullptr);
    BOOST_TEST(resetDemand.demand[GoodType::Wood] == CyclesPerWindow(BuildingType::Sawmill));
}

BOOST_AUTO_TEST_CASE(UnsupportedGoodsRemainUncalculated)
{
    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);

    BOOST_TEST(demand.calculated[GoodType::Wood]);
    BOOST_TEST(!demand.calculated[GoodType::Beer]);
    BOOST_TEST(!demand.calculated[GoodType::Coins]);
    BOOST_TEST(!demand.calculated[GoodType::Sword]);
    BOOST_TEST(!demand.calculated[GoodType::ShieldRomans]);
    BOOST_TEST(!demand.calculated[GoodType::ShieldVikings]);
    BOOST_TEST(!demand.calculated[GoodType::Tongs]);
}

BOOST_AUTO_TEST_CASE(FixedCycleDemandUsesDocumentedCycleFormula)
{
    CreateStaffedBuilding(*this, BuildingType::Sawmill);

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);
    BOOST_TEST(CyclesPerWindow(BuildingType::Sawmill) == 9u);
    BOOST_TEST(demand.demand[GoodType::Wood] == 9u);
}

BOOST_AUTO_TEST_CASE(MineFoodDemandIsSplitAcrossAcceptedFoodWares)
{
    CreateStaffedBuilding(*this, BuildingType::GraniteMine);

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);
    const unsigned expectedFoodDemand = CyclesPerWindow(BuildingType::GraniteMine);
    BOOST_TEST(demand.demand[GoodType::Fish] + demand.demand[GoodType::Meat] + demand.demand[GoodType::Bread]
               == expectedFoodDemand);
    BOOST_TEST(expectedFoodDemand == 5u);
}

BOOST_AUTO_TEST_CASE(ConstructionDemandSumsFullCostsOfCurrentBuildingSites)
{
    const MapPoint woodcutterPos = FindBuildablePoint(world, BuildingType::Woodcutter);
    BOOST_TEST_REQUIRE(woodcutterPos.isValid());
    world.SetBuildingSite(BuildingType::Woodcutter, woodcutterPos, 0);
    BOOST_TEST_REQUIRE(world.GetSpecObj<noBuildingSite>(woodcutterPos));

    const MapPoint farmPos = FindBuildablePoint(world, BuildingType::Farm);
    BOOST_TEST_REQUIRE(farmPos.isValid());
    world.SetBuildingSite(BuildingType::Farm, farmPos, 0);
    BOOST_TEST_REQUIRE(world.GetSpecObj<noBuildingSite>(farmPos));

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);
    const unsigned expectedBoards =
      BUILDING_COSTS[BuildingType::Woodcutter].boards + BUILDING_COSTS[BuildingType::Farm].boards;
    const unsigned expectedStones =
      BUILDING_COSTS[BuildingType::Woodcutter].stones + BUILDING_COSTS[BuildingType::Farm].stones;

    BOOST_TEST(demand.calculated[GoodType::Boards]);
    BOOST_TEST(demand.calculated[GoodType::Stones]);
    BOOST_TEST(demand.demand[GoodType::Boards] == expectedBoards);
    BOOST_TEST(demand.demand[GoodType::Stones] == expectedStones);
}

BOOST_AUTO_TEST_CASE(ConstructionDemandIsZeroWithoutCurrentBuildingSites)
{
    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, nullptr);
    BOOST_TEST(demand.calculated[GoodType::Boards]);
    BOOST_TEST(demand.calculated[GoodType::Stones]);
    BOOST_TEST(demand.demand[GoodType::Boards] == 0u);
    BOOST_TEST(demand.demand[GoodType::Stones] == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
