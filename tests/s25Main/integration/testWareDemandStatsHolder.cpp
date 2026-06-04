// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareDemandStatsHolder.h"

#include "GamePlayer.h"
#include "WareProductionStatsHolder.h"
#include "ai/AIPlayer.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobUsual.h"
#include "factories/BuildingFactory.h"
#include "gameData/BuildingConsts.h"
#include "gameData/JobConsts.h"
#include "gameTypes/Inventory.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include <boost/optional/optional.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>

namespace {

using DemandWorld = WorldWithGCExecution<1, 24, 22>;

struct WareDemandStatsHolderFixture : DemandWorld
{
    WareDemandStatsHolderFixture() { WareDemandStatsHolder::Reset(); }
    ~WareDemandStatsHolderFixture() { WareDemandStatsHolder::Reset(); }
};

struct MockAI final : AIPlayer
{
    MockAI(unsigned char playerId, const GameWorldBase& world) : AIPlayer(playerId, world, AI::Level::Easy)
    {
        std::fill(wanted.begin(), wanted.end(), 0u);
    }

    void RunGF(unsigned /*gf*/, bool /*gfisnwf*/) override {}
    void OnChatMessage(unsigned /*sendPlayerId*/, ChatDestination, const std::string& /*msg*/) override {}

    boost::optional<unsigned> GetNumBuildingsWanted(const BuildingType type) const override
    {
        if(!hasWantedData)
            return boost::none;
        return wanted[type];
    }

    bool hasWantedData = true;
    helpers::EnumArray<unsigned, BuildingType> wanted;
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

unsigned CountAvailableBuilders(const GamePlayer& player)
{
    const Inventory& inventory = player.GetInventory();
    return inventory[Job::Builder] + std::min(inventory[Job::Helper], inventory[GoodType::Hammer]);
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

BOOST_AUTO_TEST_CASE(AIConstructionDemandUsesWantedProportionsAndAvailableBuilders)
{
    Inventory inventory;
    inventory.Add(Job::Builder);
    inventory.Add(Job::Helper);
    inventory.Add(GoodType::Hammer);
    world.GetSpecObj<nobBaseWarehouse>(hqPos)->AddGoods(inventory, true);

    const unsigned availableBuilders = CountAvailableBuilders(world.GetPlayer(0));
    BOOST_TEST_REQUIRE(availableBuilders > 0u);

    MockAI ai(0, world);
    ai.wanted[BuildingType::Barracks] = availableBuilders;
    ai.wanted[BuildingType::Guardhouse] = availableBuilders;

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, &ai);
    const unsigned expectedBarracks = (availableBuilders + 1u) / 2u;
    const unsigned expectedGuardhouses = availableBuilders / 2u;
    const unsigned expectedBoards =
      expectedBarracks * BUILDING_COSTS[BuildingType::Barracks].boards
      + expectedGuardhouses * BUILDING_COSTS[BuildingType::Guardhouse].boards;
    const unsigned expectedStones =
      expectedBarracks * BUILDING_COSTS[BuildingType::Barracks].stones
      + expectedGuardhouses * BUILDING_COSTS[BuildingType::Guardhouse].stones;

    BOOST_TEST(demand.calculated[GoodType::Boards]);
    BOOST_TEST(demand.calculated[GoodType::Stones]);
    BOOST_TEST(demand.demand[GoodType::Boards] == expectedBoards);
    BOOST_TEST(demand.demand[GoodType::Stones] == expectedStones);
}

BOOST_AUTO_TEST_CASE(AIConstructionDemandAllocatesAllAvailableBuildersEvenWhenWantedDeficitIsSmaller)
{
    const unsigned availableBuilders = CountAvailableBuilders(world.GetPlayer(0));
    BOOST_TEST_REQUIRE(availableBuilders >= 20u);

    MockAI ai(0, world);
    ai.wanted[BuildingType::Barracks] = 1;

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, &ai);
    BOOST_TEST(demand.demand[GoodType::Boards] == availableBuilders * BUILDING_COSTS[BuildingType::Barracks].boards);
    BOOST_TEST(demand.demand[GoodType::Boards] >= 40u);
}

BOOST_AUTO_TEST_CASE(AIConstructionDemandIsSkippedWhenWantedCountsAreUnavailable)
{
    Inventory inventory;
    inventory.Add(Job::Builder, 3);
    inventory.Add(Job::Helper, 3);
    inventory.Add(GoodType::Hammer, 3);
    world.GetSpecObj<nobBaseWarehouse>(hqPos)->AddGoods(inventory, true);

    MockAI ai(0, world);
    ai.hasWantedData = false;

    const WareDemandSnapshot& demand = WareDemandStatsHolder::GetCurrentDemand(world, 0, 0, &ai);
    BOOST_TEST(demand.demand[GoodType::Boards] == 0u);
    BOOST_TEST(demand.demand[GoodType::Stones] == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
