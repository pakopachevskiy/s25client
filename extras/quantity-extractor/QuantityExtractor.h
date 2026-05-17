// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "building_quality_snapshot.pb.h"
#include "building_locations.pb.h"
#include "environment_snapshot.pb.h"
#include "road_locations.pb.h"

class Game;

namespace QuantityExtractor {

struct SnapshotFiles
{
    ru::pkopachevsky::proto::BuildingLocationsFile buildingLocations;
    ru::pkopachevsky::proto::RoadLocationsFile roadLocations;
    ru::pkopachevsky::proto::EnvironmentSnapshotFile environmentSnapshot;
    ru::pkopachevsky::proto::BuildingQualitySnapshot buildingQualitySnapshot;
};

SnapshotFiles ExtractSnapshots(const Game& game, unsigned gameframe);

} // namespace QuantityExtractor
