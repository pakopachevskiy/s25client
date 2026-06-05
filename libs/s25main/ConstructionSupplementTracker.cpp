// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ConstructionSupplementTracker.h"

#include "WareProductionStatsHolder.h"
#include "buildings/noBuildingSite.h"
#include "gameData/BuildingConsts.h"
#include "gameData/MaxPlayers.h"
#include "gameTypes/Inventory.h"
#include <array>
#include <cstdint>

namespace {

std::array<ConstructionSupplementTracker, MAX_PLAYERS> gTrackers;
const ConstructionSupplementTracker kEmptyTracker;
ConstructionSupplementTracker gInvalidTracker;

bool HasShortage(const unsigned required, const unsigned stock, const uint32_t produced)
{
    if(stock >= required)
        return false;

    const unsigned shortage = required - stock;
    return static_cast<uint64_t>(produced) * 2u <= shortage;
}

} // namespace

void ConstructionSupplementTracker::OnConstructionSiteCreated(const BuildingType buildingType)
{
    boardsRequired += BUILDING_COSTS[buildingType].boards;
    stonesRequired += BUILDING_COSTS[buildingType].stones;
}

void ConstructionSupplementTracker::OnWareDelivered(const GoodType ware, const unsigned count)
{
    switch(ware)
    {
        case GoodType::Boards: DecreaseCounter(boardsRequired, count); break;
        case GoodType::Stones: DecreaseCounter(stonesRequired, count); break;
        default: break;
    }
}

void ConstructionSupplementTracker::OnConstructionSiteDestroyed(const BuildingType buildingType,
                                                                const unsigned deliveredBoards,
                                                                const unsigned deliveredStones)
{
    const auto& costs = BUILDING_COSTS[buildingType];
    DecreaseCounter(boardsRequired, costs.boards > deliveredBoards ? costs.boards - deliveredBoards : 0u);
    DecreaseCounter(stonesRequired, costs.stones > deliveredStones ? costs.stones - deliveredStones : 0u);
}

bool ConstructionSupplementTracker::WouldHaveMaterialShortage(
  const BuildingType candidate, const Inventory& inventory, const WareProductionWindowStats& productionStats) const
{
    const unsigned requiredBoards = boardsRequired + BUILDING_COSTS[candidate].boards;
    const unsigned requiredStones = stonesRequired + BUILDING_COSTS[candidate].stones;

    return HasShortage(requiredBoards, inventory.goods[GoodType::Boards], productionStats.produced[GoodType::Boards])
           || HasShortage(requiredStones, inventory.goods[GoodType::Stones],
                          productionStats.produced[GoodType::Stones]);
}

void ConstructionSupplementTracker::DecreaseCounter(unsigned& counter, const unsigned count)
{
    if(count >= counter)
        counter = 0;
    else
        counter -= count;
}

namespace ConstructionSupplementTrackerHolder {

ConstructionSupplementTracker& Get(const unsigned char playerId)
{
    if(playerId >= MAX_PLAYERS)
        return gInvalidTracker;
    return gTrackers[playerId];
}

const ConstructionSupplementTracker& GetConst(const unsigned char playerId)
{
    if(playerId >= MAX_PLAYERS)
        return kEmptyTracker;
    return gTrackers[playerId];
}

void ReportConstructionSiteCreated(const unsigned char playerId, const BuildingType buildingType)
{
    if(playerId >= MAX_PLAYERS)
        return;
    gTrackers[playerId].OnConstructionSiteCreated(buildingType);
}

void ReportWareDelivered(const unsigned char playerId, const GoodType ware, const unsigned count)
{
    if(playerId >= MAX_PLAYERS || count == 0)
        return;
    gTrackers[playerId].OnWareDelivered(ware, count);
}

void ReportConstructionSiteDestroyed(const unsigned char playerId, const BuildingType buildingType,
                                     const unsigned deliveredBoards, const unsigned deliveredStones)
{
    if(playerId >= MAX_PLAYERS)
        return;
    gTrackers[playerId].OnConstructionSiteDestroyed(buildingType, deliveredBoards, deliveredStones);
}

void Rebuild(const unsigned char playerId, const std::list<noBuildingSite*>& buildingSites)
{
    if(playerId >= MAX_PLAYERS)
        return;

    ConstructionSupplementTracker tracker;
    for(const noBuildingSite* buildingSite : buildingSites)
    {
        tracker.OnConstructionSiteCreated(buildingSite->GetBuildingType());
        tracker.OnWareDelivered(GoodType::Boards, buildingSite->getBoards() + buildingSite->getUsedBoards());
        tracker.OnWareDelivered(GoodType::Stones, buildingSite->getStones() + buildingSite->getUsedStones());
    }
    gTrackers[playerId] = tracker;
}

void Reset()
{
    gTrackers = {};
    gInvalidTracker = {};
}

} // namespace ConstructionSupplementTrackerHolder
