// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwBuilding.h"
#include "BuildingWindowHelpers.h"
#include "GamePlayer.h"
#include "Loader.h"
#include "WindowManager.h"
#include "buildings/nobShipYard.h"
#include "controls/ctrlImageButton.h"
#include "controls/ctrlPercent.h"
#include "controls/ctrlText.h"
#include "factories/GameCommandFactory.h"
#include "helpers/containerUtils.h"
#include "iwDemolishBuilding.h"
#include "iwHelp.h"
#include "iwTempleBuilding.h"
#include "ogl/FontStyle.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "ogl/glFont.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldView.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/const_gui_ids.h"
#include <sstream>

/// IDs in IO_DAT for the boat and ship images used by the shipyard toggle button
const unsigned IODAT_BOAT_ID = 219;
const unsigned IODAT_SHIP_ID = 218;

iwBuilding::iwBuilding(GameWorldView& gwv, GameCommandFactory& gcFactory, nobUsual* const building, const bool readOnly,
                       Extent extent)
    : IngameWindow(CGI_BUILDING + MapBase::CreateGUIID(building->GetPos()), IngameWindow::posAtMouse, extent,
                   _(BUILDING_NAMES[building->GetBuildingType()]), LOADER.GetImageN("resource", 41)),
      gwv(gwv), gcFactory(gcFactory), building(building), readOnly(readOnly)
{
    // Worker icon
    AddImage(0, DrawPoint(28, 39), LOADER.GetMapTexture(2298));

    if(const auto job = BLD_WORK_DESC[building->GetBuildingType()].job)
        AddImage(13, DrawPoint(28, 39), LOADER.GetJobTex(*job));

    // Building icon
    AddImage(1, DrawPoint(117, 114), &building->GetBuildingImage());

    // Icon for the produced ware (if anything is produced here)
    const auto producedWare = BLD_WORK_DESC[building->GetBuildingType()].producedWare;
    if(producedWare && producedWare != GoodType::Nothing)
    {
        AddImage(2, DrawPoint(196, 39), LOADER.GetMapTexture(2298));
        AddImage(3, DrawPoint(196, 39), LOADER.GetWareTex(*producedWare));
    }

    // Info
    AddImageButton(4, DrawPoint(16, extent.y - 47), Extent(30, 32), TextureColor::Grey, LOADER.GetImageN("io", 225),
                   _("Help"));
    // Demolish
    AddImageButton(5, DrawPoint(50, extent.y - 47), Extent(34, 32), TextureColor::Grey, LOADER.GetImageN("io", 23),
                   _("Demolish house"))
      ->SetEnabled(!readOnly);
    // Toggle productivity (196, 197) (hide for lookout towers)
    auto* enable_productivity = AddImageButton(
      6, DrawPoint(90, extent.y - 47), Extent(34, 32), TextureColor::Grey,
      LOADER.GetImageN("io", ((building->IsProductionDisabledVirtual()) ? 197 : 196)), _("Production on/off"));
    if(building->GetBuildingType() == BuildingType::LookoutTower)
        enable_productivity->SetVisible(false);
    enable_productivity->SetEnabled(!readOnly);
    // Add a button to toggle between boats and ships for shipyards
    if(building->GetBuildingType() == BuildingType::Shipyard)
    {
        // Display either a boat or a ship depending on the mode
        unsigned io_dat_id =
          (static_cast<nobShipYard*>(building)->GetMode() == nobShipYard::Mode::Boats) ? IODAT_BOAT_ID : IODAT_SHIP_ID;
        AddImageButton(11, DrawPoint(130, extent.y - 47), Extent(43, 32), TextureColor::Grey,
                       LOADER.GetImageN("io", io_dat_id))
          ->SetEnabled(!readOnly);
    }

    // "Go to place"
    AddImageButton(7, DrawPoint(179, extent.y - 47), Extent(30, 32), TextureColor::Grey, LOADER.GetImageN("io", 107),
                   _("Go to place"));

    // Productivity display (hide for catapults and lookout towers)
    Window* productivity = AddPercent(9, DrawPoint(59, 31), Extent(106, 16), TextureColor::Grey, 0xFFFFFF00, SmallFont,
                                      building->GetProductivityPointer());
    if(building->GetBuildingType() == BuildingType::Catapult
       || building->GetBuildingType() == BuildingType::LookoutTower)
        productivity->SetVisible(false);

    AddText(10, DrawPoint(113, 50), _("(House unoccupied)"), COLOR_RED, FontStyle::CENTER, NormalFont);

    // "Go to next" (building of same type)
    AddImageButton(12, DrawPoint(179, extent.y - 79), Extent(30, 32), TextureColor::Grey,
                   LOADER.GetImageN("io_new", 11), _("Go to next building of same type"));
}

void iwBuilding::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();

    // Hide the unoccupied-house message if necessary
    GetCtrl<ctrlText>(10)->SetVisible(!building->HasWorker());
}

void iwBuilding::Msg_PaintAfter()
{
    IngameWindow::Msg_PaintAfter();
    const auto& bldWorkDesk = BLD_WORK_DESC[building->GetBuildingType()];
    if(BuildingProperties::IsMine(building->GetBuildingType()))
    {
        // The food display for mines looks slightly different (3x2)

        // "Black border"
        DrawRectangle(Rect(GetDrawPos() + DrawPoint(40, 60), Extent(144, 24)), 0x80000000);
        DrawPoint curPos = GetDrawPos() + DrawPoint(52, 72);
        for(unsigned char i = 0; i < bldWorkDesk.waresNeeded.size(); ++i)
        {
            for(unsigned char z = 0; z < bldWorkDesk.numSpacesPerWare; ++z)
            {
                LOADER.GetWareTex(bldWorkDesk.waresNeeded[i])
                  ->DrawFull(curPos, (z < building->GetNumWares(i) ? 0xFFFFFFFF : 0xFF404040));
                curPos.x += 24;
            }
        }
    } else
    {
        DrawPoint curPos = GetDrawPos() + DrawPoint(GetSize().x / 2, 60);
        for(unsigned char i = 0; i < bldWorkDesk.waresNeeded.size(); ++i)
        {
            const unsigned wares_count = bldWorkDesk.numSpacesPerWare;

            // "Black border"
            DrawPoint waresPos = curPos - DrawPoint(24 * wares_count / 2, 0);
            DrawRectangle(Rect(waresPos, Extent(24 * wares_count, 24)), 0x80000000);
            waresPos += DrawPoint(12, 12);

            for(unsigned char z = 0; z < wares_count; ++z)
            {
                LOADER.GetWareTex(bldWorkDesk.waresNeeded[i])
                  ->DrawFull(waresPos, (z < building->GetNumWares(i) ? COLOR_WHITE : 0xFF404040));
                waresPos.x += 24;
            }

            std::stringstream text;
            text << (unsigned)building->GetNumWares(i) << "/" << wares_count;
            NormalFont->Draw(curPos + DrawPoint(0, 12), text.str(), FontStyle::CENTER | FontStyle::VCENTER);
            curPos.y += 29;
        }
    }
}

void iwBuilding::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case 4: // Help
        {
            WINDOWMANAGER.ReplaceWindow(
              std::make_unique<iwHelp>(_(BUILDING_HELP_STRINGS[building->GetBuildingType()])));
        }
        break;
        case 5: // Burn down building
        {
            if(readOnly)
                return;
            // Demolish?
            Close();
            WINDOWMANAGER.Show(std::make_unique<iwDemolishBuilding>(gwv, building));
        }
        break;
        case 6:
        {
            if(readOnly)
                return;
            // Stop/resume production
            // Send NC
            if(gcFactory.SetProductionEnabled(building->GetPos(), building->IsProductionDisabledVirtual()))
            {
                // Update the display if successful
                building->ToggleProductionVirtual();

                // Use a different image for the button
                if(building->IsProductionDisabledVirtual())
                    GetCtrl<ctrlImageButton>(6)->SetImage(LOADER.GetImageN("io", 197));
                else
                    GetCtrl<ctrlImageButton>(6)->SetImage(LOADER.GetImageN("io", 196));

                auto* text = GetCtrl<ctrlText>(10);
                if(building->IsProductionDisabledVirtual() && building->HasWorker())
                    text->SetText(_("(House unoccupied)"));
                else if(building->HasWorker())
                    text->SetVisible(false);
            }
        }
        break;
        case 7: // "Go to place"
        {
            gwv.MoveToMapPt(building->GetPos());
        }
        break;
        case 11: // Toggle ship/boat mode for shipyards
        {
            if(readOnly)
                return;
            if(gcFactory.SetShipYardMode(building->GetPos(), static_cast<const nobShipYard*>(building)->GetMode()
                                                               == nobShipYard::Mode::Boats))
            {
                // Update the button image as well
                auto* button = GetCtrl<ctrlImageButton>(11);
                if(button->GetImage() == LOADER.GetImageN("io", IODAT_BOAT_ID))
                    button->SetImage(LOADER.GetImageN("io", IODAT_SHIP_ID));
                else
                    button->SetImage(LOADER.GetImageN("io", IODAT_BOAT_ID));
            }
        }
        break;
        case 12: // go to next of same type
        {
            const std::list<nobUsual*>& buildings = gwv.GetWorld()
                                                      .GetPlayer(building->GetPlayer())
                                                      .GetBuildingRegister()
                                                      .GetBuildings(building->GetBuildingType());
            // go through list once we get to current building -> open window for the next one and go to next location
            const auto it = building_window_helpers::GetNextBuilding(gwv, buildings, building, readOnly);
            if(it != buildings.end())
            {
                Close();
                gwv.MoveToMapPt((*it)->GetPos());
                if(building->GetBuildingType() == BuildingType::Temple)
                    WINDOWMANAGER.ReplaceWindow(std::make_unique<iwTempleBuilding>(gwv, gcFactory, *it, readOnly))
                      .SetPos(GetPos());
                else
                    WINDOWMANAGER.ReplaceWindow(std::make_unique<iwBuilding>(gwv, gcFactory, *it, readOnly))
                      .SetPos(GetPos());
                break;
            }
        }
        break;
    }
}
