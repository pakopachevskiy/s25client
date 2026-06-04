// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareDemandStatsHolder.h"

#include "BuildingRegister.h"
#include "GamePlayer.h"
#include "WareProductionStatsHolder.h"
#include "ai/AIPlayer.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobUsual.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/JobConsts.h"
#include "gameData/MaxPlayers.h"
#include "gameData/ShieldConsts.h"
#include "gameTypes/BuildingCount.h"
#include "gameTypes/Inventory.h"
#include "helpers/EnumRange.h"
#include "world/GameWorldBase.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

struct DemandSlot
{
    bool valid = false;
    const GameWorldBase* world = nullptr;
    unsigned windowIndex = 0;
    WareDemandSnapshot snapshot;
};

struct BuildingDemandRemainder
{
    BuildingType type;
    unsigned remainder;
};

std::array<DemandSlot, MAX_PLAYERS> gDemandStats;
const WareDemandSnapshot kEmptySnapshot{};

bool IsSkippedRecurringBuilding(const BuildingType type)
{
    switch(type)
    {
        case BuildingType::Temple:
        case BuildingType::Catapult:
        case BuildingType::Brewery:
        case BuildingType::Armory:
        case BuildingType::Mint: return true;
        default: return false;
    }
}

bool IsSkippedConstructionBuilding(const BuildingType type)
{
    return type == BuildingType::Temple || type == BuildingType::Catapult;
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

void AddRecurringDemand(WareDemandSnapshot& snapshot, const GamePlayer& player)
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

        const unsigned cyclesPerWindow = CeilDiv(WareProductionStatsHolder::WINDOW_SIZE_GF, cycleGf);
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

unsigned CountAvailableBuilders(const GamePlayer& player)
{
    const Inventory& inventory = player.GetInventory();
    return inventory[Job::Builder] + std::min(inventory[Job::Helper], inventory[GoodType::Hammer]);
}

void AddConstructionDemand(WareDemandSnapshot& snapshot, const GamePlayer& player, const AIPlayer* aiPlayer)
{
    if(!aiPlayer)
        return;

    const unsigned availableBuilders = CountAvailableBuilders(player);
    if(availableBuilders == 0u)
        return;

    const BuildingCount buildingCount = player.GetBuildingRegister().GetBuildingNums();
    std::vector<std::pair<BuildingType, unsigned>> deficits;
    unsigned totalDeficit = 0;

    for(const BuildingType type : helpers::EnumRange<BuildingType>{})
    {
        if(!BuildingProperties::IsValid(type) || IsSkippedConstructionBuilding(type))
            continue;

        const boost::optional<unsigned> wanted = aiPlayer->GetNumBuildingsWanted(type);
        if(!wanted)
            return;

        const unsigned existing = buildingCount.buildings[type] + buildingCount.buildingSites[type];
        if(*wanted <= existing)
            continue;

        const unsigned deficit = *wanted - existing;
        deficits.emplace_back(type, deficit);
        totalDeficit += deficit;
    }

    MarkCalculated(snapshot, GoodType::Boards);
    MarkCalculated(snapshot, GoodType::Stones);

    if(totalDeficit == 0u)
        return;

    const unsigned numPlannedBuildings = availableBuilders;
    std::vector<BuildingDemandRemainder> remainders;
    unsigned allocatedBuildings = 0;

    for(const auto& deficit : deficits)
    {
        const uint64_t scaled = static_cast<uint64_t>(numPlannedBuildings) * deficit.second;
        const unsigned numBuildings = static_cast<unsigned>(scaled / totalDeficit);
        allocatedBuildings += numBuildings;
        remainders.push_back({deficit.first, static_cast<unsigned>(scaled % totalDeficit)});

        snapshot.demand[GoodType::Boards] += numBuildings * BUILDING_COSTS[deficit.first].boards;
        snapshot.demand[GoodType::Stones] += numBuildings * BUILDING_COSTS[deficit.first].stones;
    }

    std::sort(remainders.begin(), remainders.end(), [](const BuildingDemandRemainder& lhs,
                                                       const BuildingDemandRemainder& rhs) {
        if(lhs.remainder != rhs.remainder)
            return lhs.remainder > rhs.remainder;
        return static_cast<uint8_t>(lhs.type) < static_cast<uint8_t>(rhs.type);
    });

    for(unsigned i = 0; allocatedBuildings < numPlannedBuildings && i < remainders.size(); ++i, ++allocatedBuildings)
    {
        if(remainders[i].remainder == 0u)
            break;

        snapshot.demand[GoodType::Boards] += BUILDING_COSTS[remainders[i].type].boards;
        snapshot.demand[GoodType::Stones] += BUILDING_COSTS[remainders[i].type].stones;
    }
}

WareDemandSnapshot CalculateDemand(const GameWorldBase& world, const unsigned char playerId, const AIPlayer* aiPlayer)
{
    WareDemandSnapshot snapshot;
    if(playerId >= world.GetNumPlayers() || !world.GetPlayer(playerId).isUsed())
        return snapshot;

    const GamePlayer& player = world.GetPlayer(playerId);
    MarkSupportedRecurringDemand(snapshot);
    AddRecurringDemand(snapshot, player);
    AddConstructionDemand(snapshot, player, aiPlayer);
    return snapshot;
}

} // namespace

namespace WareDemandStatsHolder {

const WareDemandSnapshot& GetCurrentDemand(const GameWorldBase& world, const unsigned char playerId,
                                           const unsigned currentGf, const AIPlayer* aiPlayer)
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
        slot.snapshot = CalculateDemand(world, playerId, aiPlayer);
    }
    return slot.snapshot;
}

void Reset()
{
    gDemandStats = {};
}

} // namespace WareDemandStatsHolder
