// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "EnvironmentEventLogger.h"

#include "RttrForeachPt.h"
#include "ai/aijh/debug/StatsConfig.h"
#include "environment_log.pb.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "nodeObjs/noGranite.h"
#include "nodeObjs/noTree.h"
#include "world/GameWorldBase.h"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace bfs = boost::filesystem;
namespace pb = ru::pkopachevsky::proto;

struct HeaderInfo
{
    uint32_t mapWidth = 0;
    uint32_t mapHeight = 0;
    bool isValid = false;
};

HeaderInfo gHeaderInfo;
bool gHeaderWritten = false;
std::vector<pb::EnvironmentLogRecord> gPendingRecords;
unsigned gNextFlushGF = 0;
bool gHasNextFlushGF = false;
constexpr unsigned kFlushPeriodGF = 500;

bfs::path GetEnvironmentLogPath()
{
    return bfs::path(STATS_CONFIG.statsPath) / "environment_log.pb";
}

template<typename T>
bool WriteDelimitedMessage(std::ostream& os, const T& message)
{
    std::string payload;
    if(!message.SerializeToString(&payload))
        return false;

    google::protobuf::io::OstreamOutputStream zeroCopyOut(&os);
    google::protobuf::io::CodedOutputStream codedOut(&zeroCopyOut);
    codedOut.WriteVarint32(static_cast<uint32_t>(payload.size()));
    codedOut.WriteRaw(payload.data(), static_cast<int>(payload.size()));
    return !codedOut.HadError() && os.good();
}

void RememberHeaderInfo(const GameWorldBase& world)
{
    const MapExtent size = world.GetSize();
    gHeaderInfo.mapWidth = size.x;
    gHeaderInfo.mapHeight = size.y;
    gHeaderInfo.isValid = true;
}

bool EnsureHeaderWritten(std::ofstream& log)
{
    if(gHeaderWritten)
        return true;
    if(!gHeaderInfo.isValid)
        return false;

    const bfs::path path = GetEnvironmentLogPath();
    const bool needsHeader = !bfs::exists(path) || bfs::file_size(path) == 0;
    if(needsHeader)
    {
        pb::EnvironmentLogHeader header;
        header.set_map_width(gHeaderInfo.mapWidth);
        header.set_map_height(gHeaderInfo.mapHeight);
        if(!WriteDelimitedMessage(log, header))
            return false;
    }

    gHeaderWritten = true;
    return true;
}

std::ofstream OpenEnvironmentLog()
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment))
        return {};
    return std::ofstream(GetEnvironmentLogPath().string(), std::ios::binary | std::ios::app);
}

void FlushPendingRecords()
{
    if(gPendingRecords.empty())
        return;

    std::ofstream log = OpenEnvironmentLog();
    if(!log)
        return;
    if(!EnsureHeaderWritten(log))
        return;

    for(const auto& record : gPendingRecords)
    {
        if(!WriteDelimitedMessage(log, record))
            return;
    }

    gPendingRecords.clear();
}

struct PendingFlushAtExit
{
    ~PendingFlushAtExit() { FlushPendingRecords(); }
};

PendingFlushAtExit gPendingFlushAtExit;

pb::EnvironmentLogRecord CreateRecord(unsigned gf, pb::EnvironmentEventType eventType, MapPoint pt)
{
    pb::EnvironmentLogRecord record;
    record.set_gameframe(gf);
    record.set_event_type(eventType);
    record.set_x(pt.x);
    record.set_y(pt.y);
    return record;
}

void EnqueueRecord(unsigned gf, const GameWorldBase& world, pb::EnvironmentLogRecord&& record)
{
    RememberHeaderInfo(world);
    gPendingRecords.push_back(std::move(record));

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

namespace EnvironmentEventLogger {

void LogInitialEnvironment(unsigned gf, const GameWorldBase& world)
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment))
        return;

    RTTR_FOREACH_PT(MapPoint, world.GetSize())
    {
        const noBase* obj = world.GetNO(pt);
        switch(obj->GetType())
        {
            case NodalObjectType::Tree:
            {
                const auto& tree = static_cast<const noTree&>(*obj);
                auto record = CreateRecord(gf, pb::ENVIRONMENT_EVENT_TYPE_TREE_INITIAL, pt);
                record.set_tree_type(tree.GetTreeType());
                EnqueueRecord(gf, world, std::move(record));
            }
            break;
            case NodalObjectType::Granite:
            {
                const auto& granite = static_cast<const noGranite&>(*obj);
                auto record = CreateRecord(gf, pb::ENVIRONMENT_EVENT_TYPE_GRANITE_INITIAL, pt);
                record.set_granite_type(static_cast<unsigned>(granite.GetGraniteType()));
                EnqueueRecord(gf, world, std::move(record));
            }
            break;
            default: break;
        }
    }
}

void LogTreePlanted(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree)
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment))
        return;

    auto record = CreateRecord(gf, pb::ENVIRONMENT_EVENT_TYPE_TREE_PLANTED, pt);
    record.set_tree_type(tree.GetTreeType());
    EnqueueRecord(gf, world, std::move(record));
}

void LogTreeCut(unsigned gf, const GameWorldBase& world, MapPoint pt, const noTree& tree)
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment))
        return;

    auto record = CreateRecord(gf, pb::ENVIRONMENT_EVENT_TYPE_TREE_CUT, pt);
    record.set_tree_type(tree.GetTreeType());
    EnqueueRecord(gf, world, std::move(record));
}

void LogGraniteHew(unsigned gf, const GameWorldBase& world, MapPoint pt, const noGranite& granite,
                   unsigned char sizeBefore, unsigned char sizeAfter)
{
    if(!STATS_CONFIG.IsEventLoggerEnabled(EventLoggerType::Environment))
        return;

    auto record = CreateRecord(gf, pb::ENVIRONMENT_EVENT_TYPE_GRANITE_HEW, pt);
    record.set_granite_type(static_cast<unsigned>(granite.GetGraniteType()));
    record.set_granite_size_before(sizeBefore);
    record.set_granite_size_after(sizeAfter);
    EnqueueRecord(gf, world, std::move(record));
}

} // namespace EnvironmentEventLogger
