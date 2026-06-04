// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameTypes/GoodTypes.h"
#include "helpers/EnumArray.h"
#include <cstdint>

class AIPlayer;
class GameWorldBase;

struct WareDemandSnapshot
{
    helpers::EnumArray<uint32_t, GoodType> demand{};
    helpers::EnumArray<bool, GoodType> calculated{};
};

namespace WareDemandStatsHolder {

const WareDemandSnapshot& GetCurrentDemand(const GameWorldBase& world, unsigned char playerId, unsigned currentGf,
                                           const AIPlayer* aiPlayer);
void Reset();

} // namespace WareDemandStatsHolder
