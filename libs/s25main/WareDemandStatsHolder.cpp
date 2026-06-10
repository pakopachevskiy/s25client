// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareDemandStatsHolder.h"

#include "BuildingRegister.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "WareProductionStatsHolder.h"
#include "addons/const_addons.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobUsual.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/JobConsts.h"
#include "gameData/MaxPlayers.h"
#include "gameData/ShieldConsts.h"
#include "helpers/EnumRange.h"
#include "world/GameWorldBase.h"
#include <array>

namespace {

struct DemandSlot
{
    bool valid = false;
    const GameWorldBase* world = nullptr;
    unsigned windowIndex = 0;
    WareDemandSnapshot snapshot;
};

std::array<DemandSlot, MAX_PLAYERS> gDemandStats;
const WareDemandSnapshot kEmptySnapshot{};

bool IsSkippedRecurringBuilding(const BuildingType type)
{
    switch(type)
    {
        case BuildingType::Temple:
        case BuildingType::Catapult:
        case BuildingType::Brewery: return true;
        default: return false;
    }
}

void MarkCalculated(WareDemandSnapshot& snapshot, const GoodType good)
{
    const GoodType normalizedGood = ConvertShields(good);
    if(normalizedGood != GoodType::Nothing)
        snapshot.calculated[normalizedGood] = true;
}

void AddDemand(WareDemandSnapshot& snapshot, const GoodType good, const unsigned count)
{
    const GoodType normalizedGood = ConvertShields(good);
    if(normalizedGood == GoodType::Nothing)
        return;

    snapshot.calculated[normalizedGood] = true;
    snapshot.demand[normalizedGood] += count;
}

unsigned CeilDiv(const unsigned numerator, const unsigned denominator)
{
    return denominator == 0u ? 0u : (numerator + denominator - 1u) / denominator;
}

unsigned GetBuildingCycleGf(const BuildingType type)
{
    const auto job = BLD_WORK_DESC[type].job;
    if(!job)
        return 0;

    const JobConst& jobConst = JOB_CONSTS[*job];
    return jobConst.wait1_length + jobConst.work_length + jobConst.wait2_length + 40u;
}

unsigned GetDemandCyclesPerWindow(const GlobalGameSettings& ggs, const BuildingType type, const unsigned cycleGf)
{
    const unsigned cyclesPerWindow = CeilDiv(WareProductionStatsHolder::WINDOW_SIZE_GF, cycleGf);
    if(type == BuildingType::Armory && ggs.isEnabled(AddonId::HALF_COST_MIL_EQUIP))
        return CeilDiv(cyclesPerWindow, 2u);
    return cyclesPerWindow;
}

void MarkSupportedRecurringDemand(WareDemandSnapshot& snapshot)
{
    for(const BuildingType type : helpers::EnumRange<BuildingType>{})
    {
        if(!BuildingProperties::IsUsual(type) || IsSkippedRecurringBuilding(type))
            continue;

        const BldWorkDescription& desc = BLD_WORK_DESC[type];
        for(const GoodType good : desc.waresNeeded)
            MarkCalculated(snapshot, good);
    }
}

void AddRecurringDemand(WareDemandSnapshot& snapshot, const GameWorldBase& world, const GamePlayer& player)
{
    const BuildingRegister& buildingRegister = player.GetBuildingRegister();
    for(const BuildingType type : helpers::EnumRange<BuildingType>{})
    {
        if(!BuildingProperties::IsUsual(type) || IsSkippedRecurringBuilding(type))
            continue;

        const BldWorkDescription& desc = BLD_WORK_DESC[type];
        if(desc.waresNeeded.empty())
            continue;

        const unsigned cycleGf = GetBuildingCycleGf(type);
        if(cycleGf == 0u)
            continue;

        const unsigned cyclesPerWindow = GetDemandCyclesPerWindow(world.GetGGS(), type, cycleGf);
        for(const nobUsual* building : buildingRegister.GetBuildings(type))
        {
            if(!building->HasWorker() || building->IsProductionDisabled())
                continue;

            if(desc.useOneWareEach)
            {
                for(const GoodType good : desc.waresNeeded)
                    AddDemand(snapshot, good, cyclesPerWindow);
            } else
            {
                const unsigned numWareTypes = desc.waresNeeded.size();
                const unsigned baseDemand = cyclesPerWindow / numWareTypes;
                unsigned remainder = cyclesPerWindow % numWareTypes;
                for(const GoodType good : desc.waresNeeded)
                {
                    AddDemand(snapshot, good, baseDemand + (remainder > 0u ? 1u : 0u));
                    if(remainder > 0u)
                        --remainder;
                }
            }
        }
    }
}

void AddConstructionDemand(WareDemandSnapshot& snapshot, const GamePlayer& player)
{
    MarkCalculated(snapshot, GoodType::Boards);
    MarkCalculated(snapshot, GoodType::Stones);

    for(const noBuildingSite* buildingSite : player.GetBuildingRegister().GetBuildingSites())
    {
        const BuildingCost& costs = BUILDING_COSTS[buildingSite->GetBuildingType()];
        snapshot.demand[GoodType::Boards] += costs.boards;
        snapshot.demand[GoodType::Stones] += costs.stones;
    }
}

WareDemandSnapshot CalculateDemand(const GameWorldBase& world, const unsigned char playerId)
{
    WareDemandSnapshot snapshot;
    if(playerId >= world.GetNumPlayers() || !world.GetPlayer(playerId).isUsed())
        return snapshot;

    const GamePlayer& player = world.GetPlayer(playerId);
    MarkSupportedRecurringDemand(snapshot);
    AddRecurringDemand(snapshot, world, player);
    AddConstructionDemand(snapshot, player);
    return snapshot;
}

} // namespace

namespace WareDemandStatsHolder {

const WareDemandSnapshot& GetCurrentDemand(const GameWorldBase& world, const unsigned char playerId,
                                           const unsigned currentGf, const AIPlayer*)
{
    if(playerId >= MAX_PLAYERS)
        return kEmptySnapshot;

    const unsigned windowIndex = currentGf / WareProductionStatsHolder::WINDOW_SIZE_GF;
    DemandSlot& slot = gDemandStats[playerId];
    if(!slot.valid || slot.windowIndex != windowIndex || slot.world != &world)
    {
        slot.valid = true;
        slot.world = &world;
        slot.windowIndex = windowIndex;
        slot.snapshot = CalculateDemand(world, playerId);
    }
    return slot.snapshot;
}

void Reset()
{
    gDemandStats = {};
}

} // namespace WareDemandStatsHolder
