// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "helpers/OptionalEnum.h"
#include "RoadEventLogger.h"
#include "world/GameWorldBase.h"
#include "gameTypes/MapCoordinates.h"
#include "gameTypes/RoadPathDirection.h"
#include <vector>

class GameInterface;
class MilitarySquares;
class noBaseBuilding;
class nobMilitary;
class noBuildingSite;
class noRoadNode;
class nofActiveSoldier;
class nofAttacker;
struct PlayerInfo;
class RoadSegment;
class TerritoryRegion;

enum class TerritoryChangeReason
{
    Build,     /// Building was build (and occupied for the first time)
    Destroyed, /// Building destroyed
    Captured   /// Owner changed
};

/// "Interface class" for the game
class GameWorld : public GameWorldBase
{
    /// Destroys player belongings if that pint does not belong to the player anymore
    void DestroyPlayerRests(MapPoint pt, unsigned char newOwner, const noBaseBuilding* exception,
                            unsigned capturingObjId);

    /// Return if there are deco-objects that can be removed when building roads
    bool HasRemovableObjForRoad(MapPoint pt) const;

    bool IsPointCompletelyVisible(const MapPoint& pt, unsigned char player, const noBaseBuilding* exception) const;
    /// Return if there is a scout (or an attacking soldier) of this player at that node with a visual range of at most
    /// the given distance. Excludes scouting ships!
    bool IsScoutingFigureOnNode(const MapPoint& pt, unsigned player, unsigned distance) const;
    /// Return true, if the point is explored by any ship of the player
    bool IsPointScoutedByShip(const MapPoint& pt, unsigned player) const;
    /// Recalculates the visibility of a point for the given player
    /// exception is a building (lookout tower, military building) that should not be included in the calculation,
    /// for example because it is being demolished
    void RecalcVisibility(MapPoint pt, unsigned char player, const noBaseBuilding* exception);
    /// Always sets the point to visible
    void MakeVisible(MapPoint pt, unsigned char player);

    /// Creates a region with territories marked around a building with the given radius
    TerritoryRegion CreateTerritoryRegion(const noBaseBuilding& building, unsigned radius,
                                          TerritoryChangeReason reason,
                                          const unsigned char* triggerOwnerOverride = nullptr) const;
    /// Cleans the region (removes edges of terrain and applies the allied border push addon
    void CleanTerritoryRegion(TerritoryRegion& region, TerritoryChangeReason reason,
                              const noBaseBuilding& triggerBld) const;

public:
    GameWorld(const std::vector<PlayerInfo>& players, const GlobalGameSettings& gameSettings, EventManager& em);
    ~GameWorld() override;

    /// Provides the game GUI interface to other players/game objects
    GameInterface* GetGameInterface() const { return gi; }
    TradePathCache& GetTradePathCache();
    void setEconHandler(std::unique_ptr<EconomyModeHandler> handler);

    /// Can this point be entered by people walking on roads? (fights!)
    bool IsRoadNodeForFigures(MapPoint pt);
    /// Stops all figures on roads that are moving toward this point (because of a fight)
    void StopOnRoads(MapPoint pt, helpers::OptionalEnum<Direction> dir = boost::none);

    /// Notifies that the point is available again and lets nearby figures continue moving if necessary
    void RoadNodeAvailable(MapPoint pt);

    /// Place a flag for the player specific
    void SetFlag(MapPoint pt, unsigned char player,
                 RoadEventLogger::FlagBuildReason reason = RoadEventLogger::FlagBuildReason::Manual);
    /// Flag should be destroyed
    void DestroyFlag(MapPoint pt, unsigned char playerId,
                     RoadEventLogger::FlagDemolitionReason reason = RoadEventLogger::FlagDemolitionReason::Manual,
                     unsigned initiatorPlayerId = 0);
    /// Place a building site
    void SetBuildingSite(BuildingType type, MapPoint pt, unsigned char player);
    /// Demolish a building or building site
    void DestroyBuilding(MapPoint pt, unsigned char player);

    /// Find a path for people using roads.
    RoadPathDirection FindHumanPathOnRoads(const noRoadNode& start, const noRoadNode& goal, unsigned* length = nullptr,
                                           MapPoint* firstPt = nullptr, const RoadSegment* forbidden = nullptr);
    /// Find a path for wares using roads.
    RoadPathDirection FindPathForWareOnRoads(const noRoadNode& start, const noRoadNode& goal,
                                             unsigned* length = nullptr, MapPoint* firstPt = nullptr,
                                             unsigned max = std::numeric_limits<unsigned>::max());
    /// Checks whether a ship route is still valid
    bool CheckShipRoute(MapPoint start, const std::vector<Direction>& route, unsigned pos, MapPoint* dest);
    /// Find a route for trade caravanes
    helpers::OptionalEnum<Direction> FindTradePath(MapPoint start, MapPoint dest, unsigned char player,
                                                   unsigned max_route = 0xffffffff, bool random_route = false,
                                                   std::vector<Direction>* route = nullptr,
                                                   unsigned* length = nullptr) const;
    /// Check whether trade path (starting from point @param start and at index @param startRouteIdx) is still valid.
    /// Optionally returns destination pt
    bool CheckTradeRoute(MapPoint start, const std::vector<Direction>& route, unsigned pos, unsigned char player,
                         MapPoint* dest = nullptr) const;

    /// Sets the road value around the point X,Y.
    void SetPointRoad(MapPoint pt, Direction dir, PointRoad type);

    /// Builds a road (not only visually, but really)
    void BuildRoad(unsigned char playerId, bool boat_road, MapPoint start, const std::vector<Direction>& route);

    /// Recalculates the ownership around a military building
    void RecalcTerritory(const noBaseBuilding& building, TerritoryChangeReason reason);

    /// Recalculates the land in a certain area around a current military building and returns whether anything would
    /// change (on ground important to the AI) if the building were destroyed
    bool DoesDestructionChangeTerritory(const noBaseBuilding& building) const;
    /// Return the building types that would be destroyed if the given military building were captured
    std::vector<BuildingType> GetBuildingsLostOnCapture(const nobMilitary& building) const;
    /// Estimate how many buildings would be destroyed if the given military building were captured
    unsigned CountBuildingsLostOnCapture(const nobMilitary& building) const;
    /// Invalidate cached capture-loss analysis for military buildings near the given point
    void InvalidateBuildingsLostOnCaptureCachesAround(MapPoint pt);
    /// Refresh capture risk and importance for all military buildings
    void UpdateMilitaryRiskEstimates();

    /// Attacks a military building at x,y (dispatches the soldiers for it, etc.)
    void Attack(unsigned char player_attacker, MapPoint pt, unsigned short soldiers_count, bool strong_soldiers);
    /// Attacks a military building with ships
    void AttackViaSea(unsigned char player_attacker, MapPoint pt, unsigned short soldiers_count, bool strong_soldiers);

    MilitarySquares& GetMilitarySquares();

    /// Burns down everything player-owned by destroying all player flags
    void Armageddon();

    /// Burns down everything owned by one player by destroying all flags of that player
    void Armageddon(unsigned char player);

    /// Is the point a suitable place to wait in front of the military building
    bool ValidWaitingAroundBuildingPoint(MapPoint pt, MapPoint center);
    /// Is this point a valid point for the given soldier to fight?
    bool IsValidPointForFighting(MapPoint pt, const nofActiveSoldier& soldier, bool avoid_military_building_flags);

    /// Recalculates visibility around a point with radius
    void RecalcVisibilitiesAroundPoint(MapPoint pt, MapCoord radius, unsigned char player,
                                       const noBaseBuilding* exception);
    /// Sets visibility around a point to visible (performance alternative to the above)
    void MakeVisibleAroundPoint(MapPoint pt, MapCoord radius, unsigned char player);
    /// Recalculates visibility at the edges when a scouting object moves
    void RecalcMovingVisibilities(MapPoint pt, unsigned char player, MapCoord radius, Direction moving_dir,
                                  MapPoint* enemy_territory);

    /// Return whether this is a border node (node belongs to player, but not all others around)
    bool IsBorderNode(MapPoint pt, unsigned char owner) const;

    // Converts resources between types or deletes them.
    // For games without gold.
    void ConvertMineResourceTypes(ResourceType from, ResourceType to);
    // Setup resources like gold and water after loading a new map
    void SetupResources();

    // Fills water depending on terrain and Addon setting
    void PlaceAndFixWater();

    /// Founds a new colony from a ship, returns true on success
    bool FoundColony(unsigned harbor_point, unsigned char player, unsigned short seaId);
    /// Registers a harbor building site that was placed from a ship
    void AddHarborBuildingSiteFromSea(noBuildingSite* building_site)
    {
        harbor_building_sites_from_sea.push_back(building_site);
    }
    /// Removes it. It is allowed to be called with a regular harbor building site (no-op in that case)
    void RemoveHarborBuildingSiteFromSea(noBuildingSite* building_site);
    /// Returns whether a specific building site is a building site that was built from a ship
    bool IsHarborBuildingSiteFromSea(const noBuildingSite* building_site) const;
    /// Returns a list of harbor points reachable from a specific harbor point
    std::vector<unsigned> GetUnexploredHarborPoints(unsigned hbIdToSkip, unsigned seaId, unsigned playerId) const;

    /// Writeable access to node. Use only for initial map setup!
    MapNode& GetNodeWriteable(MapPoint pt);
    /// Recalculates where border stones should be done after a change in the given region
    void RecalcBorderStones(Position startPt, Extent areaSize);

    /// Create Trade graphs
    void CreateTradeGraphs() final;

protected:
    void VisibilityChanged(MapPoint pt, unsigned player, Visibility oldVis, Visibility newVis) override;
};
