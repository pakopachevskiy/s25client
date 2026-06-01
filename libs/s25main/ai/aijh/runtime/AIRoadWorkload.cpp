// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AIRoadWorkload.h"

#include "RoadSegment.h"
#include "ai/AIQueryService.h"
#include "ai/aijh/debug/AIRuntimeProfiler.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "helpers/EnumRange.h"
#include "world/GameWorldBase.h"

#include <cstdint>
#include <unordered_map>
#include <utility>

namespace AIJH {

namespace {

struct WareEdge
{
    const noRoadNode* node;
    GoodType good;
};

} // namespace

AIRoadWorkload::AIRoadWorkload(const AIQueryService& queries, const GameWorldBase& world)
    : queries_(queries), world_(world), nodeValues_(world.GetSize().x * world.GetSize().y)
{
}

void AIRoadWorkload::Refresh()
{
    ScopedAIRuntimeProfile profile(AIRuntimeProfileSection::CalculateRoadWorkload);

    std::unordered_map<const RoadSegment*, unsigned> segmentScores;
    for(const RoadSegment* road : queries_.GetRoads())
        segmentScores.emplace(road, 0);

    std::vector<WareEdge> producers;
    std::vector<WareEdge> consumers;
    for(const BuildingType type : helpers::enumRange<BuildingType>())
    {
        if(!BuildingProperties::IsUsual(type))
            continue;

        const BldWorkDescription& workDesc = BLD_WORK_DESC[type];
        for(nobUsual* building : queries_.GetBuildings(type))
        {
            if(workDesc.producedWare && workDesc.producedWare != GoodType::Nothing
               && !building->IsProductionDisabled())
                producers.push_back({building, *workDesc.producedWare});

            for(const GoodType good : workDesc.waresNeeded)
            {
                if(building->CalcDistributionPoints(good) > 0)
                    consumers.push_back({building, good});
            }
        }
    }

    for(noBuildingSite* site : queries_.GetBuildingSites())
    {
        for(const GoodType good : {GoodType::Boards, GoodType::Stones})
        {
            if(site->CalcDistributionPoints(good) > 0)
                consumers.push_back({site, good});
        }
    }

    for(const nobHarborBuilding* harbor : queries_.GetHarbors())
    {
        for(const GoodType good : {GoodType::Boards, GoodType::Stones})
        {
            if(harbor->CalcDistributionPoints(good) > 0)
                consumers.push_back({harbor, good});
        }
    }

    for(const nobMilitary* military : queries_.GetMilitaryBuildings())
    {
        if(military->CalcCoinsPoints() > 0)
            consumers.push_back({military, GoodType::Coins});
    }

    std::uint64_t attemptedRoutes = 0;
    std::vector<const RoadSegment*> route;
    const auto addRoute = [&](const noRoadNode& start, const noRoadNode& goal) {
        if(start.GetPos() == goal.GetPos())
            return;

        ++attemptedRoutes;
        if(!queries_.FindPathForWareOnRoads(start, goal, nullptr, &route))
            return;

        for(const RoadSegment* segment : route)
        {
            const auto it = segmentScores.find(segment);
            if(it != segmentScores.end())
                ++it->second;
        }
    };

    for(const WareEdge& producer : producers)
    {
        for(const WareEdge& consumer : consumers)
        {
            if(producer.good == consumer.good)
                addRoute(*producer.node, *consumer.node);
        }
        for(const nobBaseWarehouse* warehouse : queries_.GetStorehouses())
            addRoute(*producer.node, *warehouse);
    }

    for(const nobBaseWarehouse* warehouse : queries_.GetStorehouses())
    {
        for(const WareEdge& consumer : consumers)
            addRoute(*warehouse, *consumer.node);
    }

    nodeValues_.assign(world_.GetSize().x * world_.GetSize().y, std::nullopt);
    for(const auto& segmentScore : segmentScores)
    {
        const RoadSegment& segment = *segmentScore.first;
        MapPoint pt = segment.GetF1()->GetPos();
        for(unsigned i = 0; i < segment.GetLength(); ++i)
        {
            pt = world_.GetNeighbour(pt, segment.GetRoute(i));
            if(i + 1 < segment.GetLength())
                nodeValues_[world_.GetIdx(pt)] = segmentScore.second;
        }
    }

    profile.SetWorkUnits(attemptedRoutes);
}

std::optional<unsigned> AIRoadWorkload::Get(const MapPoint pt) const
{
    if(!pt.isValid() || pt.x >= world_.GetSize().x || pt.y >= world_.GetSize().y)
        return std::nullopt;
    return nodeValues_[world_.GetIdx(pt)];
}

} // namespace AIJH
