// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ai/AIResource.h"
#include "ai/aijh/runtime/AIMap.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/MapCoordinates.h"

#include <optional>
#include <string>

namespace AIJH {

class AIJob;

class AIDebugView
{
public:
    virtual ~AIDebugView() = default;

    virtual const std::string& GetPlayerName() const = 0;
    virtual const AIJob* GetCurrentJob() const = 0;
    virtual unsigned GetNumJobs() const = 0;
    virtual const Node& GetAINode(MapPoint pt) const = 0;
    virtual int GetResourceValueForDebug(MapPoint pt, AIResource res) const = 0;
    virtual std::optional<int> GetPointRating(BuildingType type, MapPoint pt) const = 0;
    virtual unsigned GetNumBuildingsWanted(BuildingType type) const = 0;
    virtual std::optional<unsigned> GetRoadWorkload(MapPoint pt) const = 0;
};

} // namespace AIJH
