// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "BuildingEventLogger.h"

#include "ai/aijh/debug/StatsConfig.h"
#include "building_log.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include <boost/filesystem/path.hpp>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
namespace pb = ru::pkopachevsky::proto;

std::unordered_set<const void*> gConstructedSites;
unsigned gLastLoggedGF = 0;
bool gHasLastLoggedGF = false;
std::vector<pb::BuildingLogRecord> gPendingRecords;
unsigned gNextFlushGF = 0;
bool gHasNextFlushGF = false;
constexpr unsigned kFlushPeriodGF = 500;

std::ofstream OpenBuildingLog()
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Building))
        return {};
    const boost::filesystem::path path = boost::filesystem::path(STATS_CONFIG.statsPath) / "building_log.pb";
    return std::ofstream(path.string(), std::ios::binary | std::ios::app);
}

pb::BuildingType ToProtoBuildingType(const BuildingType buildingType)
{
    const int raw = static_cast<int>(buildingType);
    if(raw >= static_cast<int>(BuildingType::Headquarters) && raw <= static_cast<int>(BuildingType::HarborBuilding))
        return static_cast<pb::BuildingType>(raw + 1);
    return pb::BuildingType::BUILDING_TYPE_UNSPECIFIED;
}

bool WriteDelimitedRecord(std::ostream& os, const pb::BuildingLogRecord& record)
{
    std::string payload;
    if(!record.SerializeToString(&payload))
        return false;

    google::protobuf::io::OstreamOutputStream zeroCopyOut(&os);
    google::protobuf::io::CodedOutputStream codedOut(&zeroCopyOut);
    codedOut.WriteVarint32(static_cast<uint32_t>(payload.size()));
    codedOut.WriteRaw(payload.data(), static_cast<int>(payload.size()));
    return !codedOut.HadError() && os.good();
}

void FlushPendingRecords()
{
    if(gPendingRecords.empty())
        return;

    std::ofstream log = OpenBuildingLog();
    if(!log)
        return;

    for(const auto& record : gPendingRecords)
    {
        if(!WriteDelimitedRecord(log, record))
            return;
    }

    gPendingRecords.clear();
}

struct PendingFlushAtExit
{
    ~PendingFlushAtExit() { FlushPendingRecords(); }
};

PendingFlushAtExit gPendingFlushAtExit;

void LogEvent(unsigned gf, unsigned char playerId, pb::BuildingLogEvent event, BuildingType buildingType,
              unsigned buildingId, unsigned x, unsigned y)
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Building))
        return;

    pb::BuildingLogRecord record;
    if(gHasLastLoggedGF && gf >= gLastLoggedGF)
        record.set_delta_gf(gf - gLastLoggedGF);
    else if(!gHasLastLoggedGF)
        record.set_delta_gf(gf);
    else
        record.set_delta_gf(0);
    gLastLoggedGF = gf;
    gHasLastLoggedGF = true;

    record.set_player_id(static_cast<uint32_t>(playerId + 1));
    record.set_event(event);
    record.set_building_type(ToProtoBuildingType(buildingType));
    record.set_building_id(static_cast<uint32_t>(buildingId));
    record.set_x(static_cast<uint32_t>(x));
    record.set_y(static_cast<uint32_t>(y));
    gPendingRecords.push_back(record);

    if(!gHasNextFlushGF)
    {
        gNextFlushGF = kFlushPeriodGF;
        gHasNextFlushGF = true;
    }

    if(gf >= gNextFlushGF)
    {
        FlushPendingRecords();
        while(gf >= gNextFlushGF)
            gNextFlushGF += kFlushPeriodGF;
    }
}

} // namespace

namespace BuildingEventLogger {

void LogConstructionSiteCreated(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId,
                                unsigned x, unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_CONSTRUCTION_SITE_CREATED, buildingType, buildingId, x, y);
}

void MarkConstructionSiteConstructed(const void* sitePtr)
{
    if(sitePtr)
        gConstructedSites.insert(sitePtr);
}

void LogConstructionSiteCancelled(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId,
                                  unsigned x, unsigned y, const void* sitePtr)
{
    if(sitePtr)
    {
        const auto it = gConstructedSites.find(sitePtr);
        if(it != gConstructedSites.end())
        {
            gConstructedSites.erase(it);
            return;
        }
    }
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_CONSTRUCTION_SITE_CANCELLED, buildingType, buildingId, x, y);
}

void LogBuilderArrive(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId, unsigned x,
                      unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_BUILDER_ARRIVE, buildingType, buildingId, x, y);
}

void LogBoardDeliver(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId, unsigned x,
                     unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_BOARD_DELIVER, buildingType, buildingId, x, y);
}

void LogStoneDeliver(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId, unsigned x,
                     unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_STONE_DELIVER, buildingType, buildingId, x, y);
}

void LogBuildingConstructed(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId,
                            unsigned x, unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_CONSTRUCTED, buildingType, buildingId, x, y);
}

void LogBuildingInhabited(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId,
                          unsigned x, unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_INHABITED, buildingType, buildingId, x, y);
}

void LogBuildingDestroyed(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId,
                          unsigned x, unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_DESTROYED, buildingType, buildingId, x, y);
}

void LogBuildingCaptured(unsigned gf, unsigned char playerId, BuildingType buildingType, unsigned buildingId, unsigned x,
                         unsigned y)
{
    LogEvent(gf, playerId, pb::BUILDING_LOG_EVENT_CAPTURED, buildingType, buildingId, x, y);
}

} // namespace BuildingEventLogger
