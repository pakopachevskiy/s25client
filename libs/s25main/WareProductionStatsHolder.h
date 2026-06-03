// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gameData/MaxPlayers.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/GoodTypes.h"
#include "helpers/EnumArray.h"
#include <cstdint>

enum class WareConsumptionConsumer : uint8_t
{
    Construction,
    Headquarters,
    Barracks,
    Guardhouse,
    Nothing2,
    Watchtower,
    Vineyard,
    Winery,
    Temple,
    Nothing6,
    Fortress,
    GraniteMine,
    CoalMine,
    IronMine,
    GoldMine,
    LookoutTower,
    Nothing7,
    Catapult,
    Woodcutter,
    Fishery,
    Quarry,
    Forester,
    Slaughterhouse,
    Hunter,
    Brewery,
    Armory,
    Metalworks,
    Ironsmelter,
    Charburner,
    PigFarm,
    Storehouse,
    Nothing9,
    Mill,
    Bakery,
    Sawmill,
    Mint,
    Well,
    Shipyard,
    Farm,
    DonkeyBreeder,
    HarborBuilding,
};

constexpr auto maxEnumValue(WareConsumptionConsumer)
{
    return WareConsumptionConsumer::HarborBuilding;
}

struct WareProductionWindowStats
{
    helpers::EnumArray<uint32_t, GoodType> produced{};
    helpers::EnumArray<uint32_t, GoodType> consumed{};
    helpers::MultiEnumArray<uint32_t, WareConsumptionConsumer, GoodType> consumedByConsumer{};
};

namespace WareProductionStatsHolder {

constexpr unsigned WINDOW_SIZE_GF = 1000;

WareConsumptionConsumer ToConsumer(BuildingType buildingType);
void ReportProduced(unsigned gf, unsigned char playerId, GoodType good, unsigned count = 1);
void ReportConsumed(unsigned gf, unsigned char playerId, GoodType good, WareConsumptionConsumer consumer,
                    unsigned count = 1);
void ReportConsumed(unsigned gf, unsigned char playerId, GoodType good, BuildingType consumer, unsigned count = 1);
void ReportConstructionConsumed(unsigned gf, unsigned char playerId, GoodType good, unsigned count = 1);
const WareProductionWindowStats& GetPreviousWindowStats(unsigned char playerId, unsigned currentGf);
void Reset();

} // namespace WareProductionStatsHolder
