// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "WareProductionStatsHolder.h"

#include "gameData/ShieldConsts.h"

#include <array>

namespace {

struct WindowSlot
{
    bool valid = false;
    unsigned windowIndex = 0;
    WareProductionWindowStats stats;
};

std::array<std::array<WindowSlot, 2>, MAX_PLAYERS> gPlayerWindowStats;
const WareProductionWindowStats kEmptyStats{};

bool IsTrackableGood(const GoodType good)
{
    return good != GoodType::Nothing;
}

WindowSlot& GetSlotForWrite(const unsigned char playerId, const unsigned windowIndex)
{
    WindowSlot& slot = gPlayerWindowStats[playerId][windowIndex % gPlayerWindowStats[playerId].size()];
    if(!slot.valid || slot.windowIndex != windowIndex)
    {
        slot.valid = true;
        slot.windowIndex = windowIndex;
        slot.stats = WareProductionWindowStats{};
    }
    return slot;
}

const WindowSlot* GetSlotForRead(const unsigned char playerId, const unsigned windowIndex)
{
    const WindowSlot& slot = gPlayerWindowStats[playerId][windowIndex % gPlayerWindowStats[playerId].size()];
    if(!slot.valid || slot.windowIndex != windowIndex)
        return nullptr;
    return &slot;
}

} // namespace

namespace WareProductionStatsHolder {

WareConsumptionConsumer ToConsumer(const BuildingType buildingType)
{
    return static_cast<WareConsumptionConsumer>(static_cast<uint8_t>(buildingType) + 1u);
}

void ReportProduced(const unsigned gf, const unsigned char playerId, const GoodType good, const unsigned count)
{
    if(playerId >= MAX_PLAYERS || count == 0)
        return;

    const GoodType normalizedGood = ConvertShields(good);
    if(!IsTrackableGood(normalizedGood))
        return;

    WindowSlot& slot = GetSlotForWrite(playerId, gf / WINDOW_SIZE_GF);
    slot.stats.produced[normalizedGood] += count;
}

void ReportConsumed(const unsigned gf, const unsigned char playerId, const GoodType good,
                    const WareConsumptionConsumer consumer, const unsigned count)
{
    if(playerId >= MAX_PLAYERS || count == 0)
        return;

    const GoodType normalizedGood = ConvertShields(good);
    if(!IsTrackableGood(normalizedGood))
        return;

    WindowSlot& slot = GetSlotForWrite(playerId, gf / WINDOW_SIZE_GF);
    slot.stats.consumed[normalizedGood] += count;
    slot.stats.consumedByConsumer[consumer][normalizedGood] += count;
}

void ReportConsumed(const unsigned gf, const unsigned char playerId, const GoodType good, const BuildingType consumer,
                    const unsigned count)
{
    ReportConsumed(gf, playerId, good, ToConsumer(consumer), count);
}

void ReportConstructionConsumed(const unsigned gf, const unsigned char playerId, const GoodType good,
                                const unsigned count)
{
    ReportConsumed(gf, playerId, good, WareConsumptionConsumer::Construction, count);
}

const WareProductionWindowStats& GetPreviousWindowStats(const unsigned char playerId, const unsigned currentGf)
{
    if(playerId >= MAX_PLAYERS)
        return kEmptyStats;

    const unsigned currentWindowIndex = currentGf / WINDOW_SIZE_GF;
    if(currentWindowIndex == 0)
        return kEmptyStats;

    if(const WindowSlot* slot = GetSlotForRead(playerId, currentWindowIndex - 1))
        return slot->stats;
    return kEmptyStats;
}

void Reset()
{
    gPlayerWindowStats = {};
}

} // namespace WareProductionStatsHolder
