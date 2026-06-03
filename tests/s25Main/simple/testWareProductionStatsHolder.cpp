// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareProductionStatsHolder.h"

#include <boost/test/unit_test.hpp>

namespace {

struct WareProductionStatsHolderFixture
{
    WareProductionStatsHolderFixture() { WareProductionStatsHolder::Reset(); }
    ~WareProductionStatsHolderFixture() { WareProductionStatsHolder::Reset(); }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(WareProductionStatsHolderTests, WareProductionStatsHolderFixture)

BOOST_AUTO_TEST_CASE(ReportsAreVisibleOnlyAfterWindowCompletes)
{
    WareProductionStatsHolder::ReportProduced(0, 0, GoodType::Boards);
    WareProductionStatsHolder::ReportProduced(999, 0, GoodType::Boards, 2);
    WareProductionStatsHolder::ReportProduced(1000, 0, GoodType::Boards, 4);

    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 999).produced[GoodType::Boards] == 0u);
    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 1000).produced[GoodType::Boards] == 3u);
    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 1999).produced[GoodType::Boards] == 3u);
    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 2000).produced[GoodType::Boards] == 4u);
}

BOOST_AUTO_TEST_CASE(ConsumptionIsTrackedByGoodAndConsumerBuilding)
{
    WareProductionStatsHolder::ReportConsumed(100, 1, GoodType::Wood, BuildingType::Sawmill, 3);
    WareProductionStatsHolder::ReportConsumed(200, 1, GoodType::Wood, BuildingType::Charburner, 2);
    WareProductionStatsHolder::ReportConsumed(300, 1, GoodType::Grain, BuildingType::Charburner);

    const WareProductionWindowStats& stats = WareProductionStatsHolder::GetPreviousWindowStats(1, 1234);
    BOOST_TEST(stats.consumed[GoodType::Wood] == 5u);
    BOOST_TEST(stats.consumed[GoodType::Grain] == 1u);
    BOOST_TEST(stats.consumedByConsumer[WareProductionStatsHolder::ToConsumer(BuildingType::Sawmill)][GoodType::Wood]
               == 3u);
    BOOST_TEST(stats.consumedByConsumer[WareProductionStatsHolder::ToConsumer(BuildingType::Charburner)][GoodType::Wood]
               == 2u);
    BOOST_TEST(
      stats.consumedByConsumer[WareProductionStatsHolder::ToConsumer(BuildingType::Charburner)][GoodType::Grain] == 1u);
}

BOOST_AUTO_TEST_CASE(ConstructionConsumptionUsesDedicatedConsumer)
{
    WareProductionStatsHolder::ReportConstructionConsumed(100, 1, GoodType::Boards, 3);
    WareProductionStatsHolder::ReportConstructionConsumed(200, 1, GoodType::Stones, 2);

    const WareProductionWindowStats& stats = WareProductionStatsHolder::GetPreviousWindowStats(1, 1234);
    BOOST_TEST(stats.consumed[GoodType::Boards] == 3u);
    BOOST_TEST(stats.consumed[GoodType::Stones] == 2u);
    BOOST_TEST(stats.consumedByConsumer[WareConsumptionConsumer::Construction][GoodType::Boards] == 3u);
    BOOST_TEST(stats.consumedByConsumer[WareConsumptionConsumer::Construction][GoodType::Stones] == 2u);
    BOOST_TEST(stats.consumedByConsumer[WareProductionStatsHolder::ToConsumer(BuildingType::Sawmill)][GoodType::Boards]
               == 0u);
}

BOOST_AUTO_TEST_CASE(SkippedWindowsReturnEmptyStats)
{
    WareProductionStatsHolder::ReportProduced(0, 0, GoodType::Boards, 7);

    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 3000).produced[GoodType::Boards] == 0u);

    WareProductionStatsHolder::ReportProduced(2500, 0, GoodType::Boards, 4);

    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 3000).produced[GoodType::Boards] == 4u);
}

BOOST_AUTO_TEST_CASE(SeparatePlayersDoNotShareStats)
{
    WareProductionStatsHolder::ReportProduced(0, 0, GoodType::Fish, 2);
    WareProductionStatsHolder::ReportProduced(0, 1, GoodType::Fish, 5);

    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 1000).produced[GoodType::Fish] == 2u);
    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(1, 1000).produced[GoodType::Fish] == 5u);
}

BOOST_AUTO_TEST_CASE(IgnoresInvalidInputsAndNormalizesShields)
{
    WareProductionStatsHolder::ReportProduced(0, 0, GoodType::Nothing);
    WareProductionStatsHolder::ReportProduced(0, 255, GoodType::Boards);
    WareProductionStatsHolder::ReportConsumed(0, 0, GoodType::ShieldVikings, BuildingType::Headquarters, 2);

    const WareProductionWindowStats& stats = WareProductionStatsHolder::GetPreviousWindowStats(0, 1000);
    BOOST_TEST(stats.produced[GoodType::Boards] == 0u);
    BOOST_TEST(stats.consumed[GoodType::ShieldRomans] == 2u);
    BOOST_TEST(
      stats.consumedByConsumer[WareProductionStatsHolder::ToConsumer(BuildingType::Headquarters)][GoodType::ShieldRomans]
      == 2u);
}

BOOST_AUTO_TEST_CASE(ResetClearsStats)
{
    WareProductionStatsHolder::ReportProduced(0, 0, GoodType::Boards, 3);
    WareProductionStatsHolder::Reset();

    BOOST_TEST(WareProductionStatsHolder::GetPreviousWindowStats(0, 1000).produced[GoodType::Boards] == 0u);
}

BOOST_AUTO_TEST_SUITE_END()
