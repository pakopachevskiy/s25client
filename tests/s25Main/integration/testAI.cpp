// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "PointOutput.h"
#include "RttrForeachPt.h"
#include "Ware.h"
#include "ai/AIPlayer.h"
#include "ai/AIQueryService.h"
#include "ai/aijh/config/AIConfig.h"
#include "ai/aijh/planning/AIConstruction.h"
#include "ai/aijh/runtime/AIRoadController.h"
#include "ai/aijh/runtime/AIRoadWorkload.h"
#include "ai/aijh/runtime/AIPlayerJH.h"
#include "buildings/noBuilding.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobShipYard.h"
#include "factories/AIFactory.h"
#include "factories/BuildingFactory.h"
#include "figures/nofPassiveSoldier.h"
#include "helpers/containerUtils.h"
#include "network/GameMessage_Chat.h"
#include "notifications/NodeNote.h"
#include "worldFixtures/terrainHelpers.h"
#include "worldFixtures/WorldWithGCExecution.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noTree.h"
#include "RoadSegment.h"
#include "gameTypes/GameTypesOutput.h"
#include "gameData/BuildingProperties.h"
#include "gameData/MilitaryConsts.h"
#include "rttr/test/random.hpp"
#include <boost/test/unit_test.hpp>
#include <memory>
#include <set>

namespace {
// We need border land
using BiggerWorldWithGCExecution = WorldWithGCExecution<1, 24, 22>;
using WaterwayWorldWithGCExecution = WorldWithGCExecution<1, 24, 22>;
using EmptyWorldFixture1P = WorldFixture<CreateEmptyWorld, 1>;
using EmptyWorldFixture2P = WorldFixture<CreateEmptyWorld, 2>;

template<class T_Col>
inline bool containsBldType(const T_Col& collection, BuildingType type)
{
    return helpers::contains_if(collection,
                                [type](const noBaseBuilding* bld) { return bld->GetBuildingType() == type; });
}

inline bool playerHasBld(const GamePlayer& player, BuildingType type)
{
    const BuildingRegister& blds = player.GetBuildingRegister();
    if(containsBldType(blds.GetBuildingSites(), type))
        return true;
    if(BuildingProperties::IsMilitary(type))
        return containsBldType(blds.GetMilitaryBuildings(), type);
    if(BuildingProperties::IsWareHouse(type)) // Includes harbors
        return containsBldType(blds.GetStorehouses(), type);
    return !blds.GetBuildings(type).empty();
}

struct MockAI final : public AIPlayer
{
    MockAI(unsigned char playerId, const GameWorldBase& gwb, const AI::Level level) : AIPlayer(playerId, gwb, level) {}
    // LCOV_EXCL_START
    void RunGF(unsigned /*gf*/, bool /*gfisnwf*/) override {}
    void OnChatMessage(unsigned /*sendPlayerId*/, ChatDestination, const std::string& /*msg*/) override {}
    // LCOV_EXCL_STOP
};

void SetAllOwned(GameWorld& world)
{
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
        world.SetOwner(pt, 1);
}

MapPoint GetRouteEnd(const GameWorldBase& world, MapPoint pt, const std::vector<Direction>& route)
{
    for(const Direction dir : route)
        pt = world.GetNeighbour(pt, dir);
    return pt;
}

void SetWaterwayTerrain(GameWorld& world, MapPoint pt, const std::vector<Direction>& route)
{
    const auto water = GetWaterTerrain(world.GetDescription());
    for(std::size_t i = 0; i + 1 < route.size(); ++i)
    {
        pt = world.GetNeighbour(pt, route[i]);
        for(const Direction dir : helpers::EnumRange<Direction>{})
            setRightTerrain(world, pt, dir, water);
    }
}

struct WaterwayShortcut
{
    MapPoint source;
    MapPoint target;
    std::vector<Direction> waterRoute{Direction::East, Direction::East};
};

WaterwayShortcut CreateWaterwayShortcut(GameWorld& world, unsigned playerId, bool buildLandDetour)
{
    SetAllOwned(world);
    WaterwayShortcut shortcut;
    shortcut.source = world.GetNeighbour(world.GetPlayer(playerId).GetHQPos(), Direction::SouthEast);
    shortcut.target = GetRouteEnd(world, shortcut.source, shortcut.waterRoute);
    world.SetFlag(shortcut.target, playerId);
    if(buildLandDetour)
    {
        std::vector<Direction> landRoute(5, Direction::SouthEast);
        landRoute.insert(landRoute.end(), 2, Direction::East);
        landRoute.insert(landRoute.end(), 5, Direction::NorthWest);
        world.BuildRoad(playerId, false, shortcut.source, landRoute);
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(shortcut.source)->GetRoute(Direction::SouthEast));
    }
    SetWaterwayTerrain(world, shortcut.source, shortcut.waterRoute);
    return shortcut;
}

struct ScopedRoadRouteBQPenalty
{
    explicit ScopedRoadRouteBQPenalty(double value)
        : oldRoadRoute(AI_CONFIG.bqPenalty.roadRoute),
          oldWeightedSearch(AI_CONFIG.bqPenalty.roadRouteWeightedSearch)
    {
        AI_CONFIG.bqPenalty.roadRoute = value;
        AI_CONFIG.bqPenalty.roadRouteWeightedSearch = value > 0.0 && oldWeightedSearch;
    }

    ~ScopedRoadRouteBQPenalty()
    {
        AI_CONFIG.bqPenalty.roadRoute = oldRoadRoute;
        AI_CONFIG.bqPenalty.roadRouteWeightedSearch = oldWeightedSearch;
    }

    double oldRoadRoute;
    bool oldWeightedSearch;
};
} // namespace

// Note game command execution is emulated to be like the ones send via network:
// Run "Network Frame" then execute GCs from last NWF
// Also use "HARD" AI for faster execution
BOOST_AUTO_TEST_SUITE(AI)

BOOST_FIXTURE_TEST_CASE(PlayerHasBld_IsCorrect, EmptyWorldFixture1P)
{
    const GamePlayer& player = world.GetPlayer(0);
    BOOST_TEST(playerHasBld(player, BuildingType::Headquarters));
    MapPoint pos = player.GetHQPos();
    for(const auto bld : {BuildingType::Woodcutter, BuildingType::Barracks, BuildingType::Storehouse})
    {
        pos = world.MakeMapPoint(pos + Position(2, 0));
        BOOST_TEST_INFO(bld);
        BOOST_TEST(!playerHasBld(player, bld));
        BuildingFactory::CreateBuilding(world, bld, pos, player.GetPlayerId(), Nation::Romans);
        BOOST_TEST_INFO(bld);
        BOOST_TEST(playerHasBld(player, bld));
    }
}

BOOST_FIXTURE_TEST_CASE(AIChat, EmptyWorldFixture2P)
{
    MockAI ai(1, world, AI::Level::Easy);
    ai.getAIInterface().Chat("Hello players!");
    ai.getAIInterface().Chat("2nd Message!", ChatDestination::Allies);
    const auto msgs = ai.getAIInterface().FetchChatMessages();
    BOOST_TEST_REQUIRE(msgs.size() == 2u);
    BOOST_TEST(msgs[0]->player == 1u);
    BOOST_TEST(msgs[0]->destination == ChatDestination::All);
    BOOST_TEST(msgs[0]->text == "Hello players!");
    BOOST_TEST(msgs[1]->player == 1u);
    BOOST_TEST(msgs[1]->destination == ChatDestination::Allies);
    BOOST_TEST(msgs[1]->text == "2nd Message!");
    // Messages cleared by first call
    BOOST_TEST(ai.getAIInterface().FetchChatMessages().empty());
    // Can readd
    const auto dest = rttr::test::randomEnum<ChatDestination>();
    ai.getAIInterface().Chat("Hello again!", dest);
    // Iterate just like in ExecuteNWF function
    for(auto& msg : ai.getAIInterface().FetchChatMessages())
    {
        BOOST_TEST(msg->player == 1u);
        BOOST_TEST(msg->destination == dest);
        BOOST_TEST(msg->text == "Hello again!");
    }
}

BOOST_FIXTURE_TEST_CASE(PointRating_IsUnavailableForUnsuitableNode, WorldWithGCExecution<1>)
{
    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    MapPoint ratedPt = MapPoint::Invalid();
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(!ratedPt.isValid() && ai.GetPointRating(BuildingType::Well, pt))
            ratedPt = pt;
    }
    BOOST_TEST_REQUIRE(ratedPt.isValid());

    ai.GetAINode(ratedPt).farmed = true;
    BOOST_TEST(!ai.GetPointRating(BuildingType::Well, ratedPt));
}

BOOST_FIXTURE_TEST_CASE(KeepBQUpdated, BiggerWorldWithGCExecution)
{
    // Place some trees to reduce BQ at some points
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(pt.x % 4 == 0 && pt.y % 2 == 0 && world.GetNode(pt).bq == BuildingQuality::Castle
           && world.CalcDistance(pt, hqPos) > 6)
            world.SetNO(pt, new noTree(pt, 0, 3));
    }
    world.InitAfterLoad();

    auto ai = AIFactory::Create(AI::Info(AI::Type::Default, AI::Level::Hard), curPlayer, world);
    const AIJH::AIPlayerJH& aijh = static_cast<AIJH::AIPlayerJH&>(*ai);

    const auto assertBqEqualOnWholeMap = [this, &aijh](const unsigned lineNr) {
        BOOST_TEST_CONTEXT("Line #" << lineNr)
        RTTR_FOREACH_PT(MapPoint, world.GetSize())
        {
            BOOST_TEST_INFO(pt);
            BOOST_TEST(this->world.GetBQ(pt, curPlayer) == aijh.GetAINode(pt).bq);
        }
    };
    const auto assertBqEqualAround = [this, &aijh](const unsigned lineNr, MapPoint pt, unsigned radius) {
        BOOST_TEST_CONTEXT("Line #" << lineNr)
        world.CheckPointsInRadius(
          pt, radius,
          [&](const MapPoint curPt, unsigned) {
              BOOST_TEST_INFO(curPt);
              BOOST_TEST(this->world.GetBQ(curPt, curPlayer) == aijh.GetAINode(curPt).bq);
              return false;
          },
          true);
    };

    // 100GFs for initialization
    for(unsigned gf = 0; gf < 100; ++gf)
    {
        em.ExecuteNextGF();
        ai->RunGF(em.GetCurrentGF(), true);
    }
    assertBqEqualOnWholeMap(__LINE__);

    // Set and destroy flag everywhere possible
    std::vector<MapPoint> possibleFlagNodes;
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(world.GetBQ(pt, curPlayer) != BuildingQuality::Nothing && !world.IsFlagAround(pt))
            possibleFlagNodes.push_back(pt);
    }
    for(const MapPoint flagPos : possibleFlagNodes)
    {
        this->SetFlag(flagPos);
        BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagPos));
        em.ExecuteNextGF();
        ai->RunGF(em.GetCurrentGF(), true);
        assertBqEqualAround(__LINE__, flagPos, 3);

        this->DestroyFlag(flagPos);
        BOOST_TEST_REQUIRE(!world.GetSpecObj<noFlag>(flagPos));
        em.ExecuteNextGF();
        ai->RunGF(em.GetCurrentGF(), true);
        assertBqEqualAround(__LINE__, flagPos, 3);
    }

    // Build road
    const MapPoint flagPos = world.MakeMapPoint(world.GetNeighbour(hqPos, Direction::SouthEast) + Position(4, 0));
    this->BuildRoad(world.GetNeighbour(hqPos, Direction::SouthEast), false, std::vector<Direction>(4, Direction::East));
    BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(flagPos)->GetRoute(Direction::West));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), true);
    assertBqEqualAround(__LINE__, flagPos, 6);

    // Destroy road and flag
    this->DestroyFlag(flagPos);
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), true);
    assertBqEqualAround(__LINE__, flagPos, 6);

    // Build building
    const MapPoint bldPos = world.MakeMapPoint(hqPos + Position(5, 0));
    this->SetBuildingSite(bldPos, BuildingType::Barracks);
    BOOST_TEST_REQUIRE(world.GetSpecObj<noBuildingSite>(bldPos));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), true);
    assertBqEqualAround(__LINE__, bldPos, 6);

    this->BuildRoad(world.GetNeighbour(bldPos, Direction::SouthEast), false,
                    std::vector<Direction>(5, Direction::West));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), true);
    RTTR_EXEC_TILL(2000, world.GetSpecObj<noBuilding>(bldPos));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), false);
    assertBqEqualOnWholeMap(__LINE__);

    // Gain land
    const nobMilitary* bld = world.GetSpecObj<nobMilitary>(bldPos);
    for(unsigned i = 0; i < 500; i++)
    {
        em.ExecuteNextGF();
        ai->RunGF(em.GetCurrentGF(), false);
        if(bld->GetNumTroops() > 0u)
            break;
    }
    BOOST_TEST_REQUIRE(bld->GetNumTroops() > 0u);
    assertBqEqualOnWholeMap(__LINE__);

    // Move the boundary by one node
    std::set<MapPoint, MapPointLess> outerBoundaryNodes;
    std::vector<MapPoint> borderNodes;
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        if(world.IsBorderNode(pt, curPlayer + 1))
        {
            borderNodes.push_back(pt);
            world.CheckPointsInRadius(
              pt, 1,
              [&outerBoundaryNodes, &world = this->world, curPlayer = this->curPlayer](const MapPoint curPt, unsigned) {
                  if(world.GetNode(curPt).owner != curPlayer + 1)
                      outerBoundaryNodes.insert(curPt);
                  return false;
              },
              false);
        }
    }
    // Once to outside
    for(const MapPoint pt : outerBoundaryNodes)
        world.SetOwner(pt, curPlayer + 1);
    world.RecalcBorderStones(Position(0, 0), Extent(world.GetSize()));
    for(const MapPoint pt : outerBoundaryNodes)
        world.GetNotifications().publish(NodeNote(NodeNote::Owner, pt));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), false);
    assertBqEqualOnWholeMap(__LINE__);

    // And back
    for(const MapPoint pt : outerBoundaryNodes)
        world.SetOwner(pt, 0);
    world.RecalcBorderStones(Position(0, 0), Extent(world.GetSize()));
    for(const MapPoint pt : outerBoundaryNodes)
        world.GetNotifications().publish(NodeNote(NodeNote::Owner, pt));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), false);
    assertBqEqualOnWholeMap(__LINE__);

    // And once to inside
    for(const MapPoint pt : borderNodes)
        world.SetOwner(pt, 0);
    world.RecalcBorderStones(Position(0, 0), Extent(world.GetSize()));
    for(const MapPoint pt : borderNodes)
        world.GetNotifications().publish(NodeNote(NodeNote::Owner, pt));
    em.ExecuteNextGF();
    ai->RunGF(em.GetCurrentGF(), false);
    assertBqEqualOnWholeMap(__LINE__);
}

BOOST_FIXTURE_TEST_CASE(BuildAlternativeRoad_ShortcutPolicyRejectsLongerRoad, WorldWithGCExecution<1>)
{
    const MapPoint sourceFlagPos = world.GetNeighbour(hqPos, Direction::SouthEast);
    this->BuildRoad(sourceFlagPos, false, std::vector<Direction>(4, Direction::East));
    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(sourceFlagPos);
    BOOST_TEST_REQUIRE(sourceFlag);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    std::vector<Direction> route;
    BOOST_TEST(!ai.GetConstruction().BuildAlternativeRoad(sourceFlag, route, AIJH::AlternativeRoadPolicy::ShortcutOnly));
    BOOST_TEST(ai.FetchGameCommands().empty());
}

BOOST_FIXTURE_TEST_CASE(WareRoadPath_ReturnsOrderedSegmentsAndClearsThemOnFailure, BiggerWorldWithGCExecution)
{
    SetAllOwned(world);
    const MapPoint source = world.GetNeighbour(hqPos, Direction::SouthEast);
    const std::vector<Direction> firstRoute(3, Direction::East);
    const MapPoint middle = GetRouteEnd(world, source, firstRoute);
    const std::vector<Direction> secondRoute(3, Direction::SouthEast);
    const MapPoint target = GetRouteEnd(world, middle, secondRoute);
    this->BuildRoad(source, false, firstRoute);
    this->BuildRoad(middle, false, secondRoute);

    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(source);
    const noFlag* middleFlag = world.GetSpecObj<noFlag>(middle);
    const noFlag* targetFlag = world.GetSpecObj<noFlag>(target);
    BOOST_TEST_REQUIRE(sourceFlag);
    BOOST_TEST_REQUIRE(middleFlag);
    BOOST_TEST_REQUIRE(targetFlag);

    AIQueryService queries(world, curPlayer);
    std::vector<const RoadSegment*> traversedSegments;
    BOOST_TEST_REQUIRE(queries.FindPathForWareOnRoads(*sourceFlag, *targetFlag, nullptr, &traversedSegments));
    const std::vector<const RoadSegment*> expectedSegments{sourceFlag->GetRoute(Direction::East),
                                                           middleFlag->GetRoute(Direction::SouthEast)};
    BOOST_TEST(traversedSegments == expectedSegments, boost::test_tools::per_element());

    const MapPoint disconnected = world.MakeMapPoint(Position(source.x - 4, source.y - 4));
    world.SetFlag(disconnected, curPlayer);
    BOOST_TEST(!queries.FindPathForWareOnRoads(*sourceFlag, *world.GetSpecObj<noFlag>(disconnected), nullptr,
                                               &traversedSegments));
    BOOST_TEST(traversedSegments.empty());
}

BOOST_FIXTURE_TEST_CASE(WareRoadPath_ReturnsWaterwaySegments, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, false);
    world.BuildRoad(curPlayer, true, shortcut.source, shortcut.waterRoute);
    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(shortcut.source);
    const noFlag* targetFlag = world.GetSpecObj<noFlag>(shortcut.target);
    BOOST_TEST_REQUIRE(sourceFlag);
    BOOST_TEST_REQUIRE(targetFlag);

    AIQueryService queries(world, curPlayer);
    std::vector<const RoadSegment*> traversedSegments;
    BOOST_TEST_REQUIRE(queries.FindPathForWareOnRoads(*sourceFlag, *targetFlag, nullptr, &traversedSegments));
    BOOST_TEST_REQUIRE(traversedSegments.size() == 1u);
    BOOST_TEST(traversedSegments.front() == sourceFlag->GetRoute(Direction::East));
    BOOST_TEST(static_cast<int>(traversedSegments.front()->GetRoadType()) == static_cast<int>(RoadType::Water));
}

BOOST_FIXTURE_TEST_CASE(RoadWorkload_AccumulatesWareEdgesAndRefreshesDisabledProducer, BiggerWorldWithGCExecution)
{
    SetAllOwned(world);
    const MapPoint hqFlag = world.GetNeighbour(hqPos, Direction::SouthEast);
    const std::vector<Direction> producerRoute(4, Direction::East);
    const MapPoint producerFlag = GetRouteEnd(world, hqFlag, producerRoute);
    const std::vector<Direction> consumerRoute(4, Direction::East);
    const MapPoint consumerFlag = GetRouteEnd(world, producerFlag, consumerRoute);
    const std::vector<Direction> warehouseRoute(4, Direction::SouthWest);
    const MapPoint warehouseFlag = GetRouteEnd(world, producerFlag, warehouseRoute);
    const std::vector<Direction> unusedRoute(3, Direction::NorthEast);
    const MapPoint unusedFlag = GetRouteEnd(world, producerFlag, unusedRoute);

    this->BuildRoad(hqFlag, false, producerRoute);
    this->BuildRoad(producerFlag, false, consumerRoute);
    this->BuildRoad(producerFlag, false, warehouseRoute);
    this->BuildRoad(producerFlag, false, unusedRoute);

    auto* producer = dynamic_cast<nobUsual*>(BuildingFactory::CreateBuilding(
      world, BuildingType::Sawmill, world.GetNeighbour(producerFlag, Direction::NorthWest), curPlayer, Nation::Romans));
    BOOST_TEST_REQUIRE(producer);
    auto* consumer = dynamic_cast<nobUsual*>(BuildingFactory::CreateBuilding(
      world, BuildingType::Metalworks, world.GetNeighbour(consumerFlag, Direction::NorthWest), curPlayer,
      Nation::Romans));
    BOOST_TEST_REQUIRE(consumer);
    BOOST_TEST_REQUIRE(BuildingFactory::CreateBuilding(
      world, BuildingType::Storehouse, world.GetNeighbour(warehouseFlag, Direction::NorthWest), curPlayer,
      Nation::Romans));

    AIQueryService queries(world, curPlayer);
    AIJH::AIRoadWorkload workload(queries, world);
    workload.Refresh();

    const std::vector<AIJH::RoadWorkloadSegment> hotSegments = workload.GetHotSegments(3);
    BOOST_TEST_REQUIRE(!hotSegments.empty());
    BOOST_TEST(hotSegments.front().workload == 3u);
    BOOST_TEST(workload.GetHotSegments(4).empty());

    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 3u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::East)).value() == 3u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::SouthWest)).value() == 0u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::NorthEast)).value() == 0u);
    BOOST_TEST(!workload.Get(producerFlag));
    BOOST_TEST_REQUIRE(world.GetSpecObj<noFlag>(unusedFlag));

    const MapPoint disconnectedFlag = world.MakeMapPoint(Position(hqFlag.x - 5, hqFlag.y - 5));
    BuildingFactory::CreateBuilding(world, BuildingType::Metalworks,
                                    world.GetNeighbour(disconnectedFlag, Direction::NorthWest), curPlayer,
                                    Nation::Romans);
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 3u);

    std::vector<std::unique_ptr<Ware>> orderedBoards;
    while(consumer->CalcDistributionPoints(GoodType::Boards) > 0)
        orderedBoards.push_back(std::make_unique<Ware>(GoodType::Boards, consumer, queries.GetStorehouses().front()));
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 3u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::East)).value() == 1u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::SouthWest)).value() == 0u);
    for(const auto& ware : orderedBoards)
    {
        consumer->WareLost(*ware);
        world.GetPlayer(curPlayer).RemoveWare(*ware);
    }

    producer->SetProductionEnabled(false);
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 2u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::East)).value() == 2u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::SouthWest)).value() == 0u);
}

BOOST_FIXTURE_TEST_CASE(RoadWorkload_IncludesConstructionSiteMaterials, BiggerWorldWithGCExecution)
{
    SetAllOwned(world);
    const MapPoint hqFlag = world.GetNeighbour(hqPos, Direction::SouthEast);
    const std::vector<Direction> producerRoute(4, Direction::East);
    const MapPoint producerFlag = GetRouteEnd(world, hqFlag, producerRoute);
    const std::vector<Direction> siteRoute(4, Direction::East);
    const MapPoint siteFlag = GetRouteEnd(world, producerFlag, siteRoute);
    this->BuildRoad(hqFlag, false, producerRoute);
    this->BuildRoad(producerFlag, false, siteRoute);

    auto* producer = dynamic_cast<nobUsual*>(BuildingFactory::CreateBuilding(
      world, BuildingType::Sawmill, world.GetNeighbour(producerFlag, Direction::NorthWest), curPlayer,
      Nation::Romans));
    BOOST_TEST_REQUIRE(producer);
    const MapPoint sitePos = world.GetNeighbour(siteFlag, Direction::NorthWest);
    world.GetPlayer(curPlayer).GetFirstWH()->Clear();
    world.SetBuildingSite(BuildingType::Metalworks, sitePos, curPlayer);
    noBuildingSite* site = world.GetSpecObj<noBuildingSite>(sitePos);
    BOOST_TEST_REQUIRE(site);
    site->PlaningFinished();
    BOOST_TEST_REQUIRE(site->CalcDistributionPoints(GoodType::Boards) > 0u);

    AIQueryService queries(world, curPlayer);
    AIJH::AIRoadWorkload workload(queries, world);
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 0u);
    BOOST_TEST(workload.Get(world.GetNeighbour(producerFlag, Direction::East)).value() == 1u);
}

BOOST_FIXTURE_TEST_CASE(RoadWorkload_IncludesMilitaryCoinDemand, BiggerWorldWithGCExecution)
{
    SetAllOwned(world);
    const MapPoint hqFlag = world.GetNeighbour(hqPos, Direction::SouthEast);
    const std::vector<Direction> route(4, Direction::East);
    const MapPoint militaryFlag = GetRouteEnd(world, hqFlag, route);
    this->BuildRoad(hqFlag, false, route);

    const MapPoint militaryPos = world.GetNeighbour(militaryFlag, Direction::NorthWest);
    auto* military = dynamic_cast<nobMilitary*>(
      BuildingFactory::CreateBuilding(world, BuildingType::Barracks, militaryPos, curPlayer, Nation::Romans));
    BOOST_TEST_REQUIRE(military);
    auto& soldier = world.AddFigure(
      militaryPos, std::make_unique<nofPassiveSoldier>(militaryPos, curPlayer, military, military, 0));
    world.GetPlayer(curPlayer).IncreaseInventoryJob(soldier.GetJobType(), 1);
    soldier.WalkToGoal();
    BOOST_TEST_REQUIRE(military->CalcCoinsPoints() > 0);
    Inventory goods;
    goods.Add(GoodType::Coins);
    world.GetPlayer(curPlayer).GetFirstWH()->AddGoods(goods, true);

    AIQueryService queries(world, curPlayer);
    AIJH::AIRoadWorkload workload(queries, world);
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(hqFlag, Direction::East)).value() == 1u);
}

BOOST_FIXTURE_TEST_CASE(RoadWorkload_ExposesWaterwayTiles, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, false);
    world.BuildRoad(curPlayer, true, shortcut.source, shortcut.waterRoute);

    AIQueryService queries(world, curPlayer);
    AIJH::AIRoadWorkload workload(queries, world);
    workload.Refresh();
    BOOST_TEST(workload.Get(world.GetNeighbour(shortcut.source, Direction::East)).value() == 0u);
    BOOST_TEST(!workload.Get(shortcut.source));
    BOOST_TEST(!workload.Get(shortcut.target));
}

BOOST_FIXTURE_TEST_CASE(BuildAlternativeRoad_StorehousePolicyBuildsLongerValidRoad, WorldWithGCExecution<1>)
{
    const MapPoint sourceFlagPos = world.GetNeighbour(hqPos, Direction::SouthEast);
    this->BuildRoad(sourceFlagPos, false, std::vector<Direction>(4, Direction::East));
    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(sourceFlagPos);
    BOOST_TEST_REQUIRE(sourceFlag);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    std::vector<Direction> route;
    BOOST_TEST(ai.GetConstruction().BuildAlternativeRoad(sourceFlag, route, AIJH::AlternativeRoadPolicy::BuildFirstValid));
    BOOST_TEST(!route.empty());
    BOOST_TEST(ai.FetchGameCommands().size() == 1u);
}

BOOST_FIXTURE_TEST_CASE(BuildAlternativeRoadBypassingSegment_BuildsShortcutAroundHotSegment, BiggerWorldWithGCExecution)
{
    ScopedRoadRouteBQPenalty disableRoadBQPenalty(0.0);
    SetAllOwned(world);

    const MapPoint sourceFlagPos = world.GetNeighbour(hqPos, Direction::SouthEast);
    const std::vector<Direction> hotRoute{Direction::SouthEast, Direction::East, Direction::NorthEast,
                                          Direction::SouthEast, Direction::East, Direction::NorthEast,
                                          Direction::SouthEast, Direction::East, Direction::NorthEast};
    const MapPoint targetFlagPos = GetRouteEnd(world, sourceFlagPos, hotRoute);
    BOOST_TEST_REQUIRE(world.CalcDistance(sourceFlagPos, targetFlagPos) < hotRoute.size());

    this->BuildRoad(sourceFlagPos, false, hotRoute);
    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(sourceFlagPos);
    const noFlag* targetFlag = world.GetSpecObj<noFlag>(targetFlagPos);
    BOOST_TEST_REQUIRE(sourceFlag);
    BOOST_TEST_REQUIRE(targetFlag);

    const RoadSegment* hotRoad = sourceFlag->GetRoute(hotRoute.front());
    BOOST_TEST_REQUIRE(hotRoad);
    BOOST_TEST(&hotRoad->GetOtherFlag(*sourceFlag) == targetFlag);

    AIQueryService queries(world, curPlayer);
    unsigned oldLength = 0;
    std::vector<const RoadSegment*> traversedSegments;
    BOOST_TEST_REQUIRE(queries.FindPathOnRoads(*sourceFlag, *targetFlag, &oldLength, &traversedSegments));
    BOOST_TEST(oldLength == hotRoute.size());
    BOOST_TEST(helpers::contains(traversedSegments, hotRoad));
    std::vector<Direction> freeRoute;
    unsigned freeLength = 0;
    BOOST_TEST_REQUIRE(queries.FindFreePathForNewRoad(sourceFlagPos, targetFlagPos, &freeRoute, &freeLength));
    BOOST_TEST(freeLength < oldLength);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    const AIJH::RoadWorkloadSegment hotSegment{sourceFlagPos, targetFlagPos, 41u,
                                               static_cast<unsigned>(hotRoute.size()), false};
    std::vector<Direction> route;
    BOOST_TEST_REQUIRE(ai.GetConstruction().BuildAlternativeRoadBypassingSegment(hotSegment, route));
    BOOST_TEST(route.size() < hotRoute.size());
    BOOST_TEST(route.front() != hotRoute.front());

    auto commands = ai.FetchGameCommands();
    BOOST_TEST_REQUIRE(commands.size() == 1u);
    commands.front()->Execute(world, curPlayer);
    BOOST_TEST_REQUIRE(sourceFlag->GetRoute(route.front()));
    BOOST_TEST(sourceFlag->GetRoute(route.front()) != hotRoad);
}

BOOST_FIXTURE_TEST_CASE(BuildAlternativeWaterRoad_BuildsBeneficialWareShortcut, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, true);
    const noFlag* sourceFlag = world.GetSpecObj<noFlag>(shortcut.source);
    BOOST_TEST_REQUIRE(sourceFlag);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    std::vector<Direction> route;
    BOOST_TEST_REQUIRE(ai.GetConstruction().BuildAlternativeWaterRoad(sourceFlag, route));
    BOOST_TEST(route == shortcut.waterRoute, boost::test_tools::per_element());

    auto commands = ai.FetchGameCommands();
    BOOST_TEST_REQUIRE(commands.size() == 1u);
    commands.front()->Execute(world, curPlayer);
    BOOST_TEST_REQUIRE(sourceFlag->GetRoute(Direction::East));
    BOOST_TEST(static_cast<int>(sourceFlag->GetRoute(Direction::East)->GetRoadType())
               == static_cast<int>(RoadType::Water));
}

BOOST_FIXTURE_TEST_CASE(BuildAlternativeWaterRoad_RequiresAvailableBoat, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, true);
    auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq);
    hq->Clear();
    Inventory goods;
    goods.Add(Job::Helper);
    hq->AddGoods(goods, true);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    std::vector<Direction> route;
    BOOST_TEST(!ai.GetConstruction().BuildAlternativeWaterRoad(world.GetSpecObj<noFlag>(shortcut.source), route));
    BOOST_TEST(ai.FetchGameCommands().empty());
}

BOOST_FIXTURE_TEST_CASE(Waterways_DoNotProvideLandConnectivityOrInteriorFlagsAndSurviveCleanup, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, false);
    world.BuildRoad(curPlayer, true, shortcut.source, shortcut.waterRoute);
    const noFlag* targetFlag = world.GetSpecObj<noFlag>(shortcut.target);
    BOOST_TEST_REQUIRE(targetFlag);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    BOOST_TEST(!ai.GetConstruction().IsConnectedToRoadSystem(targetFlag));

    AIJH::AIRoadController roads(ai);
    roads.HandleRoadConstructionComplete(shortcut.target, Direction::West);
    BOOST_TEST(ai.FetchGameCommands().empty());
    BOOST_TEST(!roads.RemoveUnusedRoad(*targetFlag, boost::none));
    BOOST_TEST(ai.FetchGameCommands().empty());
    BOOST_TEST_REQUIRE(targetFlag->GetRoute(Direction::West));
    BOOST_TEST(static_cast<int>(targetFlag->GetRoute(Direction::West)->GetRoadType())
               == static_cast<int>(RoadType::Water));
}

BOOST_FIXTURE_TEST_CASE(BoatReserve_SwitchesAndEnablesShipyardForExistingWaterway, WaterwayWorldWithGCExecution)
{
    const WaterwayShortcut shortcut = CreateWaterwayShortcut(world, curPlayer, false);
    auto* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos);
    BOOST_TEST_REQUIRE(hq);
    hq->Clear();
    Inventory goods;
    goods.Add(Job::Helper);
    hq->AddGoods(goods, true);
    world.BuildRoad(curPlayer, true, shortcut.source, shortcut.waterRoute);

    MapPoint shipyardPos = MapPoint::Invalid();
    for(const MapPoint pt : world.GetPointsInRadius(hqPos, 6))
    {
        if(world.GetBQ(pt, curPlayer) == BuildingQuality::Castle)
        {
            shipyardPos = pt;
            break;
        }
    }
    BOOST_TEST_REQUIRE(shipyardPos.isValid());
    auto* shipyard = dynamic_cast<nobShipYard*>(
      BuildingFactory::CreateBuilding(world, BuildingType::Shipyard, shipyardPos, curPlayer, Nation::Romans));
    BOOST_TEST_REQUIRE(shipyard);
    shipyard->SetMode(nobShipYard::Mode::Ships);
    shipyard->SetProductionEnabled(false);

    AIJH::AIPlayerJH ai(curPlayer, world, AI::Level::Hard);
    for(unsigned gf = 0; gf < 9; ++gf)
        ai.RunGF(gf, false);
    ai.RunGF(50, false);
    for(auto& command : ai.FetchGameCommands())
        command->Execute(world, curPlayer);

    BOOST_TEST(static_cast<int>(shipyard->GetMode()) == static_cast<int>(nobShipYard::Mode::Boats));
    BOOST_TEST(!shipyard->IsProductionDisabled());
}

BOOST_FIXTURE_TEST_CASE(BuildWoodIndustry, WorldWithGCExecution<1>)
{
    // Place a few trees
    for(const MapPoint& pt : world.GetPointsInRadius(hqPos + MapPoint(4, 0), 2))
    {
        if(!world.GetNode(pt).obj)
            world.SetNO(pt, new noTree(pt, 0, 3));
    }
    world.InitAfterLoad();

    const GamePlayer& player = world.GetPlayer(curPlayer);
    auto ai = AIFactory::Create(AI::Info(AI::Type::Default, AI::Level::Hard), curPlayer, world);
    // Build a woodcutter, sawmill and forester at some point
    for(unsigned gf = 0; gf < 2000;)
    {
        std::vector<gc::GameCommandPtr> aiGcs = ai->FetchGameCommands();
        for(unsigned i = 0; i < 5; i++, gf++)
        {
            em.ExecuteNextGF();
            ai->RunGF(em.GetCurrentGF(), i == 0);
        }
        for(gc::GameCommandPtr& gc : aiGcs)
        {
            gc->Execute(world, curPlayer);
        }
        if(playerHasBld(player, BuildingType::Sawmill) && playerHasBld(player, BuildingType::Woodcutter)
           && playerHasBld(player, BuildingType::Forester))
            break;
    }
    BOOST_TEST(playerHasBld(player, BuildingType::Sawmill));
    BOOST_TEST(playerHasBld(player, BuildingType::Woodcutter));
    BOOST_TEST(playerHasBld(player, BuildingType::Forester));
}

namespace {
void forceExpansion(const GamePlayer& player, GameWorld& world)
{
    // No space for saw mill due to altitude diff of 3 in range 2 -> Huts only
    for(unsigned y = 0; y < world.GetHeight(); y += 2)
    {
        for(unsigned x = 0; x < world.GetWidth(); x += 2)
            world.ChangeAltitude(MapPoint(x, y), 13);
    }
    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        BOOST_TEST_REQUIRE(world.GetBQ(pt, player.GetPlayerId()) <= BuildingQuality::Hut);
    }
}

void runUntilMilitaryBuildingSiteFound(TestEventManager& em, unsigned curPlayer, GameWorld& world,
                                       const std::list<noBuildingSite*>& bldSites)
{
    auto ai = AIFactory::Create(AI::Info(AI::Type::Default, AI::Level::Hard), curPlayer, world);
    for(unsigned gf = 0; gf < 2000;)
    {
        std::vector<gc::GameCommandPtr> aiGcs = ai->FetchGameCommands();
        for(unsigned i = 0; i < 5; i++, gf++)
        {
            em.ExecuteNextGF();
            ai->RunGF(em.GetCurrentGF(), i == 0);
        }
        for(gc::GameCommandPtr& gc : aiGcs)
        {
            gc->Execute(world, curPlayer);
        }
        if(containsBldType(bldSites, BuildingType::Barracks) || containsBldType(bldSites, BuildingType::Guardhouse))
            break;
    }
}
} // namespace

BOOST_FIXTURE_TEST_CASE(ExpandWhenNoSpace, BiggerWorldWithGCExecution)
{
    const auto& player = world.GetPlayer(curPlayer);
    const auto& bldSites = player.GetBuildingRegister().GetBuildingSites();

    forceExpansion(player, world);
    runUntilMilitaryBuildingSiteFound(em, curPlayer, world, bldSites);

    BOOST_TEST_REQUIRE(
      (containsBldType(bldSites, BuildingType::Barracks) || containsBldType(bldSites, BuildingType::Guardhouse)));
}

BOOST_FIXTURE_TEST_CASE(DoNotBuildMilitaryBuildingsWithinComputerBarrier, BiggerWorldWithGCExecution)
{
    const auto& player = world.GetPlayer(curPlayer);
    const auto& bldSites = player.GetBuildingRegister().GetBuildingSites();

    const auto& barrierPt = player.GetHQPos();
    constexpr auto radius = HQ_RADIUS;

    world.SetComputerBarrier(barrierPt, radius);
    forceExpansion(player, world);
    runUntilMilitaryBuildingSiteFound(em, curPlayer, world, bldSites);

    BOOST_TEST_REQUIRE(
      !(containsBldType(bldSites, BuildingType::Barracks) || containsBldType(bldSites, BuildingType::Guardhouse)));
}

BOOST_FIXTURE_TEST_CASE(DoBuildMilitaryBuildingsOutsideComputerBarrier, BiggerWorldWithGCExecution)
{
    const auto& player = world.GetPlayer(curPlayer);
    const auto& bldSites = player.GetBuildingRegister().GetBuildingSites();

    auto barrierPt = player.GetHQPos();
    // move barrier 2 tiles west of HQ, now military buildings should be buildable to the east
    barrierPt.x -= 2;
    constexpr auto radius = HQ_RADIUS;

    world.SetComputerBarrier(barrierPt, radius);
    forceExpansion(player, world);
    runUntilMilitaryBuildingSiteFound(em, curPlayer, world, bldSites);

    BOOST_TEST_REQUIRE(
      (containsBldType(bldSites, BuildingType::Barracks) || containsBldType(bldSites, BuildingType::Guardhouse)));

    for(noBuildingSite* bldSite : bldSites)
        BOOST_TEST_REQUIRE(!world.CheckPointsInRadius(
          barrierPt, radius,
          [bldSite](const MapPoint& pt, unsigned) {
              const auto type = bldSite->GetBuildingType();
              return (type == BuildingType::Barracks || type == BuildingType::Guardhouse) && pt == bldSite->GetPos();
          },
          true));
}

BOOST_AUTO_TEST_SUITE_END()
