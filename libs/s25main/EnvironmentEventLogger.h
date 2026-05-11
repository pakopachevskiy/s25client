// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/MapCoordinates.h"

class GameWorldBase;
class noGranite;
class noTree;

namespace EnvironmentEventLogger {

void LogInitialEnvironment(unsigned gf, const GameWorldBase& world);
void LogTreePlanted(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree);
void LogTreeCut(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree);
void LogGraniteHew(unsigned gf, const GameWorldBase& world, MapPoint pt, const noGranite& granite,
                   unsigned char sizeBefore, unsigned char sizeAfter);

} // namespace EnvironmentEventLogger
