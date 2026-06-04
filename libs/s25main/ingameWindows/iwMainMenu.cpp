// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwMainMenu.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "WindowManager.h"
#include "addons/const_addons.h"
#include "iwAIDebug.h"
#include "iwBuildOrder.h"
#include "iwBuildingProductivities.h"
#include "iwBuildings.h"
#include "iwDiplomacy.h"
#include "iwDistribution.h"
#include "iwEconomicProgress.h"
#include "iwInventory.h"
#include "iwMerchandiseStatistics.h"
#include "iwMilitary.h"
#include "iwOptionsWindow.h"
#include "iwShip.h"
#include "iwStatistics.h"
#include "iwTools.h"
#include "iwTransport.h"
#include "iwWaresFlows.h"
#include "network/GameClient.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "gameData/const_gui_ids.h"

iwMainMenu::iwMainMenu(GameWorldView& gwv, GameCommandFactory& gcFactory)
    : IngameWindow(CGI_MAINSELECTION, IngameWindow::posLastOrCenter, Extent(190, 286), _("Main selection"),
                   LOADER.GetImageN("io", 5)),
      gwv(gwv), gcFactory(gcFactory)
{
    // Distribution
    AddImageButton(0, DrawPoint(12, 22), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 134),
                   _("Distribution of goods"));
    // Transport
    AddImageButton(1, DrawPoint(68, 22), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 198),
                   _("Transport"));
    // Tool production
    AddImageButton(2, DrawPoint(124, 22), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 137), _("Tools"));

    // Statistics
    AddImageButton(3, DrawPoint(12, 70), Extent(39, 44), TextureColor::Grey, LOADER.GetImageN("io", 166),
                   _("General statistics"));
    AddImageButton(4, DrawPoint(54, 70), Extent(39, 44), TextureColor::Grey, LOADER.GetImageN("io", 135),
                   _("Merchandise statistics"));
    AddImageButton(5, DrawPoint(96, 70), Extent(39, 44), TextureColor::Grey, LOADER.GetImageN("io", 132),
                   _("Buildings"));

    // Inventory
    AddImageButton(6, DrawPoint(138, 70), Extent(39, 44), TextureColor::Grey, LOADER.GetImageN("io", 214), _("Stock"));

    // Buildings
    AddImageButton(7, DrawPoint(12, 118), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 136),
                   _("Productivity"));
    // Military
    AddImageButton(8, DrawPoint(68, 118), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 133),
                   _("Military"));
    // Ships
    AddImageButton(9, DrawPoint(124, 118), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 175),
                   _("Ship register"));

    const bool buildSequenceEnabled = gwv.GetWorld().GetGGS().isEnabled(AddonId::CUSTOM_BUILD_SEQUENCE);
    if(buildSequenceEnabled)
        AddImageButton(10, DrawPoint(12, 166), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 24),
                       _("Building sequence"));

    // Wares Flows
    AddImageButton(14, buildSequenceEnabled ? DrawPoint(12, 214) : DrawPoint(12, 166), Extent(53, 44),
                   TextureColor::Grey, LOADER.GetImageN("io", 84), _("Wares Flows"));

    // Diplomacy (todo: find a better image)
    AddImageButton(11, DrawPoint(68, 166), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 190),
                   _("Diplomacy"));

    // AI Debug
#ifdef NDEBUG
    bool enableAIDebug = gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW);
#else
    bool enableAIDebug = true;
#endif
    if(gwv.GetViewer().GetPlayer().isHost && enableAIDebug)
    {
        AddTextButton(13, DrawPoint(124, 166), Extent(53, 44), TextureColor::Grey, _("AI"), NormalFont,
                      _("AI Debug Window"));
    } else if(gwv.GetWorld().getEconHandler())
    {
        // Economy Mode
        AddImageButton(12, DrawPoint(124, 166), Extent(53, 44), TextureColor::Grey, LOADER.GetImageN("io", 196),
                       _("Economic Progress"));
    }

    if(buildSequenceEnabled)
        Resize(Extent(190, 334));

    // Options
    AddImageButton(30, buildSequenceEnabled ? DrawPoint(12, 279) : DrawPoint(12, 231), Extent(165, 32),
                   TextureColor::Grey, LOADER.GetImageN("io", 37), _("Options"));
}

/**
 *  Button click handler.
 */
void iwMainMenu::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case 0: // Distribution
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwDistribution>(gwv.GetViewer(), gcFactory));
        }
        break;
        case 1: // Transport
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwTransport>(gwv.GetViewer(), gcFactory));
        }
        break;
        case 2: // Tool production
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwTools>(gwv.GetViewer(), gcFactory));
        }
        break;
        case 3: // Statistics
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwStatistics>(gwv.GetViewer()));
        }
        break;
        case 4: // Merchandise statistics
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMerchandiseStatistics>(gwv.GetViewer().GetPlayer()));
        }
        break;
        case 5: // Building statistics
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwBuildings>(gwv, gcFactory));
        }
        break;
        case 6: // Inventory
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwInventory>(gwv.GetViewer().GetPlayer()));
        }
        break;
        case 7: // Productivity
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwBuildingProductivities>(gwv.GetViewer().GetPlayer()));
        }
        break;
        case 8: // Military
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMilitary>(gwv.GetViewer(), gcFactory));
        }
        break;
        case 9: // Ships
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwShip>(
              gwv, gcFactory, gwv.GetViewer().GetPlayer().GetShipByID(0), IngameWindow::posCenter));
        }
        break;
        case 10: // Building sequence
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwBuildOrder>(gwv.GetViewer()));
        }
        break;
        case 11: // Diplomacy
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwDiplomacy>(gwv.GetViewer(), gcFactory));
        }
        break;
        case 12: // Economic progress
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwEconomicProgress>(gwv.GetViewer()));
        }
        break;
        case 13: // AI Debug
        {
            if(auto* wnd = WINDOWMANAGER.FindNonModalWindow(CGI_AI_DEBUG))
                wnd->Close();
            else if(gwv.GetViewer().GetPlayer().isHost)
            {
                std::vector<const AIPlayer*> ais;
                for(unsigned i = 0; i < gwv.GetViewer().GetNumPlayers(); ++i)
                {
                    const AIPlayer* ai = GAMECLIENT.GetAIPlayer(i);
                    if(ai)
                        ais.push_back(ai);
                }
                WINDOWMANAGER.Show(std::make_unique<iwAIDebug>(gwv, ais));
            }
        }
        break;
        case 14: // Wares Flows
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwWaresFlows>(gwv.GetViewer()));
        }
        break;
        case 30: // Options
        {
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwOptionsWindow>(gwv.GetSoundMgr()));
        }
        break;
    }
}
