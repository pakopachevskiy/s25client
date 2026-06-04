// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwBuildings.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "WindowManager.h"
#include "WineLoader.h"
#include "addons/const_addons.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlImageButton.h"
#include "controls/ctrlOptionGroup.h"
#include "files.h"
#include "iwBaseWarehouse.h"
#include "iwBuilding.h"
#include "iwHarborBuilding.h"
#include "iwHelp.h"
#include "iwMilitaryBuilding.h"
#include "iwTempleBuilding.h"
#include "ogl/FontStyle.h"
#include "ogl/glFont.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/BuildingCount.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/const_gui_ids.h"
#include <algorithm>
#include <string>

namespace {
enum
{
    ID_PlayerGroup = 100,
};

constexpr unsigned ID_PlayerButtonBase = 1000;
constexpr unsigned playerSelectionHeight = 60;

std::string GetPlayerStatus(const GamePlayer& player)
{
    if(player.IsDefeated())
        return "---";
    else if(player.isHuman())
        return "#" + std::to_string(player.GetPlayerId() + 1);
    else
        return _("COMP");
}

glArchivItem_Bitmap* GetPlayerImage(const Nation nation)
{
    switch(nation)
    {
        case Nation::Africans: return LOADER.GetImageN("io", 257);
        case Nation::Japanese: return LOADER.GetImageN("io", 253);
        case Nation::Romans: return LOADER.GetImageN("io", 252);
        case Nation::Vikings: return LOADER.GetImageN("io", 256);
        case Nation::Babylonians: return LOADER.GetImageN("io_new", 7);
    }
    return LOADER.GetImageN("io", 252);
}
} // namespace

void iwBuildings::setBuildingOrder()
{
    // Order of the buildings in which they will be shown
    bts = {
      BuildingType::Barracks,       BuildingType::Guardhouse, BuildingType::Watchtower,     BuildingType::Fortress,
      BuildingType::GraniteMine,    BuildingType::CoalMine,   BuildingType::IronMine,       BuildingType::GoldMine,
      BuildingType::LookoutTower,   BuildingType::Catapult,   BuildingType::Woodcutter,     BuildingType::Fishery,
      BuildingType::Quarry,         BuildingType::Forester,   BuildingType::Slaughterhouse, BuildingType::Hunter,
      BuildingType::Brewery,        BuildingType::Armory,     BuildingType::Metalworks,     BuildingType::Ironsmelter,
      BuildingType::PigFarm,
      BuildingType::Storehouse, // entry 21
      BuildingType::Mill,           BuildingType::Bakery,     BuildingType::Sawmill,        BuildingType::Mint,
      BuildingType::Well,           BuildingType::Shipyard,   BuildingType::Farm,           BuildingType::DonkeyBreeder,
      BuildingType::Charburner,
      BuildingType::HarborBuilding,                                                 // entry 31
      BuildingType::Vineyard,       BuildingType::Winery,     BuildingType::Temple, // entry 34
    };

    const auto isUnused = [&](BuildingType const& bld) {
        if(!wineaddon::isAddonActive(gwv.GetWorld()) && wineaddon::isWineAddonBuildingType(bld))
            return true;
        if(!gwv.GetWorld().GetGGS().isEnabled(AddonId::CHARBURNER) && bld == BuildingType::Charburner)
            return true;
        return false;
    };
    helpers::erase_if(bts, isUnused);
}

// Distance of the first icon from the upper-left window edge
const Extent bldContentOffset(30, 40);
// Distance between individual icons
const Extent iconSpacing(40, 48);
// Distance of the labels below the icons
const unsigned short font_distance_y = 20;

iwBuildings::iwBuildings(GameWorldView& gwv, GameCommandFactory& gcFactory)
    : IngameWindow(CGI_BUILDINGS, IngameWindow::posLastOrCenter, Extent(185, 480), _("Buildings"),
                   LOADER.GetImageN("resource", 41)),
      gwv(gwv), gcFactory(gcFactory), selectedPlayerId(gwv.GetViewer().GetPlayerId()), numPlayingPlayers(0)
{
    setBuildingOrder();

    if(IsPlayerSelectionEnabled())
    {
        const GameWorldBase& world = gwv.GetWorld();
        for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
        {
            if(world.GetPlayer(i).isUsed())
                ++numPlayingPlayers;
        }
    }

    Extent windowSize = iconSpacing * Extent(4, helpers::divCeil(bts.size(), 4) + 1) + GetBuildingContentOffset();
    if(IsPlayerSelectionEnabled())
        windowSize.x = std::max(windowSize.x, numPlayingPlayers * 34);
    Resize(windowSize);

    AddPlayerSelection();

    const Nation playerNation = gwv.GetWorld().GetPlayer(selectedPlayerId).nation;
    const Extent buildingContentOffset = GetBuildingContentOffset();
    // Create icons for the individual buildings
    for(unsigned y = 0; y < bts.size() / 4 + (bts.size() % 4 > 0 ? 1 : 0); ++y)
    {
        for(unsigned x = 0; x < 4; ++x)
        {
            if(y * 4 + x >= bts.size()) //-V547
                break;

            Extent btSize = Extent(32, 32);
            DrawPoint btPos = buildingContentOffset - btSize / 2 + iconSpacing * DrawPoint(x, y);
            AddImageButton(y * 4 + x, btPos, btSize, TextureColor::Grey,
                           LOADER.GetNationIcon(playerNation, bts[y * 4 + x]), _(BUILDING_NAMES[bts[y * 4 + x]]));
        }
    }

    // "Help" button
    Extent btSize = Extent(30, 32);
    AddImageButton(35, GetFullSize() - DrawPoint(14, 20) - btSize, btSize, TextureColor::Grey,
                   LOADER.GetImageN("io", 225), _("Help"));
}

/// Draw the building counts
void iwBuildings::Msg_PaintAfter()
{
    static boost::format fmt("%1%/%2%");
    IngameWindow::Msg_PaintAfter();
    // Determine counts
    BuildingCount bc = gwv.GetWorld().GetPlayer(selectedPlayerId).GetBuildingRegister().GetBuildingNums();

    // Write counts below the buildings
    DrawPoint rowPos = GetDrawPos() + GetBuildingContentOffset() + DrawPoint(0, font_distance_y);
    for(unsigned y = 0; y < helpers::divCeil(bts.size(), 4); ++y)
    {
        DrawPoint curPos = rowPos;
        for(unsigned x = 0; x < 4; x++)
        {
            if(y * 4 + x >= bts.size()) //-V547
                break;

            fmt % bc.buildings[bts[y * 4 + x]] % bc.buildingSites[bts[y * 4 + x]];
            NormalFont->Draw(curPos, fmt.str(), FontStyle::CENTER, COLOR_YELLOW);
            curPos.x += iconSpacing.x;
        }
        rowPos.y += iconSpacing.y;
    }
}

void iwBuildings::Draw_()
{
    IngameWindow::Draw_();

    if(IsMinimized() || !IsPlayerSelectionEnabled())
        return;

    DrawPlayerSelection();
}

void iwBuildings::Msg_ButtonClick(const unsigned ctrl_id)
{
    if(ctrl_id == 35) // Help button
    {
        WINDOWMANAGER.ReplaceWindow(
          std::make_unique<iwHelp>(_("The building statistics window gives you an insight into "
                                     "the number of buildings you have, by type. The number on "
                                     "the left is the total number of this type of building "
                                     "completed, the number on the right shows how many are "
                                     "currently under construction.")));
        return;
    }

    // no buildings of type complete? -> do nothing
    const GamePlayer& selectedPlayer = gwv.GetWorld().GetPlayer(selectedPlayerId);
    const BuildingRegister& buildingRegister = selectedPlayer.GetBuildingRegister();

    BuildingType bldType = bts[ctrl_id];
    if(BuildingProperties::IsMilitary(bldType))
        GoToFirstMatching<iwMilitaryBuilding>(bldType, buildingRegister.GetMilitaryBuildings());
    else if(bldType == BuildingType::HarborBuilding)
        GoToFirstMatching<iwHarborBuilding>(bldType, buildingRegister.GetHarbors());
    else if(BuildingProperties::IsWareHouse(bldType))
        GoToFirstMatching<iwBaseWarehouse>(bldType, buildingRegister.GetStorehouses());
    else if(bldType == BuildingType::Temple)
        GoToFirstMatching<iwTempleBuilding>(bldType, buildingRegister.GetBuildings(bldType));
    else
        GoToFirstMatching<iwBuilding>(bldType, buildingRegister.GetBuildings(bldType));
}

void iwBuildings::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
{
    switch(ctrl_id)
    {
        case ID_PlayerGroup:
            selectedPlayerId = selection - ID_PlayerButtonBase;
            UpdateBuildingIcons();
            break;
    }
}

bool iwBuildings::IsPlayerSelectionEnabled() const
{
    return gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW);
}

Extent iwBuildings::GetBuildingContentOffset() const
{
    return bldContentOffset + Extent(0, IsPlayerSelectionEnabled() ? playerSelectionHeight : 0);
}

void iwBuildings::AddPlayerSelection()
{
    if(!IsPlayerSelectionEnabled())
        return;

    const GameWorldBase& world = gwv.GetWorld();
    if(numPlayingPlayers == 0)
        return;

    const int startX = static_cast<int>(GetFullSize().x) / 2 - static_cast<int>(numPlayingPlayers - 1) * 17;
    unsigned pos = 0;
    ctrlOptionGroup* players = AddOptionGroup(ID_PlayerGroup, GroupSelectType::Illuminate);
    for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
    {
        const GamePlayer& curPlayer = world.GetPlayer(i);
        if(!curPlayer.isUsed())
            continue;

        const unsigned buttonId = ID_PlayerButtonBase + i;
        players
          ->AddImageButton(buttonId, DrawPoint(startX + pos * 34 - 17, 45 - 23), Extent(34, 47),
                           TextureColor::Green1, GetPlayerImage(curPlayer.nation), curPlayer.name)
          ->SetBorder(false);
        pos++;
    }

    if(!world.GetPlayer(selectedPlayerId).isUsed())
    {
        for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
        {
            if(world.GetPlayer(i).isUsed())
            {
                selectedPlayerId = i;
                break;
            }
        }
    }
    players->SetSelection(ID_PlayerButtonBase + selectedPlayerId);
}

void iwBuildings::DrawPlayerSelection()
{
    if(numPlayingPlayers == 0)
        return;

    DrawPoint drawPt =
      GetDrawPos() + DrawPoint(static_cast<int>(GetFullSize().x) / 2 - static_cast<int>(numPlayingPlayers) * 17, 22);
    for(unsigned i = 0; i < gwv.GetWorld().GetNumPlayers(); ++i)
    {
        const GamePlayer& player = gwv.GetWorld().GetPlayer(i);
        if(!player.isUsed())
            continue;

        if(i == selectedPlayerId)
        {
            const Rect playerBoxRect(DrawPoint(drawPt.x, drawPt.y + 47), Extent(34, 12));
            const DrawPoint playerStatusPosition =
              DrawPoint(playerBoxRect.getOrigin() + playerBoxRect.getSize() / 2 + DrawPoint(0, 1));
            DrawRectangle(playerBoxRect, player.color);
            SmallFont->Draw(playerStatusPosition, GetPlayerStatus(player), FontStyle::CENTER | FontStyle::VCENTER,
                            COLOR_YELLOW);
        }

        drawPt.x += 34;
    }
}

void iwBuildings::UpdateBuildingIcons()
{
    const Nation playerNation = gwv.GetWorld().GetPlayer(selectedPlayerId).nation;
    for(unsigned i = 0; i < bts.size(); ++i)
    {
        ctrlImageButton* button = GetCtrl<ctrlImageButton>(i);
        if(button)
            button->SetImage(LOADER.GetNationIcon(playerNation, bts[i]));
    }
}

template<class T_Window, class T_Building>
void iwBuildings::GoToFirstMatching(BuildingType bldType, const std::list<T_Building*>& blds)
{
    for(T_Building* bld : blds)
    {
        if(bld->GetBuildingType() == bldType)
        {
            gwv.MoveToMapPt(bld->GetPos());
            auto nextscrn = std::make_unique<T_Window>(gwv, gcFactory, static_cast<T_Building*>(bld));
            nextscrn->SetPos(GetPos());
            WINDOWMANAGER.ReplaceWindow(std::move(nextscrn));
            return;
        }
    }
}
