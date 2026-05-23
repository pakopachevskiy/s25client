// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "QuantityExtractor.h"

#include "BuildingRegister.h"
#include "Game.h"
#include "GamePlayer.h"
#include "RoadSegment.h"
#include "RttrForeachPt.h"
#include "buildings/noBaseBuilding.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "gameData/BuildingProperties.h"
#include "helpers/EnumRange.h"
#include "gameTypes/BuildingQuality.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noGranite.h"
#include "nodeObjs/noStaticObject.h"
#include "nodeObjs/noTree.h"
#include "world/BQCalculator.h"
#include "world/GameWorld.h"
#include "gameData/TerrainDesc.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace QuantityExtractor {
namespace {

namespace pb = ru::pkopachevsky::proto;

constexpr const char* kSchemaVersion = "quantity-extractor/v1";

static_assert(static_cast<int>(BuildingType::Headquarters) + 1 == pb::BUILDING_TYPE_HEADQUARTERS,
              "BuildingType proto enum no longer matches engine ordinals");
static_assert(static_cast<int>(BuildingType::Fortress) + 1 == pb::BUILDING_TYPE_FORTRESS,
              "BuildingType proto enum no longer matches engine ordinals");
static_assert(static_cast<int>(BuildingType::Storehouse) + 1 == pb::BUILDING_TYPE_STOREHOUSE,
              "BuildingType proto enum no longer matches engine ordinals");
static_assert(static_cast<int>(BuildingType::HarborBuilding) + 1 == pb::BUILDING_TYPE_HARBOR_BUILDING,
              "BuildingType proto enum no longer matches engine ordinals");
static_assert(static_cast<int>(BuildingQuality::Nothing) == pb::BUILDING_QUALITY_NOTHING,
              "BuildingQuality proto enum no longer matches engine ordinals");
static_assert(static_cast<int>(BuildingQuality::Harbor) == pb::BUILDING_QUALITY_HARBOR,
              "BuildingQuality proto enum no longer matches engine ordinals");

uint32_t ToOutputPlayerId(const unsigned playerId)
{
    return static_cast<uint32_t>(playerId + 1u);
}

pb::BuildingType ToProtoBuildingType(const BuildingType buildingType)
{
    if(!BuildingProperties::IsValid(buildingType))
        return pb::BUILDING_TYPE_UNSPECIFIED;
    return static_cast<pb::BuildingType>(static_cast<int>(buildingType) + 1);
}

pb::BuildingQuality ToProtoBuildingQuality(const BuildingQuality buildingQuality)
{
    switch(buildingQuality)
    {
        case BuildingQuality::Nothing: return pb::BUILDING_QUALITY_NOTHING;
        case BuildingQuality::Flag: return pb::BUILDING_QUALITY_FLAG;
        case BuildingQuality::Mine: return pb::BUILDING_QUALITY_MINE;
        case BuildingQuality::Hut: return pb::BUILDING_QUALITY_HUT;
        case BuildingQuality::House: return pb::BUILDING_QUALITY_HOUSE;
        case BuildingQuality::Castle: return pb::BUILDING_QUALITY_CASTLE;
        case BuildingQuality::Harbor: return pb::BUILDING_QUALITY_HARBOR;
    }
    return pb::BUILDING_QUALITY_NOTHING;
}

bool IsBuildingFlag(const GameWorld& world, const MapPoint pt)
{
    if(world.GetNO(pt)->GetType() != NodalObjectType::Flag)
        return false;

    const NodalObjectType northWestObjectType = world.GetNO(world.GetNeighbour(pt, Direction::NorthWest))->GetType();
    return northWestObjectType == NodalObjectType::Building || northWestObjectType == NodalObjectType::Buildingsite;
}

BuildingQuality CalculateBuildingQualityWithoutRoadsOrStandaloneFlags(const GameWorld& world, const MapPoint pt)
{
    const BQCalculator calcBQ(world);
    const auto isOnRoad = [](MapPoint) { return false; };
    const auto getBlockingManner = [&world](const MapPoint point) {
        const noBase* obj = world.GetNO(point);
        if(obj->GetType() == NodalObjectType::Flag && !IsBuildingFlag(world, point))
            return BlockingManner::None;
        return obj->GetBM();
    };
    return calcBQ(pt, isOnRoad, getBlockingManner, false);
}

pb::RoadDirection ToProtoDirection(const Direction dir)
{
    switch(dir)
    {
        case Direction::West: return pb::ROAD_DIRECTION_WEST;
        case Direction::NorthWest: return pb::ROAD_DIRECTION_NORTH_WEST;
        case Direction::NorthEast: return pb::ROAD_DIRECTION_NORTH_EAST;
        case Direction::East: return pb::ROAD_DIRECTION_EAST;
        case Direction::SouthEast: return pb::ROAD_DIRECTION_SOUTH_EAST;
        case Direction::SouthWest: return pb::ROAD_DIRECTION_SOUTH_WEST;
    }
    return pb::ROAD_DIRECTION_UNSPECIFIED;
}

pb::RoadLogType ToProtoRoadType(const RoadType roadType)
{
    switch(roadType)
    {
        case RoadType::Normal: return pb::ROAD_LOG_TYPE_NORMAL;
        case RoadType::Donkey: return pb::ROAD_LOG_TYPE_DONKEY;
        case RoadType::Water: return pb::ROAD_LOG_TYPE_WATER;
    }
    return pb::ROAD_LOG_TYPE_UNSPECIFIED;
}

bool IsRoadSnapshotSegment(const RoadSegment& road)
{
    // Building entrance segments end at the building tile; only flag-to-flag roads belong in road_locations.pb.
    return road.GetF1() && road.GetF2() && road.GetF1()->GetGOT() == GO_Type::Flag
           && road.GetF2()->GetGOT() == GO_Type::Flag;
}

struct BuildingRecord
{
    BuildingType type;
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t createdGameframe;
    uint32_t constructedGameframe;
};

struct ConstructionSiteRecord
{
    BuildingType type;
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t createdGameframe;
};

bool ByBuildingIdentity(const BuildingRecord& lhs, const BuildingRecord& rhs)
{
    return std::tie(lhs.type, lhs.id, lhs.x, lhs.y) < std::tie(rhs.type, rhs.id, rhs.x, rhs.y);
}

bool ByConstructionSiteIdentity(const ConstructionSiteRecord& lhs, const ConstructionSiteRecord& rhs)
{
    return std::tie(lhs.type, lhs.id, lhs.x, lhs.y) < std::tie(rhs.type, rhs.id, rhs.x, rhs.y);
}

BuildingRecord ToBuildingRecord(const noBaseBuilding& building)
{
    const MapPoint pos = building.GetPos();
    return BuildingRecord{building.GetBuildingType(),
                          static_cast<uint32_t>(building.GetObjId()),
                          static_cast<uint32_t>(pos.x),
                          static_cast<uint32_t>(pos.y),
                          static_cast<uint32_t>(building.GetBuildStartingFrame()),
                          static_cast<uint32_t>(building.GetBuildCompleteFrame())};
}

ConstructionSiteRecord ToConstructionSiteRecord(const noBuildingSite& site)
{
    const MapPoint pos = site.GetPos();
    return ConstructionSiteRecord{site.GetBuildingType(),
                                  static_cast<uint32_t>(site.GetObjId()),
                                  static_cast<uint32_t>(pos.x),
                                  static_cast<uint32_t>(pos.y),
                                  static_cast<uint32_t>(site.GetBuildStartingFrame())};
}

void AddBuildingRecord(const noBaseBuilding& building, std::vector<BuildingRecord>& records,
                       std::set<unsigned>& seenBuildingIds)
{
    if(seenBuildingIds.insert(building.GetObjId()).second)
        records.push_back(ToBuildingRecord(building));
}

pb::BuildingLocationsFile ExtractBuildings(const GameWorld& world, const unsigned gameframe)
{
    pb::BuildingLocationsFile file;
    file.set_schema_version(kSchemaVersion);
    file.set_gameframe(gameframe);

    for(unsigned playerId = 0; playerId < world.GetNumPlayers(); ++playerId)
    {
        const GamePlayer& player = world.GetPlayer(playerId);
        if(!player.isUsed())
            continue;

        std::vector<BuildingRecord> buildingRecords;
        std::vector<ConstructionSiteRecord> siteRecords;
        std::set<unsigned> seenBuildingIds;
        const BuildingRegister& buildingRegister = player.GetBuildingRegister();

        for(const BuildingType type : helpers::enumRange<BuildingType>())
        {
            if(!BuildingProperties::IsUsual(type))
                continue;
            for(const nobUsual* building : buildingRegister.GetBuildings(type))
                AddBuildingRecord(*building, buildingRecords, seenBuildingIds);
        }
        for(const nobBaseWarehouse* building : buildingRegister.GetStorehouses())
            AddBuildingRecord(*building, buildingRecords, seenBuildingIds);
        for(const nobMilitary* building : buildingRegister.GetMilitaryBuildings())
            AddBuildingRecord(*building, buildingRecords, seenBuildingIds);
        for(const noBuildingSite* site : buildingRegister.GetBuildingSites())
            siteRecords.push_back(ToConstructionSiteRecord(*site));

        std::sort(buildingRecords.begin(), buildingRecords.end(), ByBuildingIdentity);
        std::sort(siteRecords.begin(), siteRecords.end(), ByConstructionSiteIdentity);

        pb::PlayerBuildingLocations* protoPlayer = file.add_players();
        protoPlayer->set_player_id(ToOutputPlayerId(player.GetPlayerId()));

        for(const BuildingType type : helpers::enumRange<BuildingType>())
        {
            auto begin = std::lower_bound(buildingRecords.begin(), buildingRecords.end(), type,
                                          [](const BuildingRecord& record, BuildingType wanted) {
                                              return record.type < wanted;
                                          });
            auto end = std::upper_bound(buildingRecords.begin(), buildingRecords.end(), type,
                                        [](BuildingType wanted, const BuildingRecord& record) {
                                            return wanted < record.type;
                                        });
            if(begin == end)
                continue;

            pb::BuildingTypeLocations* typeLocations = protoPlayer->add_buildings_by_type();
            typeLocations->set_building_type(ToProtoBuildingType(type));
            for(auto it = begin; it != end; ++it)
            {
                pb::BuildingLocation* location = typeLocations->add_locations();
                location->set_building_id(it->id);
                location->set_x(it->x);
                location->set_y(it->y);
                location->set_created_gameframe(it->createdGameframe);
                location->set_constructed_gameframe(it->constructedGameframe);
            }
        }

        for(const BuildingType type : helpers::enumRange<BuildingType>())
        {
            auto begin = std::lower_bound(siteRecords.begin(), siteRecords.end(), type,
                                          [](const ConstructionSiteRecord& record, BuildingType wanted) {
                                              return record.type < wanted;
                                          });
            auto end = std::upper_bound(siteRecords.begin(), siteRecords.end(), type,
                                        [](BuildingType wanted, const ConstructionSiteRecord& record) {
                                            return wanted < record.type;
                                        });
            if(begin == end)
                continue;

            pb::ConstructionSiteTypeLocations* typeLocations = protoPlayer->add_construction_sites_by_type();
            typeLocations->set_building_type(ToProtoBuildingType(type));
            for(auto it = begin; it != end; ++it)
            {
                pb::ConstructionSiteLocation* location = typeLocations->add_locations();
                location->set_x(it->x);
                location->set_y(it->y);
                location->set_created_gameframe(it->createdGameframe);
                location->set_id(it->id);
            }
        }
    }

    return file;
}

struct RoadRecord
{
    uint32_t playerId;
    pb::RoadLogType roadType;
    uint32_t startX;
    uint32_t startY;
    uint32_t endX;
    uint32_t endY;
    std::vector<pb::RoadDirection> route;
};

bool ByRoadIdentity(const RoadRecord& lhs, const RoadRecord& rhs)
{
    return std::tie(lhs.playerId, lhs.roadType, lhs.startY, lhs.startX, lhs.endY, lhs.endX, lhs.route)
           < std::tie(rhs.playerId, rhs.roadType, rhs.startY, rhs.startX, rhs.endY, rhs.endX, rhs.route);
}

RoadRecord ToRoadRecord(const RoadSegment& road)
{
    const noRoadNode* ownerNode = road.GetF1() ? road.GetF1() : road.GetF2();
    const uint32_t playerId = ownerNode ? ToOutputPlayerId(ownerNode->GetPlayer()) : 0u;
    const MapPoint start = road.GetF1() ? road.GetF1()->GetPos() : MapPoint(0, 0);
    const MapPoint end = road.GetF2() ? road.GetF2()->GetPos() : MapPoint(0, 0);

    RoadRecord record{playerId,
                      ToProtoRoadType(road.GetRoadType()),
                      static_cast<uint32_t>(start.x),
                      static_cast<uint32_t>(start.y),
                      static_cast<uint32_t>(end.x),
                      static_cast<uint32_t>(end.y),
                      {}};
    record.route.reserve(road.GetLength());
    for(unsigned i = 0; i < road.GetLength(); ++i)
        record.route.push_back(ToProtoDirection(road.GetRoute(i)));
    return record;
}

pb::RoadLocationsFile ExtractRoads(const GameWorld& world, const unsigned gameframe)
{
    pb::RoadLocationsFile file;
    file.set_schema_version(kSchemaVersion);
    file.set_gameframe(gameframe);
    const MapExtent mapSize = world.GetSize();
    file.set_map_width(mapSize.x);
    file.set_map_height(mapSize.y);

    std::set<const RoadSegment*> uniqueRoads;
    RTTR_FOREACH_PT(MapPoint, mapSize)
    {
        const auto* flag = dynamic_cast<const noFlag*>(world.GetNO(pt));
        if(!flag)
            continue;
        for(const Direction dir : helpers::enumRange<Direction>())
        {
            if(const RoadSegment* road = flag->GetRoute(dir))
                uniqueRoads.insert(road);
        }
    }

    std::vector<RoadRecord> roads;
    roads.reserve(uniqueRoads.size());
    for(const RoadSegment* road : uniqueRoads)
    {
        if(IsRoadSnapshotSegment(*road))
            roads.push_back(ToRoadRecord(*road));
    }
    std::sort(roads.begin(), roads.end(), ByRoadIdentity);

    std::map<uint32_t, std::map<pb::RoadLogType, std::vector<RoadRecord>>> roadsByPlayer;
    for(unsigned playerId = 0; playerId < world.GetNumPlayers(); ++playerId)
    {
        if(world.GetPlayer(playerId).isUsed())
            roadsByPlayer.emplace(ToOutputPlayerId(playerId), std::map<pb::RoadLogType, std::vector<RoadRecord>>{});
    }
    for(const RoadRecord& road : roads)
    {
        if(road.playerId != 0u)
            roadsByPlayer[road.playerId][road.roadType].push_back(road);
    }

    for(const auto& playerEntry : roadsByPlayer)
    {
        pb::PlayerRoadLocations* protoPlayer = file.add_players();
        protoPlayer->set_player_id(playerEntry.first);

        for(const auto& typeEntry : playerEntry.second)
        {
            pb::RoadTypeLocations* typeLocations = protoPlayer->add_roads_by_type();
            typeLocations->set_road_type(typeEntry.first);
            for(const RoadRecord& road : typeEntry.second)
            {
                pb::RoadLocation* location = typeLocations->add_roads();
                location->mutable_start()->set_x(road.startX);
                location->mutable_start()->set_y(road.startY);
                location->mutable_end()->set_x(road.endX);
                location->mutable_end()->set_y(road.endY);
                for(const pb::RoadDirection dir : road.route)
                    location->add_route(dir);
                location->set_constructed_gameframe(0);
            }
        }
    }

    return file;
}

unsigned GetRemainingGranite(const noGranite& granite)
{
    return static_cast<unsigned>(granite.GetSize()) + 1u;
}

pb::EnvironmentSnapshotFile ExtractEnvironment(const GameWorld& world, const unsigned gameframe)
{
    pb::EnvironmentSnapshotFile file;
    file.set_schema_version(kSchemaVersion);
    file.set_gameframe(gameframe);
    const MapExtent mapSize = world.GetSize();
    file.set_map_width(mapSize.x);
    file.set_map_height(mapSize.y);

    RTTR_FOREACH_PT(MapPoint, mapSize)
    {
        const noBase* obj = world.GetNO(pt);
        if(const auto* tree = dynamic_cast<const noTree*>(obj))
        {
            const MapPoint pos = tree->GetPos();
            pb::Tree* protoTree = file.add_trees();
            protoTree->set_x(pos.x);
            protoTree->set_y(pos.y);
            protoTree->set_tree_type(tree->GetTreeType());
            protoTree->set_created_gameframe(0);
        } else if(const auto* granite = dynamic_cast<const noGranite*>(obj))
        {
            pb::Granite* protoGranite = file.add_granite_rocks();
            protoGranite->set_x(pt.x);
            protoGranite->set_y(pt.y);
            protoGranite->set_granite_type(static_cast<unsigned>(granite->GetGraniteType()));
            protoGranite->set_size(GetRemainingGranite(*granite));
            protoGranite->set_created_gameframe(0);
            protoGranite->set_updated_gameframe(0);
        } else if(obj->GetBM() != BlockingManner::None)
        {
            pb::BlockingObject* blocker = file.add_blocking_objects();
            blocker->set_x(pt.x);
            blocker->set_y(pt.y);
            blocker->set_object_id(static_cast<uint32_t>(obj->GetObjId()));
            blocker->set_go_type(static_cast<uint32_t>(obj->GetGOT()));
            blocker->set_nodal_object_type(static_cast<uint32_t>(obj->GetType()));
            blocker->set_blocking_manner(static_cast<uint32_t>(obj->GetBM()));

            if(const auto* staticObj = dynamic_cast<const noStaticObject*>(obj))
            {
                blocker->set_item_id(static_cast<uint32_t>(staticObj->GetItemID()));
                blocker->set_item_file(static_cast<uint32_t>(staticObj->GetItemFile()));
                blocker->set_size(static_cast<uint32_t>(staticObj->GetSize()));
            }
        }
    }

    return file;
}

pb::BuildingQualitySnapshot ExtractBuildingQualitySnapshot(const GameWorld& world, const unsigned gameframe)
{
    pb::BuildingQualitySnapshot file;
    file.set_schema_version(kSchemaVersion);
    file.set_gameframe(gameframe);
    const MapExtent mapSize = world.GetSize();
    file.set_map_width(mapSize.x);
    file.set_map_height(mapSize.y);
    file.mutable_nodes()->Reserve(static_cast<int>(mapSize.x) * static_cast<int>(mapSize.y));

    RTTR_FOREACH_PT(MapPoint, mapSize)
    {
        const MapNode& node = world.GetNode(pt);
        const noBase* obj = world.GetNO(pt);
        pb::BuildingQualityNode* protoNode = file.add_nodes();

        protoNode->set_cell_index(static_cast<uint32_t>(pt.y) * mapSize.x + pt.x);
        protoNode->set_owner_id(static_cast<uint32_t>(node.owner));
        protoNode->set_altitude(static_cast<uint32_t>(node.altitude));
        protoNode->set_terrain_bq_1(static_cast<uint32_t>(world.GetDescription().get(node.t1).GetBQ()));
        protoNode->set_terrain_bq_2(static_cast<uint32_t>(world.GetDescription().get(node.t2).GetBQ()));
        protoNode->set_harbor_id(static_cast<uint32_t>(node.harborId));
        protoNode->set_road_east(static_cast<uint32_t>(node.roads[RoadDir::East]));
        protoNode->set_road_south_east(static_cast<uint32_t>(node.roads[RoadDir::SouthEast]));
        protoNode->set_road_south_west(static_cast<uint32_t>(node.roads[RoadDir::SouthWest]));
        protoNode->set_blocking_manner(static_cast<uint32_t>(obj->GetBM()));
        if(obj->GetBM() != BlockingManner::None)
        {
            protoNode->set_blocking_object_id(static_cast<uint32_t>(obj->GetObjId()));
            protoNode->set_blocking_go_type(static_cast<uint32_t>(obj->GetGOT()));
        }
        protoNode->set_raw_bq(static_cast<uint32_t>(node.bq));
        protoNode->set_building_quality(ToProtoBuildingQuality(node.bq));
        protoNode->set_no_road_building_quality(
          ToProtoBuildingQuality(CalculateBuildingQualityWithoutRoadsOrStandaloneFlags(world, pt)));
        for(unsigned playerId = 0; playerId < world.GetNumPlayers(); ++playerId)
            protoNode->add_adjusted_bq_by_player(static_cast<uint32_t>(world.GetBQ(pt, playerId)));
    }

    return file;
}

} // namespace

SnapshotFiles ExtractSnapshots(const Game& game, const unsigned gameframe)
{
    SnapshotFiles files;
    files.buildingLocations = ExtractBuildings(game.world_, gameframe);
    files.roadLocations = ExtractRoads(game.world_, gameframe);
    files.environmentSnapshot = ExtractEnvironment(game.world_, gameframe);
    files.buildingQualitySnapshot = ExtractBuildingQualitySnapshot(game.world_, gameframe);
    return files;
}

} // namespace QuantityExtractor
