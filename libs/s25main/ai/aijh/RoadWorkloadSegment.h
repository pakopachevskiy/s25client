// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/MapCoordinates.h"

namespace AIJH {

struct RoadWorkloadSegment
{
    MapPoint flag1;
    MapPoint flag2;
    unsigned workload;
    unsigned length;
    bool waterRoad;
};

} // namespace AIJH
