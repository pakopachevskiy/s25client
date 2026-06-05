// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ConstructionSupplementTracker.h"
#include "WareProductionStatsHolder.h"
#include "gameData/BuildingConsts.h"
#include "gameTypes/Inventory.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(ConstructionSupplementTrackerTests)

BOOST_AUTO_TEST_CASE(CreationAddsFullBuildingCosts)
{
    ConstructionSupplementTracker tracker;
    tracker.OnConstructionSiteCreated(BuildingType::Farm);

    BOOST_TEST(tracker.GetBoardsRequired() == BUILDING_COSTS[BuildingType::Farm].boards);
    BOOST_TEST(tracker.GetStonesRequired() == BUILDING_COSTS[BuildingType::Farm].stones);
}

BOOST_AUTO_TEST_CASE(DeliveriesDecreaseMatchingCountersOnly)
{
    ConstructionSupplementTracker tracker;
    tracker.OnConstructionSiteCreated(BuildingType::Farm);

    tracker.OnWareDelivered(GoodType::Boards);
    tracker.OnWareDelivered(GoodType::Stones, 2);
    tracker.OnWareDelivered(GoodType::Wood);

    BOOST_TEST(tracker.GetBoardsRequired() == BUILDING_COSTS[BuildingType::Farm].boards - 1u);
    BOOST_TEST(tracker.GetStonesRequired() == BUILDING_COSTS[BuildingType::Farm].stones - 2u);
}

BOOST_AUTO_TEST_CASE(DestroyingPartiallySuppliedSiteRemovesOnlyLackedMaterials)
{
    ConstructionSupplementTracker tracker;
    tracker.OnConstructionSiteCreated(BuildingType::Farm);
    tracker.OnWareDelivered(GoodType::Boards);
    tracker.OnWareDelivered(GoodType::Stones, 2);

    tracker.OnConstructionSiteDestroyed(BuildingType::Farm, 1, 2);

    BOOST_TEST(tracker.GetBoardsRequired() == 0u);
    BOOST_TEST(tracker.GetStonesRequired() == 0u);
}

BOOST_AUTO_TEST_CASE(CompletedSiteRemovalLeavesCountersAtZero)
{
    ConstructionSupplementTracker tracker;
    tracker.OnConstructionSiteCreated(BuildingType::Woodcutter);
    tracker.OnWareDelivered(GoodType::Boards, BUILDING_COSTS[BuildingType::Woodcutter].boards);

    tracker.OnConstructionSiteDestroyed(BuildingType::Woodcutter,
                                        BUILDING_COSTS[BuildingType::Woodcutter].boards,
                                        BUILDING_COSTS[BuildingType::Woodcutter].stones);

    BOOST_TEST(tracker.GetBoardsRequired() == 0u);
    BOOST_TEST(tracker.GetStonesRequired() == 0u);
}

BOOST_AUTO_TEST_CASE(CandidateShortageGateUsesStrictFiftyPercentProductionThreshold)
{
    ConstructionSupplementTracker tracker;
    tracker.OnConstructionSiteCreated(BuildingType::Woodcutter);

    Inventory inventory;
    inventory.clear();
    WareProductionWindowStats stats;

    stats.produced[GoodType::Boards] = 2;
    BOOST_TEST(tracker.WouldHaveMaterialShortage(BuildingType::Woodcutter, inventory, stats));

    stats.produced[GoodType::Boards] = 3;
    BOOST_TEST(!tracker.WouldHaveMaterialShortage(BuildingType::Woodcutter, inventory, stats));
}

BOOST_AUTO_TEST_CASE(SeparatePlayersDoNotShareTrackerState)
{
    ConstructionSupplementTrackerHolder::Reset();
    ConstructionSupplementTrackerHolder::ReportConstructionSiteCreated(0, BuildingType::Woodcutter);
    ConstructionSupplementTrackerHolder::ReportConstructionSiteCreated(1, BuildingType::Farm);

    BOOST_TEST(ConstructionSupplementTrackerHolder::GetConst(0).GetBoardsRequired()
               == BUILDING_COSTS[BuildingType::Woodcutter].boards);
    BOOST_TEST(ConstructionSupplementTrackerHolder::GetConst(1).GetBoardsRequired()
               == BUILDING_COSTS[BuildingType::Farm].boards);

    ConstructionSupplementTrackerHolder::Reset();
}

BOOST_AUTO_TEST_SUITE_END()
