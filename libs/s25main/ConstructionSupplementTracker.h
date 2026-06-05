// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/BuildingType.h"
#include "gameTypes/GoodTypes.h"
#include <list>

class noBuildingSite;
struct Inventory;
struct WareProductionWindowStats;

class ConstructionSupplementTracker
{
public:
    void OnConstructionSiteCreated(BuildingType buildingType);
    void OnWareDelivered(GoodType ware, unsigned count = 1);
    void OnConstructionSiteDestroyed(BuildingType buildingType, unsigned deliveredBoards, unsigned deliveredStones);

    unsigned GetBoardsRequired() const { return boardsRequired; }
    unsigned GetStonesRequired() const { return stonesRequired; }
    bool WouldHaveMaterialShortage(BuildingType candidate, const Inventory& inventory,
                                   const WareProductionWindowStats& productionStats) const;

private:
    unsigned boardsRequired = 0;
    unsigned stonesRequired = 0;

    static void DecreaseCounter(unsigned& counter, unsigned count);
};

namespace ConstructionSupplementTrackerHolder {

ConstructionSupplementTracker& Get(unsigned char playerId);
const ConstructionSupplementTracker& GetConst(unsigned char playerId);
void ReportConstructionSiteCreated(unsigned char playerId, BuildingType buildingType);
void ReportWareDelivered(unsigned char playerId, GoodType ware, unsigned count = 1);
void ReportConstructionSiteDestroyed(unsigned char playerId, BuildingType buildingType, unsigned deliveredBoards,
                                     unsigned deliveredStones);
void Rebuild(unsigned char playerId, const std::list<noBuildingSite*>& buildingSites);
void Reset();

} // namespace ConstructionSupplementTrackerHolder
