// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/MapCoordinates.h"

#include <optional>
#include <vector>

class AIQueryService;
class GameWorldBase;

namespace AIJH {

/// Cached diagnostic estimate of ware-flow pressure on owned road tiles.
class AIRoadWorkload
{
public:
    AIRoadWorkload(const AIQueryService& queries, const GameWorldBase& world);

    void Refresh();
    std::optional<unsigned> Get(MapPoint pt) const;

private:
    const AIQueryService& queries_;
    const GameWorldBase& world_;
    std::vector<std::optional<unsigned>> nodeValues_;
};

} // namespace AIJH
