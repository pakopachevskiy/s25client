//
// Created by Codex on 19.02.26.
//

#include "GlobalPositionFinder.h"

#include "ai/aijh/config/AIConfig.h"
#include "ai/aijh/runtime/AIPlanningContext.h"
#include "AIConstruction.h"
#include "BuildingPlanner.h"
#include "RttrForeachPt.h"
#include "ai/AIInterface.h"
#include "ai/AIQueryService.h"
#include "ai/AIResource.h"
#include "gameData/BuildingProperties.h"
#include "gameData/BuildingConsts.h"
#include "ai/aijh/config/WeightParams.h"
#include "GlobalGameSettings.h"
#include "addons/const_addons.h"
#include "nodeObjs/noBase.h"

namespace AIJH {
namespace {
struct SmartForestMetrics
{
    unsigned plantable = 0;
    unsigned lowValue = 0;
    unsigned highValue = 0;
    double score = 0.0;
};

bool IsBorderBlocked(const AIWorldView& aijh, const AIQueryService& queries, const BuildingType type,
                     const MapPoint& pt)
{
    const auto& locationParams = aijh.GetConfig().locationParams[type];
    if(locationParams.buildOnBorder)
        return false;
    return queries.CalcResourceValue(pt, AIResource::Borderland) > 1;
}

int ComputeRatingBonus(const AIWorldView& aijh, AIConstruction& construction, const BuildingType buildingType,
                       const MapPoint& candidate)
{
    int totalBonus = 0;
    const auto& locationParams = aijh.GetConfig().locationParams[buildingType];
    for(const auto targetType : helpers::enumRange<BuildingType>())
    {
        if(buildingType == BuildingType::Forester && targetType == BuildingType::Woodcutter)
            continue;

        const auto& ratingParams = locationParams.rating[targetType];
        if(!ratingParams.enabled)
            continue;

        const unsigned radius = ratingParams.radius > 0 ? ratingParams.radius : locationParams.resourceRating.defaultRadius;
        const int multiplier =
          ratingParams.multiplier != 0 ? ratingParams.multiplier : locationParams.resourceRating.defaultMultiplier;

        if(radius == 0 || multiplier == 0)
            continue;

        const int neighbors = construction.CountUsualBuildingInRadius(candidate, radius, targetType);
        totalBonus += neighbors * multiplier;
    }
    return totalBonus;
}

bool IsForesterPlantablePoint(const GameWorldBase& world, const MapPoint pt)
{
    const noBase* obj = world.GetNO(pt);
    if(obj->GetBM() != BlockingManner::None)
        return false;

    if(world.GetNode(pt).boundary_stones[BorderStonePos::OnPoint])
        return false;

    for(const auto dir : helpers::EnumRange<Direction>{})
    {
        if(world.GetPointRoad(pt, dir) != PointRoad::None)
            return false;
    }

    for(const MapPoint nb : world.GetNeighbours(pt))
    {
        if(world.GetNO(nb)->GetType() == NodalObjectType::Building)
            return false;
    }

    return world.IsOfTerrain(pt, [](const auto& desc) { return desc.IsVital(); });
}

double GetSmartForestBQScore(const BuildingQuality bq)
{
    switch(bq)
    {
        case BuildingQuality::Flag: return 8.0;
        case BuildingQuality::Hut: return 6.0;
        case BuildingQuality::Nothing: return 12.0;
        case BuildingQuality::House: return -12.0;
        case BuildingQuality::Mine: return -8.0;
        case BuildingQuality::Castle: return -32.0;
        case BuildingQuality::Harbor: return -40.0;
        default: return 0.0;
    }
}

bool IsSmartForestLowValueBQ(const BuildingQuality bq)
{
    return bq == BuildingQuality::Flag || bq == BuildingQuality::Hut;
}

bool IsSmartForestHighValueBQ(const BuildingQuality bq)
{
    return bq == BuildingQuality::House || bq == BuildingQuality::Castle || bq == BuildingQuality::Harbor
           || bq == BuildingQuality::Mine;
}

SmartForestMetrics ComputeSmartForestMetrics(const AIWorldView& aijh, const SmartForestConfig& config,
                                             const MapPoint& candidate)
{
    const GameWorldBase& world = aijh.GetWorld();
    SmartForestMetrics metrics;
    for(const MapPoint pt : world.GetPointsInRadiusWithCenter(candidate, config.radius))
    {
        if(!IsForesterPlantablePoint(world, pt))
            continue;

        const BuildingQuality bq = aijh.GetAINode(pt).bq;
        ++metrics.plantable;
        metrics.score += GetSmartForestBQScore(bq);
        if(IsSmartForestLowValueBQ(bq))
            ++metrics.lowValue;
        if(IsSmartForestHighValueBQ(bq))
            ++metrics.highValue;
    }

    metrics.score += 2.0 * metrics.plantable;
    return metrics;
}

std::optional<double> ComputeSmartForestRating(const AIWorldView& aijh, AIConstruction& construction,
                                               const SmartForestConfig& config, const MapPoint& candidate)
{
    const SmartForestMetrics metrics = ComputeSmartForestMetrics(aijh, config, candidate);
    if(metrics.plantable < config.minPlantable)
        return std::nullopt;

    const double plantable = static_cast<double>(metrics.plantable);
    if(static_cast<double>(metrics.lowValue) / plantable < config.minLowValueRatio)
        return std::nullopt;
    if(static_cast<double>(metrics.highValue) / plantable > config.maxHighValueRatio)
        return std::nullopt;

    const unsigned forestersInZone =
      construction.CountUsualBuildingInRadius(candidate, config.radius, BuildingType::Forester) + 1;
    if(forestersInZone > config.maxClusterForesters)
        return std::nullopt;

    const double clusterPenalty =
      std::max(0.0, static_cast<double>(config.plantablePerForester * forestersInZone)
                      - static_cast<double>(metrics.plantable));
    return metrics.score - clusterPenalty;
}

bool MeetsMinimalResourceRequirement(const AIWorldView& aijh, const BuildingType type, const AIResource res,
                                     int rating)
{
    const unsigned minRequirement = aijh.GetConfig().locationParams[type].minResources[res];
    return rating >= static_cast<int>(minRequirement);
}

bool MeetsPointResourceRequirements(const AIWorldView& aijh, const AIQueryService& queries, const BuildingType type,
                                    const MapPoint& pt)
{
    const auto& minRequirements = aijh.GetConfig().locationParams[type].minResources;
    for(const auto resource : helpers::enumRange<AIResource>())
    {
        if(minRequirements[resource] == 0)
            continue;

        if(!MeetsMinimalResourceRequirement(aijh, type, resource, queries.CalcResourceValue(pt, resource)))
            return false;
    }
    return true;
}

double ComputeResourcePenalty(const AIWorldView& aijh, const AIQueryService& queries, const BuildingType type,
                           const MapPoint& pt)
{
    double totalPenalty = 0;
    const auto& penaltyParams = aijh.GetConfig().locationParams[type].resourcePenalty;
    for(const auto resource : helpers::enumRange<AIResource>())
    {
        const BuildParams params = penaltyParams[resource];
        if(!params.enabled)
            continue;

        const unsigned resourceValue = queries.CalcResourceValue(pt, resource);
        if(resourceValue < params.min)
            continue;

        const double value = CALC::calcCount(resourceValue, params);
        totalPenalty += static_cast<int>(std::min<double>(value, params.max));
    }

    // Penalize for BQ degradation at adjacent points
    const double bqPenaltyBuildLocation = aijh.GetConfig().bqPenalty.buildLocation;
    if(bqPenaltyBuildLocation > 0.0)
    {
        const double bqPenalty = queries.EstimateBuildLocationBQPenalty(pt) * bqPenaltyBuildLocation;
        totalPenalty += static_cast<int>(bqPenalty);
    }

    return totalPenalty;
}

int ComputeResourceRating(const AIWorldView& aijh, const AIQueryService& queries, AIConstruction& construction,
                          const BuildingType type, const MapPoint& pt)
{
    const auto& resourceRating = aijh.GetConfig().locationParams[type].resourceRating;
    int rating = 1;
    if(resourceRating.enabled)
        rating = queries.CalcResourceValue(pt, resourceRating.resource);

    rating += ComputeRatingBonus(aijh, construction, type, pt);
    return rating;
}

bool UseMinimalResourceOnlyForInexhaustibleMine(const AIWorldView& aijh, const BuildingType type)
{
    if(!aijh.GetGameSettings().isEnabled(AddonId::INEXHAUSTIBLE_MINES))
        return false;

    switch(type)
    {
        case BuildingType::GoldMine:
        case BuildingType::IronMine:
        case BuildingType::CoalMine: return true;
        default: return false;
    }
}
} // namespace

GlobalPositionFinder::GlobalPositionFinder(AIPlanningContext& aijh) : aijh(aijh) {}

bool GlobalPositionFinder::IsSuitableBuildingPosition(const BuildingType type, const MapPoint& pt,
                                                      const AIQueryService& queries) const
{
    if(!pt.isValid())
        return false;

    const Node& node = aijh.GetAINode(pt);
    if(!node.reachable || !node.owned || node.farmed)
        return false;

    const BuildingQuality requiredSize = BUILDING_SIZE[type];
    if(!canUseBq(node.bq, requiredSize))
        return false;

    const bool isMilitaryBuilding = BuildingProperties::IsMilitary(type);
    if(!isMilitaryBuilding && queries.IsReservedMilitaryBorderSlot(pt, node.bq))
        return false;
    if(queries.isHarborPosClose(pt, 2, true) && requiredSize != BuildingQuality::Harbor)
        return false;
    if(IsBorderBlocked(aijh, queries, type, pt))
        return false;
    if(!MeetsPointResourceRequirements(aijh, queries, type, pt))
        return false;
    if(isMilitaryBuilding && aijh.GetWorld().IsOnRoad(aijh.GetWorld().GetNeighbour(pt, Direction::SouthEast)))
        return false;
    if(isMilitaryBuilding && aijh.GetWorld().IsMilitaryBuildingNearNode(pt, aijh.GetPlayerId()))
        return false;

    return true;
}

bool GlobalPositionFinder::CheckProximity(const BuildingType type, const MapPoint& pt) const
{
    if(!pt.isValid())
        return false;

    AIConstruction& construction = aijh.GetConstruction();
    const auto locationParam = aijh.GetConfig().locationParams[type];
    const unsigned buildingCount = aijh.GetBldPlanner().GetNumBuildings(type);

    for(const auto otherType : helpers::enumRange<BuildingType>())
    {
        if(type == BuildingType::Forester && otherType == BuildingType::Forester)
            continue;

        const ProximityParams proximity = locationParam.proximity[otherType];
        if(proximity.enabled)
        {
            const unsigned minRadius = static_cast<unsigned>(CALC::calcCount(buildingCount, proximity.minimal));
            if(otherType == BuildingType::Storehouse)
            {
                if(construction.OtherStoreInRadius(pt, minRadius))
                    return false;
            } else if(construction.OtherUsualBuildingInRadius(pt, minRadius, otherType))
                return false;
        }
    }
    return true;
}

bool GlobalPositionFinder::ValidFishInRange(const MapPoint pt) const
{
    constexpr unsigned maxRadius = 5;
    const GameWorldBase& world = aijh.GetWorld();
    return world.CheckPointsInRadius(
      pt, maxRadius,
      [&world, pt](const MapPoint curPt, unsigned) {
          if(world.GetNode(curPt).resources.has(ResourceType::Fish))
          {
              for(const MapPoint nb : world.GetNeighbours(curPt))
              {
                  if(world.FindHumanPath(pt, nb, 10))
                      return true;
              }
          }
          return false;
      },
      false);
}

bool GlobalPositionFinder::ValidStoneinRange(const MapPoint pt) const
{
    constexpr unsigned maxRadius = 8;
    const GameWorldBase& world = aijh.GetWorld();
    for(MapCoord tx = world.GetXA(pt, Direction::West), r = 1; r <= maxRadius;
        tx = world.GetXA(MapPoint(tx, pt.y), Direction::West), ++r)
    {
        MapPoint t2(tx, pt.y);
        for(unsigned i = 2; i < 8; ++i)
        {
            for(MapCoord r2 = 0; r2 < r; t2 = world.GetNeighbour(t2, convertToDirection(i)), ++r2)
            {
                if(world.GetNO(t2)->GetType() == NodalObjectType::Granite)
                {
                    if(world.FindHumanPath(pt, t2, 20))
                        return true;
                }
            }
        }
    }
    return false;
}

std::optional<double> GlobalPositionFinder::GetPointRating(const BuildingType type, const MapPoint& pt) const
{
    return GetPointRatingInternal(type, pt, true);
}

std::optional<double> GlobalPositionFinder::GetPointRatingInternal(const BuildingType type, const MapPoint& pt,
                                                                   const bool useSmartForest) const
{
    const AIQueryService& queries = aijh.GetInterface().Queries();
    if(!IsSuitableBuildingPosition(type, pt, queries) || !CheckProximity(type, pt))
        return std::nullopt;

    AIConstruction& construction = aijh.GetConstruction();

    switch(type)
    {
        case BuildingType::Quarry:
            if(!ValidStoneinRange(pt))
                return std::nullopt;
            break;
        default: break;
    }

    double baseRating = 0;
    if(useSmartForest && type == BuildingType::Forester && aijh.GetConfig().smartForest.enabled)
    {
        const std::optional<double> smartForestRating =
          ComputeSmartForestRating(aijh, construction, aijh.GetConfig().smartForest, pt);
        if(!smartForestRating)
            return std::nullopt;
        baseRating = *smartForestRating;
    } else if(UseMinimalResourceOnlyForInexhaustibleMine(aijh, type))
        baseRating = 25.0;
    else
        baseRating = ComputeResourceRating(aijh, queries, construction, type, pt);
    const double resourcePenalty = ComputeResourcePenalty(aijh, queries, type, pt);
    return baseRating - resourcePenalty;
}

MapPoint GlobalPositionFinder::FindBestPosition(const BuildingType bt)
{
    aijh.RecordGlobalPositionSearchInvocation();
    auto findBest = [this, bt](const bool useSmartForest) {
        double bestValue = 0.0;
        MapPoint bestPt = MapPoint::Invalid();
        const MapExtent mapSize = aijh.GetWorld().GetSize();

        RTTR_FOREACH_PT(MapPoint, mapSize)
        {
            const std::optional<double> pointRating = GetPointRatingInternal(bt, pt, useSmartForest);
            if(!pointRating)
                continue;
            if(*pointRating > bestValue)
            {
                bestValue = *pointRating;
                bestPt = pt;
            }
        }

        return bestPt;
    };

    MapPoint bestPt = findBest(true);
    if(!bestPt.isValid() && bt == BuildingType::Forester && aijh.GetConfig().smartForest.enabled)
        bestPt = findBest(false);

    return bestPt;
}

} // namespace AIJH
