//
// Created by Codex on 19.02.26.
//

#ifndef GLOBALPOSITIONFINDER_H
#define GLOBALPOSITIONFINDER_H

#include "gameTypes/BuildingType.h"
#include "gameTypes/BuildingQuality.h"
#include "gameTypes/MapCoordinates.h"
#include <optional>

class AIQueryService;

namespace AIJH {
class AIPlanningContext;

class GlobalPositionFinder
{
public:
    GlobalPositionFinder(AIPlanningContext& aijh);

    MapPoint FindBestPosition(BuildingType bt);
    std::optional<double> GetPointRating(BuildingType type, const MapPoint& pt) const;

private:
    bool IsSuitableBuildingPosition(BuildingType type, const MapPoint& pt, const AIQueryService& queries) const;
    bool CheckProximity(BuildingType type, const MapPoint& pt) const;
    std::optional<double> GetPointRatingInternal(BuildingType type, const MapPoint& pt, bool useSmartForest) const;
    bool ValidFishInRange(MapPoint pt) const;
    bool ValidStoneinRange(MapPoint pt) const;

    AIPlanningContext& aijh;
};
} // namespace AIJH

#endif // GLOBALPOSITIONFINDER_H
