// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/Direction.h"
#include "gameTypes/MapCoordinates.h"
#include <vector>

class GameWorldBase;

using FP_Node_OK_Callback = bool (*)(const GameWorldBase&, const MapPoint, const Direction, const void*);
using FP_Node_Cost_Callback = double (*)(const GameWorldBase&, MapPoint, MapPoint, Direction, unsigned, bool,
                                         const void*);

// There are 2 callback types:
// IsNodeToDestOk: Called for every point to check if this node is usable
// IsNodeOk: Additionally called for every point but the destination

class FreePathFinder
{
    GameWorldBase& gwb_;
    unsigned currentVisit;
    Extent size_;

public:
    FreePathFinder(GameWorldBase& gwb) : gwb_(gwb), currentVisit(0), size_(0, 0) {}
    void Init(const MapExtent& mapSize);

    /// Pathfinding in open terrain - Template version. Users need to include FreePathFinderImpl.h
    /// TNodeChecker must implement: bool IsNodeOk(MapPoint pt, unsigned char dirFromPrevPt) and bool
    /// IsNodeToDestOk(MapPoint pt, unsigned char dirFromPrevPt)
    template<class TNodeChecker>
    bool FindPath(MapPoint start, MapPoint dest, bool randomRoute, unsigned maxLength, std::vector<Direction>* route,
                  unsigned* length, Direction* firstDir, const TNodeChecker& nodeChecker);

    bool FindPathAlternatingConditions(MapPoint start, MapPoint dest, bool randomRoute, unsigned maxLength,
                                       std::vector<Direction>* route, unsigned* length, Direction* firstDir,
                                       FP_Node_OK_Callback IsNodeOK, FP_Node_OK_Callback IsNodeOKAlternate,
                                       FP_Node_OK_Callback IsNodeToDestOk, const void* param);

    bool FindPathAlternatingConditionsWeighted(MapPoint start, MapPoint dest, bool randomRoute, unsigned maxLength,
                                               std::vector<Direction>* route, unsigned* length, Direction* firstDir,
                                               FP_Node_OK_Callback IsNodeOK, FP_Node_OK_Callback IsNodeOKAlternate,
                                               FP_Node_OK_Callback IsNodeToDestOk,
                                               FP_Node_Cost_Callback GetNodeCost, const void* param);

    /// Determines whether a free route is still passable and returns the endpoint of the route
    template<class TNodeChecker>
    bool CheckRoute(MapPoint start, const std::vector<Direction>& route, unsigned pos, const TNodeChecker& nodeChecker,
                    MapPoint* dest) const;

private:
    void IncreaseCurrentVisit();
};
