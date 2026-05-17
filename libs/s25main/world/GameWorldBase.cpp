// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "world/GameWorldBase.h"
#include "BQCalculator.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "MapGeometry.h"
#include "RttrForeachPt.h"
#include "SoundManager.h"
#include "TradePathCache.h"
#include "addons/const_addons.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "figures/nofPassiveSoldier.h"
#include "helpers/EnumRange.h"
#include "helpers/containerUtils.h"
#include "lua/LuaInterfaceGame.h"
#include "notifications/NodeNote.h"
#include "notifications/PlayerNodeNote.h"
#include "pathfinding/FreePathFinder.h"
#include "pathfinding/RoadPathFinder.h"
#include "nodeObjs/noFlag.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GameConsts.h"
#include "gameData/TerrainDesc.h"
#include <algorithm>
#include <utility>

GameWorldBase::GameWorldBase(std::vector<GamePlayer> players, const GlobalGameSettings& gameSettings, EventManager& em)
    : roadPathFinder(new RoadPathFinder(*this)), freePathFinder(new FreePathFinder(*this)), players(std::move(players)),
      gameSettings(gameSettings), em(em), soundManager(std::make_unique<SoundManager>()), lua(nullptr), gi(nullptr)
{}

GameWorldBase::~GameWorldBase() = default;

void GameWorldBase::Init(const MapExtent& mapSize, DescIdx<LandscapeDesc> lt)
{
    RTTR_Assert(GetDescription().terrain.size() > 0); // Must have game data initialized
    World::Init(mapSize, lt);
    freePathFinder->Init(mapSize);
}

void GameWorldBase::InitAfterLoad()
{
    RTTR_FOREACH_PT(MapPoint, GetSize())
        RecalcBQ(pt);
}

GamePlayer& GameWorldBase::GetPlayer(const unsigned id)
{
    RTTR_Assert(id < GetNumPlayers());
    return players[id];
}

const GamePlayer& GameWorldBase::GetPlayer(const unsigned id) const
{
    RTTR_Assert(id < GetNumPlayers());
    return players[id];
}

unsigned GameWorldBase::GetNumPlayers() const
{
    return players.size();
}

bool GameWorldBase::IsSinglePlayer() const
{
    bool foundPlayer = false;
    for(const PlayerInfo& player : players)
    {
        if(player.ps == PlayerState::Occupied)
        {
            if(foundPlayer)
                return false;
            else
                foundPlayer = true;
        }
    }
    return true;
}

bool GameWorldBase::IsRoadAvailable(const bool boat_road, const MapPoint pt) const
{
    // Obstacles
    if(GetNode(pt).obj)
    {
        BlockingManner bm = GetNode(pt).obj->GetBM();
        if(bm != BlockingManner::None)
            return false;
    }

    // dont build on the border
    if(GetNode(pt).boundary_stones[BorderStonePos::OnPoint])
        return false;

    for(const auto dir : helpers::EnumRange<Direction>{})
    {
        // Roads around charburner piles are not possible
        if(GetNO(GetNeighbour(pt, dir))->GetBM() == BlockingManner::NothingAround)
            return false;

        // Other roads at this point?
        if(GetPointRoad(pt, dir) != PointRoad::None)
            return false;
    }

    // Terrain (distinguish between water and land routes)
    if(!boat_road)
    {
        bool flagPossible = false;

        for(const DescIdx<TerrainDesc> tIdx : GetTerrainsAround(pt))
        {
            const TerrainBQ bq = GetDescription().get(tIdx).GetBQ();
            if(bq == TerrainBQ::Danger)
                return false;
            else if(bq != TerrainBQ::Nothing)
                flagPossible = true;
        }

        return flagPossible;
    } else
    {
        // For water routes, there must be water around the point
        if(!IsWaterPoint(pt))
            return false;
    }

    return true;
}

bool GameWorldBase::RoadAlreadyBuilt(const bool /*boat_road*/, const MapPoint start,
                                     const std::vector<Direction>& route)
{
    MapPoint tmp(start);
    for(unsigned i = 0; i < route.size() - 1; ++i)
    {
        // Correct road at this point?
        if(GetPointRoad(tmp, route[i]) == PointRoad::None)
            return false;

        tmp = GetNeighbour(tmp, route[i]);
    }
    return true;
}

bool GameWorldBase::IsOnRoad(const MapPoint& pt) const
{
    // This must be fast for BQ calculation so don't use GetVisiblePointRoad
    for(const auto roadDir : helpers::EnumRange<RoadDir>{})
        if(GetRoad(pt, roadDir) != PointRoad::None)
            return true;
    for(const auto roadDir : helpers::EnumRange<RoadDir>{})
    {
        const Direction oppositeDir = getOppositeDir(roadDir);
        if(GetRoad(GetNeighbour(pt, oppositeDir), roadDir) != PointRoad::None)
            return true;
    }
    return false;
}

bool GameWorldBase::IsFlagAround(const MapPoint& pt) const
{
    for(const MapPoint nb : GetNeighbours(pt))
    {
        if(GetNO(nb)->GetBM() == BlockingManner::Flag)
            return true;
    }
    return false;
}

void GameWorldBase::RecalcBQForRoad(const MapPoint pt)
{
    RecalcBQ(pt);

    for(const Direction dir : {Direction::East, Direction::SouthEast, Direction::SouthWest})
        RecalcBQ(GetNeighbour(pt, dir));
}

namespace {
bool IsMilBldOfOwner(const GameWorldBase& gwb, MapPoint pt, unsigned char owner)
{
    return gwb.IsMilitaryBuildingOnNode(pt, false) && (gwb.GetNode(pt).owner == owner);
}
} // namespace

bool GameWorldBase::IsMilitaryBuildingNearNode(const MapPoint nPt, const unsigned char player) const
{
    // Search for a military building within a radius of 4 points
    return CheckPointsInRadius(
      nPt, 4, [this, player](auto pt, auto) { return IsMilBldOfOwner(*this, pt, player + 1); }, false);
}

bool GameWorldBase::IsMilitaryBuildingOnNode(const MapPoint pt, bool attackBldsOnly) const
{
    const noBase* obj = GetNO(pt);
    if(obj->GetType() == NodalObjectType::Building || obj->GetType() == NodalObjectType::Buildingsite)
    {
        BuildingType buildingType = static_cast<const noBaseBuilding*>(obj)->GetBuildingType();
        if(BuildingProperties::IsMilitary(buildingType))
            return true;
        if(!attackBldsOnly
           && (buildingType == BuildingType::Headquarters || buildingType == BuildingType::HarborBuilding))
            return true;
    }

    return false;
}

sortedMilitaryBlds GameWorldBase::LookForMilitaryBuildings(const MapPoint pt, unsigned short radius) const
{
    return militarySquares.GetBuildingsInRange(pt, radius);
}

noFlag* GameWorldBase::GetRoadFlag(MapPoint pt, Direction& dir, const helpers::OptionalEnum<Direction> prevDir)
{
    // Getting a flag is const
    const noFlag* flag = const_cast<const GameWorldBase*>(this)->GetRoadFlag(pt, dir, prevDir);
    // However we self are not const, so we allow returning a non-const flag pointer
    return const_cast<noFlag*>(flag);
}

const noFlag* GameWorldBase::GetRoadFlag(MapPoint pt, Direction& dir, helpers::OptionalEnum<Direction> prevDir) const
{
    while(true)
    {
        // Search where the road continues
        helpers::OptionalEnum<Direction> nextDir;
        for(const auto i : helpers::EnumRange<Direction>{})
        {
            if(i != prevDir && GetPointRoad(pt, i) != PointRoad::None)
            {
                nextDir = i;
                break;
            }
        }

        if(!nextDir)
            return nullptr;

        pt = GetNeighbour(pt, *nextDir);

        // Finally reached the end of the road and arrived at a flag?
        if(GetNO(pt)->GetType() == NodalObjectType::Flag)
        {
            dir = *nextDir + 3u;
            return GetSpecObj<noFlag>(pt);
        }
        prevDir = *nextDir + 3u;
    }
}

Position GameWorldBase::GetNodePos(const MapPoint pt) const
{
    return ::GetNodePos(pt, GetNode(pt).altitude);
}

void GameWorldBase::VisibilityChanged(const MapPoint pt, unsigned player, Visibility /*oldVis*/, Visibility /*newVis*/)
{
    GetNotifications().publish(PlayerNodeNote(PlayerNodeNote::Visibility, pt, player));
}

/// Changes the height of a point and the associated shadows
void GameWorldBase::AltitudeChanged(const MapPoint pt)
{
    RecalcBQAroundPointBig(pt);
    GetNotifications().publish(NodeNote(NodeNote::Altitude, pt));
}

void GameWorldBase::RecalcBQAroundPoint(const MapPoint pt)
{
    RecalcBQ(pt);
    for(const MapPoint nb : GetNeighbours(pt))
        RecalcBQ(nb);
}

void GameWorldBase::RecalcBQAroundPointBig(const MapPoint pt)
{
    // Point and radius 1
    RecalcBQAroundPoint(pt);
    // And radius 2
    for(unsigned i = 0; i < 12; ++i)
        RecalcBQ(GetNeighbour2(pt, i));
}

Visibility GameWorldBase::CalcVisiblityWithAllies(const MapPoint pt, const unsigned char player) const
{
    const MapNode& node = GetNode(pt);
    Visibility best_visibility = node.fow[player].visibility;

    if(best_visibility == Visibility::Visible)
        return best_visibility;

    /// Team view enabled?
    if(GetGGS().teamView)
    {
        const GamePlayer& curPlayer = GetPlayer(player);
        // Then check whether team members have a better view of this point
        for(unsigned i = 0; i < GetNumPlayers(); ++i)
        {
            if(i != player && curPlayer.IsAlly(i))
            {
                if(node.fow[i].visibility > best_visibility)
                    best_visibility = node.fow[i].visibility;
            }
        }
    }

    return best_visibility;
}

bool GameWorldBase::IsCoastalPointToSeaWithHarbor(const MapPoint pt) const
{
    unsigned short sea = GetSeaFromCoastalPoint(pt);
    if(sea)
    {
        const unsigned numHarborPts = GetNumHarborPoints();
        for(unsigned i = 1; i <= numHarborPts; i++)
        {
            if(IsHarborAtSea(i, sea))
                return true;
        }
    }
    return false;
}

template<typename T_IsHarborOk>
unsigned GameWorldBase::GetHarborInDir(const MapPoint pt, const unsigned origin_harborId, const ShipDirection& dir,
                                       T_IsHarborOk isHarborOk) const
{
    RTTR_Assert(origin_harborId);

    // Determine in which direction this point differs from the starting point
    helpers::OptionalEnum<Direction> coastal_point_dir;
    const MapPoint hbPt = GetHarborPoint(origin_harborId);

    for(const auto dir : helpers::EnumRange<Direction>{})
    {
        if(GetNeighbour(hbPt, dir) == pt)
        {
            coastal_point_dir = dir;
            break;
        }
    }

    RTTR_Assert(coastal_point_dir);

    unsigned short seaId = GetSeaId(origin_harborId, *coastal_point_dir);
    const std::vector<HarborPos::Neighbor>& neighbors = GetHarborNeighbors(origin_harborId, dir);

    for(auto neighbor : neighbors)
    {
        if(IsHarborAtSea(neighbor.id, seaId) && isHarborOk(neighbor.id))
            return neighbor.id;
    }

    // Nothing found
    return 0;
}

/// Functor that returns true, when the owner of a point is set and different than the player
struct IsPointOwnerDifferent
{
    const GameWorldBase& gwb;
    // Owner to compare. Note that owner=0 --> No owner => owner=player+1
    const unsigned char cmpOwner;

    IsPointOwnerDifferent(const GameWorldBase& gwb, const unsigned char player) : gwb(gwb), cmpOwner(player + 1) {}

    bool operator()(const MapPoint pt, unsigned /*distance*/) const
    {
        const unsigned char owner = gwb.GetNode(pt).owner;
        return owner != 0 && owner != cmpOwner;
    }
};

/// Is it possible for a player to build a harbor at this place
bool GameWorldBase::IsHarborPointFree(const unsigned harborId, const unsigned char player) const
{
    MapPoint hbPos(GetHarborPoint(harborId));

    // Check whether the area within a certain radius is occupied either by the player or not at all, unless the harbor
    // and the flag are in the player's territory
    MapPoint flagPos = GetNeighbour(hbPos, Direction::SouthEast);
    if(GetNode(hbPos).owner != player + 1 || GetNode(flagPos).owner != player + 1)
    {
        if(CheckPointsInRadius(hbPos, 4, IsPointOwnerDifferent(*this, player), false))
            return false;
    }

    return GetNode(hbPos).bq == BuildingQuality::Harbor;
}

/// Searches for free harbor points where a harbor can still be built
unsigned GameWorldBase::GetNextFreeHarborPoint(const MapPoint pt, const unsigned origin_harborId,
                                               const ShipDirection& dir, const unsigned char player) const
{
    return GetHarborInDir(pt, origin_harborId, dir,
                          [this, player](auto harborId) { return this->IsHarborPointFree(harborId, player); });
}

/// Determines the distance to the nearest harbor point for any point on the map
unsigned GameWorldBase::CalcDistanceToNearestHarbor(const MapPoint pos) const
{
    unsigned min_distance = 0xffffffff;
    for(unsigned i = 1; i <= GetNumHarborPoints(); ++i)
        min_distance = std::min(min_distance, this->CalcDistance(pos, GetHarborPoint(i)));

    return min_distance;
}

/// returns true when a harborpoint is in SEAATTACK_DISTANCE for figures!
bool GameWorldBase::IsAHarborInSeaAttackDistance(const MapPoint pos) const
{
    for(unsigned i = 1; i <= GetNumHarborPoints(); ++i)
    {
        if(CalcDistance(pos, GetHarborPoint(i)) < SEAATTACK_DISTANCE)
        {
            if(FindHumanPath(pos, GetHarborPoint(i), SEAATTACK_DISTANCE))
                return true;
        }
    }
    return false;
}

std::vector<unsigned> GameWorldBase::GetUsableTargetHarborsForAttack(const MapPoint targetPt,
                                                                     std::vector<bool>& use_seas,
                                                                     const unsigned char player_attacker) const
{
    // Walk to the flag of the bld/harbor. Important to check because in some locations where the coast is north of the
    // harbor this might be blocked
    const MapPoint flagPt = GetNeighbour(targetPt, Direction::SouthEast);
    std::vector<unsigned> harbor_points;
    // Check each possible harbor
    for(unsigned curHbId = 1; curHbId <= GetNumHarborPoints(); ++curHbId)
    {
        const MapPoint harborPt = GetHarborPoint(curHbId);

        if(CalcDistance(harborPt, targetPt) > SEAATTACK_DISTANCE)
            continue;

        // Not attacking this harbor and harbors block?
        if(targetPt != harborPt && GetGGS().getSelection(AddonId::SEA_ATTACK) == 1)
        {
            // Does an enemy harbor exist at current harbor spot? -> Can't attack through this harbor spot
            const auto* hb = GetSpecObj<nobHarborBuilding>(harborPt);
            if(hb && GetPlayer(player_attacker).IsAttackable(hb->GetPlayer()))
                continue;
        }

        // add seaIds from which we can actually attack the harbor
        bool harborinlist = false;
        for(const auto dir : helpers::enumRange<Direction>())
        {
            const unsigned short seaId = GetSeaId(curHbId, dir);
            if(!seaId)
                continue;
            // checks previously tested sea ids to skip pathfinding
            bool previouslytested = false;
            for(unsigned k = 0; k < rttr::enum_cast(dir); k++)
            {
                if(seaId == GetSeaId(curHbId, Direction(k)))
                {
                    previouslytested = true;
                    break;
                }
            }
            if(previouslytested)
                continue;

            // Can figures reach flag from coast
            const MapPoint coastalPt = GetCoastalPoint(curHbId, seaId);
            if((flagPt == coastalPt) || FindHumanPath(flagPt, coastalPt, SEAATTACK_DISTANCE))
            {
                use_seas.at(seaId - 1) = true;
                if(!harborinlist)
                {
                    harbor_points.push_back(curHbId);
                    harborinlist = true;
                }
            }
        }
    }
    return harbor_points;
}

std::vector<unsigned short> GameWorldBase::GetFilteredSeaIDsForAttack(const MapPoint targetPt,
                                                                      const std::vector<unsigned short>& usableSeas,
                                                                      const unsigned char player_attacker) const
{
    // Walk to the flag of the bld/harbor. Important to check because in some locations where the coast is north of the
    // harbor this might be blocked
    const MapPoint flagPt = GetNeighbour(targetPt, Direction::SouthEast);
    std::vector<unsigned short> confirmedSeaIds;
    // Check each possible harbor
    for(unsigned curHbId = 1; curHbId <= GetNumHarborPoints(); ++curHbId)
    {
        const MapPoint harborPt = GetHarborPoint(curHbId);

        if(CalcDistance(harborPt, targetPt) > SEAATTACK_DISTANCE)
            continue;

        // Not attacking this harbor and harbors block?
        if(targetPt != harborPt && GetGGS().getSelection(AddonId::SEA_ATTACK) == 1)
        {
            // Does an enemy harbor exist at current harbor spot? -> Can't attack through this harbor spot
            const auto* hb = GetSpecObj<nobHarborBuilding>(harborPt);
            if(hb && GetPlayer(player_attacker).IsAttackable(hb->GetPlayer()))
                continue;
        }

        for(const auto dir : helpers::enumRange<Direction>())
        {
            const unsigned short seaId = GetSeaId(curHbId, dir);
            if(!seaId)
                continue;
            // sea id is not in compare list or already confirmed? -> skip rest
            if(!helpers::contains(usableSeas, seaId) || helpers::contains(confirmedSeaIds, seaId))
                continue;

            // checks previously tested sea ids to skip pathfinding
            bool previouslytested = false;
            for(unsigned k = 0; k < rttr::enum_cast(dir); k++)
            {
                if(seaId == GetSeaId(curHbId, Direction(k)))
                {
                    previouslytested = true;
                    break;
                }
            }
            if(previouslytested)
                continue;

            // Can figures reach flag from coast
            MapPoint coastalPt = GetCoastalPoint(curHbId, seaId);
            if((flagPt == coastalPt) || FindHumanPath(flagPt, coastalPt, SEAATTACK_DISTANCE))
            {
                confirmedSeaIds.push_back(seaId);
                // all sea ids confirmed? return without changes
                if(confirmedSeaIds.size() == usableSeas.size())
                    return confirmedSeaIds;
            }
        }
    }
    return confirmedSeaIds;
}

/// Returns harbor points within range of a specific military building
std::vector<unsigned> GameWorldBase::GetHarborPointsAroundMilitaryBuilding(const MapPoint pt) const
{
    std::vector<unsigned> harbor_points;
    // Search for harbor points near the attacked building
    // Go through all of our harbors
    for(unsigned i = 1; i <= GetNumHarborPoints(); ++i)
    {
        const MapPoint harborPt = GetHarborPoint(i);

        if(CalcDistance(harborPt, pt) <= SEAATTACK_DISTANCE)
        {
            // Is a path found from the military building to the harbor, or is the target the harbor?
            if(pt == harborPt || FindHumanPath(pt, harborPt, SEAATTACK_DISTANCE))
                harbor_points.push_back(i);
        }
    }
    return harbor_points;
}

/// Returns the count or estimated strength (rank sum + count) of the available soldiers that can start a sea attack
/// from a specific sea ID
unsigned GameWorldBase::GetNumSoldiersForSeaAttackAtSea(const unsigned char player_attacker, unsigned short seaid,
                                                        bool returnCount) const
{
    // List all military buildings of the attacker that provide soldiers
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;
    unsigned attackercount = 0;
    // Angrenzende Häfen des Angreifers an den entsprechenden Meeren herausfinden
    const std::list<nobHarborBuilding*>& harbors = GetPlayer(player_attacker).GetBuildingRegister().GetHarbors();
    for(auto* harbor : harbors)
    {
        // Determine whether the harbor is on one of the seas that can also reach the enemy harbor points
        if(!IsHarborAtSea(harbor->GetHarborPosID(), seaid))
            continue;

        std::vector<nobHarborBuilding::SeaAttackerBuilding> tmp = harbor->GetAttackerBuildingsForSeaIdAttack();
        buildings.insert(buildings.begin(), tmp.begin(), tmp.end());
    }

    // Collect the soldiers from all military buildings
    for(auto& building : buildings)
    {
        // Get soldiers
        std::vector<nofPassiveSoldier*> tmp_soldiers =
          building.building->GetSoldiersForAttack(building.harbor->GetPos());

        // Found any at all?
        if(tmp_soldiers.empty())
            continue;

        // Add soldiers
        for(auto& tmp_soldier : tmp_soldiers)
        {
            if(returnCount)
                attackercount++;
            else
                attackercount += (tmp_soldier->GetRank() + 1); // private is rank 0 -> increase by 1-5
        }
    }
    return attackercount;
}

double GameWorldBase::ComputeCaptureRisk(const nobMilitary& building) const
{
    const unsigned defenderStrength = building.GetGarrisonStrengthWithBonus();
    unsigned enemyStrength = 0;

    sortedMilitaryBlds nearby = LookForMilitaryBuildings(building.GetPos(), 2);
    for(const nobBaseMilitary* candidate : nearby)
    {
        if(candidate->GetPlayer() == building.GetPlayer())
            continue;
        if(!GetPlayer(candidate->GetPlayer()).IsAttackable(building.GetPlayer()))
            continue;

        const auto* enemyMilitary = dynamic_cast<const nobMilitary*>(candidate);
        if(!enemyMilitary)
            continue;

        unsigned availableAttackers = 0;
        const unsigned contribution = enemyMilitary->GetSoldiersStrengthForAttack(building.GetPos(), availableAttackers);
        if(contribution == 0)
            continue;

        enemyStrength += contribution;
    }

    if(enemyStrength == 0)
        return 0.0;
    if(defenderStrength == 0)
        return 1.0;

    const double total = static_cast<double>(defenderStrength) + static_cast<double>(enemyStrength);
    if(total <= 0.0)
        return 0.0;

    return std::clamp(static_cast<double>(enemyStrength) / total, 0.0, 1.0);
}

/// Searches for available soldiers to attack this military building with a sea attack
std::vector<GameWorldBase::PotentialSeaAttacker>
GameWorldBase::GetSoldiersForSeaAttack(const unsigned char player_attacker, const MapPoint pt) const
{
    std::vector<GameWorldBase::PotentialSeaAttacker> attackers;
    // sea attack abgeschaltet per addon?
    if(!GetGGS().isEnabled(AddonId::SEA_ATTACK))
        return attackers;
    // Do we have an attackble military building?
    const auto* milBld = GetSpecObj<nobBaseMilitary>(pt);
    if(!milBld || !milBld->IsAttackable(player_attacker))
        return attackers;
    std::vector<bool> use_seas(GetNumSeas());

    // Possible harbor points near the building
    std::vector<unsigned> defender_harbors = GetUsableTargetHarborsForAttack(pt, use_seas, player_attacker);

    // List all military buildings of the attacker that provide soldiers
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;

    // Angrenzende Häfen des Angreifers an den entsprechenden Meeren herausfinden
    const std::list<nobHarborBuilding*>& harbors = GetPlayer(player_attacker).GetBuildingRegister().GetHarbors();
    for(auto* harbor : harbors)
    {
        // Determine whether the harbor is on one of the seas that can also reach the enemy harbor points
        bool is_at_sea = false;
        for(const auto dir : helpers::EnumRange<Direction>{})
        {
            const unsigned short seaId = GetSeaId(harbor->GetHarborPosID(), dir);
            if(seaId && use_seas[seaId - 1])
            {
                is_at_sea = true;
                break;
            }
        }

        if(!is_at_sea)
            continue;

        std::vector<nobHarborBuilding::SeaAttackerBuilding> tmp =
          harbor->GetAttackerBuildingsForSeaAttack(defender_harbors);
        for(auto& itBld : tmp)
        {
            // Check if the building was already inserted
            auto oldBldIt =
              helpers::find_if(buildings, nobHarborBuilding::SeaAttackerBuilding::CmpBuilding(itBld.building));
            if(oldBldIt == buildings.end())
            {
                // Not found -> Add
                buildings.push_back(itBld);
            } else if(oldBldIt->distance > itBld.distance
                      || (oldBldIt->distance == itBld.distance
                          && oldBldIt->harbor->GetObjId() > itBld.harbor->GetObjId()))
            {
                // New distance is smaller (with tie breaker for async prevention) -> update
                *oldBldIt = itBld;
            }
        }
    }

    // Collect the soldiers from all military buildings
    for(const auto& bld : buildings)
    {
        // Get soldiers
        std::vector<nofPassiveSoldier*> tmp_soldiers = bld.building->GetSoldiersForAttack(bld.harbor->GetPos());

        // Add soldiers
        for(nofPassiveSoldier* soldier : tmp_soldiers)
        {
            RTTR_Assert(!helpers::contains_if(attackers, PotentialSeaAttacker::CmpSoldier(soldier)));
            attackers.push_back(PotentialSeaAttacker(soldier, bld.harbor, bld.distance));
        }
    }

    return attackers;
}

void GameWorldBase::RecalcBQ(const MapPoint pt)
{
    BQCalculator calcBQ(*this);
    if(SetBQ(pt, calcBQ(pt, [this](auto pt) { return this->IsOnRoad(pt); })))
    {
        GetNotifications().publish(NodeNote(NodeNote::BQ, pt));
    }
}

void GameWorldBase::SetComputerBarrier(const MapPoint& pt, unsigned radius)
{
    for(const auto& pt : GetPointsInRadiusWithCenter(pt, radius))
        ptsInsideComputerBarriers.insert(pt);
}

bool GameWorldBase::IsInsideComputerBarrier(const MapPoint& pt) const
{
    return helpers::contains(ptsInsideComputerBarriers, pt);
}
