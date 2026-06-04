// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dskGameInterface.h"
#include "CollisionDetection.h"
#include "EventManager.h"
#include "Game.h"
#include "GamePlayer.h"
#include "Loader.h"
#include "NWFInfo.h"
#include "Settings.h"
#include "SoundManager.h"
#include "WindowManager.h"
#include "addons/AddonMaxWaterwayLength.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobHQ.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobStorehouse.h"
#include "buildings/nobTemple.h"
#include "buildings/nobUsual.h"
#include "controls/ctrlImageButton.h"
#include "controls/ctrlText.h"
#include "driver/MouseCoords.h"
#include "drivers/VideoDriverWrapper.h"
#include "helpers/format.hpp"
#include "helpers/strUtils.h"
#include "helpers/toString.h"
#include "ingameWindows/iwAIDebug.h"
#include "ingameWindows/iwAction.h"
#include "ingameWindows/iwBaseWarehouse.h"
#include "ingameWindows/iwBuildOrder.h"
#include "ingameWindows/iwBuilding.h"
#include "ingameWindows/iwBuildingProductivities.h"
#include "ingameWindows/iwBuildingSite.h"
#include "ingameWindows/iwBuildings.h"
#include "ingameWindows/iwDiplomacy.h"
#include "ingameWindows/iwDistribution.h"
#include "ingameWindows/iwEconomicProgress.h"
#include "ingameWindows/iwEndgame.h"
#include "ingameWindows/iwHQ.h"
#include "ingameWindows/iwHarborBuilding.h"
#include "ingameWindows/iwInventory.h"
#include "ingameWindows/iwMainMenu.h"
#include "ingameWindows/iwMapDebug.h"
#include "ingameWindows/iwMerchandiseStatistics.h"
#include "ingameWindows/iwMilitary.h"
#include "ingameWindows/iwMilitaryBuilding.h"
#include "ingameWindows/iwMinimap.h"
#include "ingameWindows/iwMusicPlayer.h"
#include "ingameWindows/iwOptionsWindow.h"
#include "ingameWindows/iwPostWindow.h"
#include "ingameWindows/iwRoadWindow.h"
#include "ingameWindows/iwSave.h"
#include "ingameWindows/iwShip.h"
#include "ingameWindows/iwSkipGFs.h"
#include "ingameWindows/iwStatistics.h"
#include "ingameWindows/iwTempleBuilding.h"
#include "ingameWindows/iwTextfile.h"
#include "ingameWindows/iwTools.h"
#include "ingameWindows/iwTrade.h"
#include "ingameWindows/iwTransport.h"
#include "ingameWindows/iwVictory.h"
#include "ingameWindows/iwWaresFlows.h"
#include "lua/GameDataLoader.h"
#include "network/GameClient.h"
#include "notifications/BuildingNote.h"
#include "notifications/NotificationManager.h"
#include "ogl/FontStyle.h"
#include "ogl/SoundEffectItem.h"
#include "ogl/glArchivItem_Bitmap_Player.h"
#include "ogl/glFont.h"
#include "pathfinding/FindPathForRoad.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostMsg.h"
#include "random/Random.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noTree.h"
#include "gameData/BuildingProperties.h"
#include "gameData/GameConsts.h"
#include "gameData/GuiConsts.h"
#include "gameData/TerrainDesc.h"
#include "gameData/const_gui_ids.h"
#include "liblobby/LobbyClient.h"
#include <algorithm>
#include <cstdio>
#include <utility>

namespace {
enum
{
    ID_btMap,
    ID_btOptions,
    ID_btConstructionAid,
    ID_btPost,
    ID_txtNumMsg
};
}

dskGameInterface::dskGameInterface(std::shared_ptr<Game> game, std::shared_ptr<const NWFInfo> nwfInfo,
                                   unsigned playerIdx, bool initOGL)
    : Desktop(nullptr), game_(std::move(game)), nwfInfo_(std::move(nwfInfo)),
      worldViewer(playerIdx, const_cast<Game&>(*game_).world_),
      gwv(worldViewer, Position(0, 0), VIDEODRIVER.GetRenderSize()), cbb(*LOADER.GetPaletteN("pal5")),
      actionwindow(nullptr), roadwindow(nullptr), minimap(worldViewer), isScrolling(false), zoomLvl(ZOOM_DEFAULT_INDEX),
      cheats_(const_cast<Game&>(*game_).world_), cheatCommandTracker_(cheats_)
{
    road.mode = RoadBuildMode::Disabled;
    road.point = MapPoint(0, 0);
    road.start = MapPoint(0, 0);

    SetScale(false);

    DrawPoint barPos((GetSize().x - LOADER.GetImageN("resource", 29)->getWidth()) / 2 + 44,
                     GetSize().y - LOADER.GetImageN("resource", 29)->getHeight() + 4);

    Extent btSize = Extent(37, 32);
    AddImageButton(ID_btMap, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 50), _("Map"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btOptions, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 192), _("Main selection"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btConstructionAid, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 83),
                   _("Construction aid mode"))
      ->SetBorder(false);
    barPos.x += btSize.x;
    AddImageButton(ID_btPost, barPos, btSize, TextureColor::Green1, LOADER.GetImageN("io", 62), _("Post office"))
      ->SetBorder(false);
    barPos += DrawPoint(18, 24);

    AddText(ID_txtNumMsg, barPos, "", COLOR_YELLOW, FontStyle::CENTER | FontStyle::VCENTER, SmallFont);

    const_cast<Game&>(*game_).world_.SetGameInterface(this);

    std::fill(borders.begin(), borders.end(), (glArchivItem_Bitmap*)(nullptr));
    cbb.loadEdges(LOADER.GetArchive("resource"));
    cbb.buildBorder(VIDEODRIVER.GetRenderSize(), borders);

    InitPlayer();
    if(initOGL)
        worldViewer.InitTerrainRenderer();

    VIDEODRIVER.setTargetFramerate(SETTINGS.video.framerate); // Use requested setting for ingame
}

void dskGameInterface::InitPlayer()
{
    // Jump to players HQ if it exists
    if(worldViewer.GetPlayer().GetHQPos().isValid())
        gwv.MoveToMapPt(worldViewer.GetPlayer().GetHQPos());

    evBld = worldViewer.GetWorld().GetNotifications().subscribe<BuildingNote>([this](const auto& note) {
        if(note.player == worldViewer.GetPlayerId())
            this->OnBuildingNote(note);
    });
    PostBox& postBox = GetPostBox();
    postBox.ObserveNewMsg([this](const auto& msg, auto msgCt) { this->NewPostMessage(msg, msgCt); });
    postBox.ObserveDeletedMsg([this](auto msgCt) { this->PostMessageDeleted(msgCt); });
    UpdatePostIcon(postBox.GetNumMsgs(), true);
}

PostBox& dskGameInterface::GetPostBox()
{
    PostBox* postBox = worldViewer.GetWorld().GetPostMgr().GetPostBox(worldViewer.GetPlayerId());
    if(!postBox)
        postBox = &worldViewer.GetWorldNonConst().GetPostMgr().AddPostBox(worldViewer.GetPlayerId());
    RTTR_Assert(postBox != nullptr);
    return *postBox;
}

dskGameInterface::~dskGameInterface()
{
    for(auto& border : borders)
        deletePtr(border);
    GAMECLIENT.RemoveInterface(this);
    LOBBYCLIENT.RemoveListener(this);
}

void dskGameInterface::SetActive(bool activate)
{
    if(activate == IsActive())
        return;
    if(!activate && isScrolling)
    {
        // Stay active if scrolling and no modal window is open
        const IngameWindow* wnd = WINDOWMANAGER.GetTopMostWindow();
        if(wnd && wnd->IsModal())
            StopScrolling();
        else
            return;
    }
    Desktop::SetActive(activate);
    // Do this here to allow previous screen to keep control
    if(activate)
    {
        GAMECLIENT.SetInterface(this);
        LOBBYCLIENT.AddListener(this);
        if(!game_->IsStarted())
        {
            GAMECLIENT.OnGameStart();

            ShowPersistentWindowsAfterSwitch();
        }
    }
}

void dskGameInterface::StopScrolling()
{
    isScrolling = false;
    WINDOWMANAGER.SetCursor(road.mode == RoadBuildMode::Disabled ? Cursor::Hand : Cursor::Remove);
}

void dskGameInterface::StartScrolling(const Position& mousePos)
{
    startScrollPt = mousePos;
    isScrolling = true;
    WINDOWMANAGER.SetCursor(Cursor::Scroll);
}

void dskGameInterface::ToggleFoW()
{
    DisableFoW(!GAMECLIENT.IsReplayFOWDisabled());
}

void dskGameInterface::DisableFoW(const bool hideFOW)
{
    GAMECLIENT.SetReplayFOW(hideFOW);
    // Notify viewer and minimap to recalculate the visibility
    worldViewer.RecalcAllColors();
    minimap.UpdateAll();
}

void dskGameInterface::ShowPersistentWindowsAfterSwitch()
{
    auto& windows = SETTINGS.windows.persistentSettings;

    if(windows[CGI_CHAT].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwChat>(this));
    if(windows[CGI_POSTOFFICE].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwPostWindow>(gwv, GetPostBox()));
    if(windows[CGI_DISTRIBUTION].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwDistribution>(gwv.GetViewer(), GAMECLIENT));
    if(windows[CGI_BUILDORDER].isOpen && gwv.GetWorld().GetGGS().isEnabled(AddonId::CUSTOM_BUILD_SEQUENCE))
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildOrder>(gwv.GetViewer()));
    if(windows[CGI_TRANSPORT].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwTransport>(gwv.GetViewer(), GAMECLIENT));
    if(windows[CGI_MILITARY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMilitary>(gwv.GetViewer(), GAMECLIENT));
    if(windows[CGI_TOOLS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwTools>(gwv.GetViewer(), GAMECLIENT));
    if(windows[CGI_INVENTORY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwInventory>(gwv.GetViewer().GetPlayer()));
    if(windows[CGI_MINIMAP].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMinimap>(minimap, gwv));
    if(windows[CGI_BUILDINGS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildings>(gwv, GAMECLIENT));
    if(windows[CGI_BUILDINGSPRODUCTIVITY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwBuildingProductivities>(gwv.GetViewer().GetPlayer()));
    if(windows[CGI_MUSICPLAYER].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMusicPlayer>());
    if(windows[CGI_STATISTICS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwStatistics>(gwv.GetViewer()));
    if(windows[CGI_ECONOMICPROGRESS].isOpen && gwv.GetWorld().getEconHandler())
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwEconomicProgress>(gwv.GetViewer()));
    if(windows[CGI_DIPLOMACY].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwDiplomacy>(gwv.GetViewer(), GAMECLIENT));
    if(windows[CGI_SHIP].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(
          std::make_unique<iwShip>(gwv, GAMECLIENT, gwv.GetViewer().GetPlayer().GetShipByID(0)));
    if(windows[CGI_MERCHANDISE_STATISTICS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwMerchandiseStatistics>(gwv.GetViewer().GetPlayer()));
    if(windows[CGI_WARES_FLOWS].isOpen)
        WINDOWMANAGER.ShowAfterSwitch(std::make_unique<iwWaresFlows>(gwv.GetViewer()));
}

void dskGameInterface::Resize(const Extent& newSize)
{
    Window::Resize(newSize);

    // recreate borders
    for(auto& border : borders)
        deletePtr(border);
    cbb.buildBorder(newSize, borders);

    // move buttons
    DrawPoint barPos((newSize.x - LOADER.GetImageN("resource", 29)->getWidth()) / 2 + 44,
                     newSize.y - LOADER.GetImageN("resource", 29)->getHeight() + 4);

    auto* button = GetCtrl<ctrlButton>(ID_btMap);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btOptions);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btConstructionAid);
    button->SetPos(barPos);

    barPos.x += button->GetSize().x;
    button = GetCtrl<ctrlButton>(ID_btPost);
    button->SetPos(barPos);

    barPos += DrawPoint(18, 24);
    auto* text = GetCtrl<ctrlText>(ID_txtNumMsg);
    text->SetPos(barPos);

    gwv.Resize(newSize);
}

void dskGameInterface::Msg_ButtonClick(const unsigned ctrl_id)
{
    switch(ctrl_id)
    {
        case ID_btMap: WINDOWMANAGER.ToggleWindow(std::make_unique<iwMinimap>(minimap, gwv)); break;
        case ID_btOptions: WINDOWMANAGER.ToggleWindow(std::make_unique<iwMainMenu>(gwv, GAMECLIENT)); break;
        case ID_btConstructionAid:
            if(WINDOWMANAGER.IsDesktopActive())
                gwv.ToggleShowBQ();
            break;
        case ID_btPost:
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwPostWindow>(gwv, GetPostBox()));
            UpdatePostIcon(GetPostBox().GetNumMsgs(), false);
            break;
    }
}

void dskGameInterface::Msg_PaintBefore()
{
    Desktop::Msg_PaintBefore();

    // Run the game
    Run();

    /// Padding of the figures
    const DrawPoint figPadding(12, 12);
    const DrawPoint screenSize(VIDEODRIVER.GetRenderSize());
    // Draw borders
    borders[0]->DrawFull(DrawPoint(0, 0));                                      // top (with corners)
    borders[1]->DrawFull(DrawPoint(0, screenSize.y - figPadding.y));            // bottom (with corners)
    borders[2]->DrawFull(DrawPoint(0, figPadding.y));                           // left
    borders[3]->DrawFull(DrawPoint(screenSize.x - figPadding.x, figPadding.y)); // right

    // The figure/statues and the button bar
    glArchivItem_Bitmap& imgFigLeftTop = *LOADER.GetImageN("resource", 17);
    glArchivItem_Bitmap& imgFigRightTop = *LOADER.GetImageN("resource", 18);
    glArchivItem_Bitmap& imgFigLeftBot = *LOADER.GetImageN("resource", 19);
    glArchivItem_Bitmap& imgFigRightBot = *LOADER.GetImageN("resource", 20);
    imgFigLeftTop.DrawFull(figPadding);
    imgFigRightTop.DrawFull(DrawPoint(screenSize.x - figPadding.x - imgFigRightTop.getWidth(), figPadding.y));
    imgFigLeftBot.DrawFull(DrawPoint(figPadding.x, screenSize.y - figPadding.y - imgFigLeftBot.getHeight()));
    imgFigRightBot.DrawFull(screenSize - figPadding - imgFigRightBot.GetSize());

    glArchivItem_Bitmap& imgButtonBar = *LOADER.GetImageN("resource", 29);
    imgButtonBar.DrawFull(
      DrawPoint((screenSize.x - imgButtonBar.getWidth()) / 2, screenSize.y - imgButtonBar.getHeight()));
}

void dskGameInterface::Msg_PaintAfter()
{
    Desktop::Msg_PaintAfter();

    const GameWorldBase& world = worldViewer.GetWorld();

    if(SETTINGS.global.showGFInfo)
    {
        std::array<char, 256> nwf_string;
        if(GAMECLIENT.IsReplayModeOn())
        {
            snprintf(nwf_string.data(), nwf_string.size(),
                     _("(Replay-Mode) Current GF: %u (End at: %u) / GF length: %u ms / NWF length: %u gf (%u ms)"),
                     world.GetEvMgr().GetCurrentGF(), GAMECLIENT.GetLastReplayGF(),
                     GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1), GAMECLIENT.GetNWFLength(),
                     GAMECLIENT.GetNWFLength() * GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1));
        } else
            snprintf(nwf_string.data(), nwf_string.size(),
                     _("Current GF: %u / GF length: %u ms / NWF length: %u gf (%u ms) /  Ping: %u ms"),
                     world.GetEvMgr().GetCurrentGF(), GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1),
                     GAMECLIENT.GetNWFLength(),
                     GAMECLIENT.GetNWFLength() * GAMECLIENT.GetGFLength() / FramesInfo::milliseconds32_t(1),
                     worldViewer.GetPlayer().ping);
        NormalFont->Draw(DrawPoint(30, 1), nwf_string.data(), FontStyle{}, COLOR_YELLOW);
    }

    // tournament mode?
    const unsigned tournamentDuration = GAMECLIENT.GetTournamentModeDuration();
    if(tournamentDuration)
    {
        unsigned curGF = world.GetEvMgr().GetCurrentGF();
        std::string tournamentNotice;
        if(curGF >= tournamentDuration)
            tournamentNotice = _("Tournament finished");
        else
        {
            tournamentNotice =
              helpers::format("Tournament mode: %1% remaining", GAMECLIENT.FormatGFTime(tournamentDuration - curGF));
        }
        NormalFont->Draw(DrawPoint(VIDEODRIVER.GetRenderSize().x - 30, 1), tournamentNotice, FontStyle::AlignH::RIGHT,
                         COLOR_YELLOW);
    }

    // Display replay filename in the bottom-left corner
    if(GAMECLIENT.IsReplayModeOn())
    {
        NormalFont->Draw(DrawPoint(0, VIDEODRIVER.GetRenderSize().y), GAMECLIENT.GetReplayFilename().string(),
                         FontStyle::BOTTOM, COLOR_YELLOW);
    } else
    {
        // Display lagging players as snails
        DrawPoint snailPos(VIDEODRIVER.GetRenderSize().x - 70, 35);
        for(const NWFPlayerInfo& player : nwfInfo_->getPlayerInfos())
        {
            if(player.isLagging)
            {
                LOADER.GetPlayerImage("rttr", 0)->DrawFull(Rect(snailPos, 30, 30), COLOR_WHITE,
                                                           game_->world_.GetPlayer(player.id).color);
                snailPos.x -= 40;
            }
        }
    }

    // Show icons in the upper right corner of the game interface
    DrawPoint iconPos(VIDEODRIVER.GetRenderSize().x - 56, 32);

    // Draw cheating indicator icon (WINTER)
    if(cheats_.isCheatModeOn())
    {
        glArchivItem_Bitmap* cheatingImg = LOADER.GetImageN("io", 75);
        cheatingImg->DrawFull(iconPos);
        iconPos -= DrawPoint(cheatingImg->getWidth() + 6, 0);
    }

    // Draw speed indicator icon
    const int speedStep =
      static_cast<int>(SPEED_GF_LENGTHS[referenceSpeed] / 10ms) - static_cast<int>(GAMECLIENT.GetGFLength() / 10ms);

    if(speedStep != 0)
    {
        glArchivItem_Bitmap* runnerImg = LOADER.GetImageN("io", 164);

        runnerImg->DrawFull(iconPos);

        if(speedStep != 1)
        {
            std::string multiplier = helpers::toString(std::abs(speedStep));
            NormalFont->Draw(iconPos - runnerImg->GetOrigin() + DrawPoint(19, 6), multiplier, FontStyle::LEFT,
                             speedStep > 0 ? COLOR_YELLOW : COLOR_RED);
        }
        iconPos -= DrawPoint(runnerImg->getWidth() + 4, 0);
    }

    // Draw zoom level indicator icon
    if(gwv.GetCurrentTargetZoomFactor() != 1.f) //-V550
    {
        glArchivItem_Bitmap* magnifierImg = LOADER.GetImageN("io", 36);

        magnifierImg->DrawFull(iconPos);

        std::string zoom_percent = helpers::toString((int)(gwv.GetCurrentTargetZoomFactor() * 100)) + "%";
        NormalFont->Draw(iconPos - magnifierImg->GetOrigin() + DrawPoint(9, 7), zoom_percent, FontStyle::CENTER,
                         COLOR_YELLOW);
        iconPos -= DrawPoint(magnifierImg->getWidth() + 4, 0);
    }

    // Draw current game frame label (refreshed every 2 seconds)
    {
        const unsigned now = VIDEODRIVER.GetTickCount();
        if(now - lastGFDisplayUpdateTick_ >= 2000u)
        {
            displayedGF_ = world.GetEvMgr().GetCurrentGF();
            lastGFDisplayUpdateTick_ = now;
        }
        const std::string gfText = "GF " + helpers::toString(displayedGF_);
        NormalFont->Draw(iconPos, gfText, FontStyle::AlignH::RIGHT | FontStyle::AlignV::VCENTER, COLOR_YELLOW);
    }
}

bool dskGameInterface::Msg_LeftDown(const MouseCoords& mc)
{
    DrawPoint btOrig(VIDEODRIVER.GetRenderSize().x / 2 - LOADER.GetImageN("resource", 29)->getWidth() / 2 + 44,
                     VIDEODRIVER.GetRenderSize().y - LOADER.GetImageN("resource", 29)->getHeight() + 4);
    Extent btSize = Extent(37, 32) * 4u;
    if(IsPointInRect(mc.GetPos(), Rect(btOrig, btSize)))
        return false;

    // Start scrolling also on Ctrl + left click
    if(VIDEODRIVER.GetModKeyState().ctrl)
    {
        Msg_RightDown(mc);
        return true;
    } else if(isScrolling)
        StopScrolling();

    // Handle clicks differently depending on whether road-building mode is enabled
    if(road.mode != RoadBuildMode::Disabled)
    {
        // Convert the currently selected point to "real" map coordinates
        const MapPoint selPt = gwv.GetSelectedPt();

        if(selPt == road.point)
        {
            // Selected point is the same as the road point --> show window to stop road building
            ShowRoadWindow(mc.GetPos());
        } else
        {
            // Close old road window
            WINDOWMANAGER.Close((unsigned)CGI_ROADWINDOW);

            // Is this a valid new road point?
            if(worldViewer.IsRoadAvailable(road.mode == RoadBuildMode::Boat, selPt)
               && worldViewer.IsPlayerTerritory(selPt))
            {
                MapPoint targetPt = selPt;
                if(!BuildRoadPart(targetPt))
                    ShowRoadWindow(mc.GetPos());
            } else if(worldViewer.GetBQ(selPt) != BuildingQuality::Nothing)
            {
                // Was the already-built segment clicked?
                unsigned idOnRoad = GetIdInCurBuildRoad(selPt);
                if(idOnRoad)
                    DemolishRoad(idOnRoad);
                else
                {
                    MapPoint targetPt = selPt;
                    if(BuildRoadPart(targetPt))
                    {
                        // Did the target point remain unchanged?
                        if(selPt == targetPt)
                            GI_BuildRoad();
                    } else if(selPt == targetPt)
                        ShowRoadWindow(mc.GetPos());
                }
            }
            // Was a flag clicked that is not the road's starting point?
            else if(worldViewer.GetWorld().GetNO(selPt)->GetType() == NodalObjectType::Flag && selPt != road.start)
            {
                MapPoint targetPt = selPt;
                if(BuildRoadPart(targetPt))
                {
                    if(selPt == targetPt)
                        GI_BuildRoad();
                } else if(selPt == targetPt)
                    ShowRoadWindow(mc.GetPos());
            } else
            {
                unsigned tbr = GetIdInCurBuildRoad(selPt);
                // Was the already-built segment clicked?
                if(tbr)
                    DemolishRoad(tbr);
                else
                    ShowRoadWindow(mc.GetPos());
            }
        }
    } else
    {
        bool enable_military_buildings = false;

        iwAction::Tabs action_tabs;

        const MapPoint cSel = gwv.GetSelectedPt();

        // Is there a ship here?
        if(const noShip* ship = worldViewer.GetShip(cSel))
        {
            WINDOWMANAGER.Show(std::make_unique<iwShip>(gwv, GAMECLIENT, ship));
            return true;
        }

        // Is this one of our buildings?
        const noBase& selObj = *worldViewer.GetWorld().GetNO(cSel);
        if(selObj.GetType() == NodalObjectType::Building && worldViewer.IsOwner(cSel))
        {
            ShowBuildingWindow(cSel, false);
            return true;
        }
        // Or perhaps a building site?
        else if(selObj.GetType() == NodalObjectType::Buildingsite && worldViewer.IsOwner(cSel))
        {
            if(!WINDOWMANAGER.FindNonModalWindow(CGI_BUILDING + MapBase::CreateGUIID(cSel)))
                WINDOWMANAGER.Show(
                  std::make_unique<iwBuildingSite>(gwv, worldViewer.GetWorld().GetSpecObj<noBuildingSite>(cSel)));
            return true;
        }
        // Visible, completed AI buildings can be inspected without allowing changes.
        else if(selObj.GetType() == NodalObjectType::Building
                && worldViewer.GetVisibility(cSel) == Visibility::Visible
                && worldViewer.GetWorld().GetPlayer(static_cast<const noBuilding&>(selObj).GetPlayer()).ps
                     == PlayerState::AI)
        {
            ShowBuildingWindow(cSel, true);
            return true;
        }

        action_tabs.watch = true;
        const unsigned territoryOwner = worldViewer.GetWorld().GetNode(cSel).owner;
        action_tabs.point_rating =
          territoryOwner != 0 && worldViewer.GetWorld().GetPlayer(territoryOwner - 1).ps == PlayerState::AI;

        // Our territory
        if(worldViewer.IsOwner(cSel))
        {
            const BuildingQuality bq = worldViewer.GetBQ(cSel);
            // Can anything be built here?
            if(bq >= BuildingQuality::Mine)
            {
                action_tabs.build = true;

                // Which building can be built?
                switch(bq)
                {
                    case BuildingQuality::Mine: action_tabs.build_tabs = iwAction::BuildTab::Mine; break;
                    case BuildingQuality::Hut: action_tabs.build_tabs = iwAction::BuildTab::Hut; break;
                    case BuildingQuality::House: action_tabs.build_tabs = iwAction::BuildTab::House; break;
                    case BuildingQuality::Castle: action_tabs.build_tabs = iwAction::BuildTab::Castle; break;
                    case BuildingQuality::Harbor: action_tabs.build_tabs = iwAction::BuildTab::Harbor; break;
                    default: break;
                }

                if(!worldViewer.GetWorld().IsFlagAround(cSel))
                    action_tabs.setflag = true;

                // Check whether military buildings are nearby; if not, the player can also build
                // military buildings
                enable_military_buildings =
                  !worldViewer.GetWorld().IsMilitaryBuildingNearNode(cSel, worldViewer.GetPlayerId());
            } else if(bq == BuildingQuality::Flag)
                action_tabs.setflag = true;
            else if(selObj.GetType() == NodalObjectType::Flag)
                action_tabs.flag = true;

            if(selObj.GetType() != NodalObjectType::Flag && selObj.GetType() != NodalObjectType::Building)
            {
                // Check if there are roads
                for(const Direction dir : helpers::EnumRange<Direction>{})
                {
                    const PointRoad curRoad = worldViewer.GetVisiblePointRoad(cSel, dir);
                    if(curRoad != PointRoad::None)
                    {
                        action_tabs.cutroad = true;
                        action_tabs.upgradeRoad |= (curRoad == PointRoad::Normal);
                    }
                }
            }
        }
        // Is this an enemy military building that is NOT in the fog?
        else if(worldViewer.GetVisibility(cSel) == Visibility::Visible)
        {
            if(selObj.GetType() == NodalObjectType::Building)
            {
                const auto* building = worldViewer.GetWorld().GetSpecObj<noBuilding>(cSel); //-V807
                BuildingType bt = building->GetBuildingType();

                // Only if trade is enabled
                if(worldViewer.GetWorld().GetGGS().isEnabled(AddonId::TRADE))
                {
                    // Allied warehouse? -> Show trade window
                    if(BuildingProperties::IsWareHouse(bt) && worldViewer.GetPlayer().IsAlly(building->GetPlayer()))
                    {
                        WINDOWMANAGER.Show(std::make_unique<iwTrade>(*static_cast<const nobBaseWarehouse*>(building),
                                                                     worldViewer, GAMECLIENT));
                        return true;
                    }
                }

                // Is it a regular military building?
                if(BuildingProperties::IsMilitary(bt))
                {
                    // Then it must not be newly built!
                    if(!static_cast<const nobMilitary*>(building)->IsNewBuilt())
                        action_tabs.attack = true;
                }
                // Or an HQ or harbor?
                else if(bt == BuildingType::Headquarters || bt == BuildingType::HarborBuilding)
                    action_tabs.attack = true;
                action_tabs.sea_attack =
                  action_tabs.attack && worldViewer.GetWorld().GetGGS().isEnabled(AddonId::SEA_ATTACK);
            }
        }

        // Close the previous action window if there was one
        // Remember the current mouse position because closing the window can change it
        if(actionwindow)
            actionwindow->Close();
        VIDEODRIVER.SetMousePos(mc.GetPos());

        ShowActionWindow(action_tabs, cSel, mc.GetPos(), enable_military_buildings);
    }

    return true;
}

void dskGameInterface::ShowBuildingWindow(const MapPoint pt, const bool readOnly)
{
    bool replaceExisting = false;
    if(auto* wnd = WINDOWMANAGER.FindNonModalWindow(CGI_BUILDING + MapBase::CreateGUIID(pt)))
    {
        const auto* usualWnd = dynamic_cast<const iwBuilding*>(wnd);
        const auto* militaryWnd = dynamic_cast<const iwMilitaryBuilding*>(wnd);
        const auto* warehouseWnd = dynamic_cast<const iwBaseWarehouse*>(wnd);
        if((usualWnd && usualWnd->IsReadOnly() == readOnly)
           || (militaryWnd && militaryWnd->IsReadOnly() == readOnly)
           || (warehouseWnd && warehouseWnd->IsReadOnly() == readOnly))
        {
            WINDOWMANAGER.SetActiveWindow(*wnd);
            return;
        }
        replaceExisting = true;
    }

    const auto showWindow = [replaceExisting](auto wnd) {
        if(replaceExisting)
            WINDOWMANAGER.ReplaceWindow(std::move(wnd));
        else
            WINDOWMANAGER.Show(std::move(wnd));
    };

    GameWorldBase& world = worldViewer.GetWorldNonConst();
    const BuildingType bt = world.GetSpecObj<noBuilding>(pt)->GetBuildingType();
    if(bt == BuildingType::Headquarters)
        showWindow(std::make_unique<iwHQ>(gwv, GAMECLIENT, world.GetSpecObj<nobHQ>(pt), readOnly));
    else if(bt == BuildingType::Storehouse)
        showWindow(std::make_unique<iwBaseWarehouse>(gwv, GAMECLIENT, world.GetSpecObj<nobStorehouse>(pt), readOnly));
    else if(bt == BuildingType::HarborBuilding)
        showWindow(
          std::make_unique<iwHarborBuilding>(gwv, GAMECLIENT, world.GetSpecObj<nobHarborBuilding>(pt), readOnly));
    else if(BuildingProperties::IsMilitary(bt))
        showWindow(std::make_unique<iwMilitaryBuilding>(gwv, GAMECLIENT, world.GetSpecObj<nobMilitary>(pt), readOnly));
    else if(bt == BuildingType::Temple)
        showWindow(std::make_unique<iwTempleBuilding>(gwv, GAMECLIENT, world.GetSpecObj<nobTemple>(pt), readOnly));
    else
        showWindow(std::make_unique<iwBuilding>(gwv, GAMECLIENT, world.GetSpecObj<nobUsual>(pt), readOnly));
}

bool dskGameInterface::Msg_LeftUp(const MouseCoords&)
{
    if(isScrolling)
    {
        StopScrolling();
        return true;
    }
    return false;
}

bool dskGameInterface::Msg_MouseMove(const MouseCoords& mc)
{
    if(!isScrolling)
        return false;

    int acceleration = SETTINGS.global.smartCursor ? 2 : 3;

    if(SETTINGS.interface.invertMouse)
        acceleration = -acceleration;

    gwv.MoveBy((mc.GetPos() - startScrollPt) * acceleration);
    VIDEODRIVER.SetMousePos(startScrollPt);

    if(!SETTINGS.global.smartCursor)
        startScrollPt = mc.GetPos();
    return true;
}

bool dskGameInterface::Msg_RightDown(const MouseCoords& mc)
{
    StartScrolling(mc.pos);
    return true;
}

bool dskGameInterface::Msg_RightUp(const MouseCoords& /*mc*/) //-V524
{
    if(isScrolling)
        StopScrolling();
    return false;
}

/**
 *  Handle special key presses.
 */
bool dskGameInterface::Msg_KeyDown(const KeyEvent& ke)
{
    cheatCommandTracker_.onKeyEvent(ke);

    switch(ke.kt)
    {
        default: break;
        case KeyType::Return: // Open chat window
            WINDOWMANAGER.Show(std::make_unique<iwChat>(this));
            return true;

        case KeyType::Space: // Show building quality
            gwv.ToggleShowBQ();
            return true;

        case KeyType::Left: // Scroll left
            gwv.MoveBy({-30, 0});
            return true;
        case KeyType::Right: // Scroll right
            gwv.MoveBy({30, 0});
            return true;
        case KeyType::Up: // Scroll up
            gwv.MoveBy({0, -30});
            return true;
        case KeyType::Down: // Scroll down
            gwv.MoveBy({0, 30});
            return true;

        case KeyType::F2: // Save game
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwSave>());
            return true;
        case KeyType::F3: // Map debug window/ Multiplayer coordinates
        {
            const bool replayMode = GAMECLIENT.IsReplayModeOn();
            if(replayMode)
                DisableFoW(true);
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMapDebug>(gwv, game_->world_.IsSinglePlayer() || replayMode));
            return true;
        }
        case KeyType::F8: // Keyboard layout
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwTextfile>("keyboardlayout.txt", _("Keyboard layout")));
            return true;
        // case KeyType::F9: // Readme
        //     WINDOWMANAGER.ToggleWindow(std::make_unique<iwTextfile>("readme.txt", _("Readme!")));
        //     return true;
        case KeyType::F11: // Music player (midi files)
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMusicPlayer>());
            return true;
        case KeyType::F12: // Options window
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwOptionsWindow>(gwv.GetSoundMgr()));
            return true;
    }

    switch(ke.c)
    {
        case '+':
            if(GAMECLIENT.IsReplayModeOn() || game_->world_.IsSinglePlayer())
                GAMECLIENT.IncreaseSpeed();
            return true;
        case '-':
            if(GAMECLIENT.IsReplayModeOn() || game_->world_.IsSinglePlayer())
                GAMECLIENT.DecreaseSpeed();
            return true;

        case '1':
        case '2':
        case '3': // Switch player
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        {
            unsigned playerIdx = ke.c - '1';
            if(GAMECLIENT.IsReplayModeOn())
            {
                unsigned oldPlayerId = worldViewer.GetPlayerId();
                GAMECLIENT.ChangePlayerIngame(worldViewer.GetPlayerId(), playerIdx);
                RTTR_Assert(worldViewer.GetPlayerId() == oldPlayerId || worldViewer.GetPlayerId() == playerIdx);
            } else if(playerIdx < worldViewer.GetWorld().GetNumPlayers())
            {
                // On mutiplayer this currently asyncs, but as this is a debug feature anyway just disable it there.
                // If this should be enabled again, look into the handling/clearing of accumulated GCs
                if(game_->world_.IsSinglePlayer())
                {
                    const GamePlayer& player = worldViewer.GetWorld().GetPlayer(playerIdx);
                    if(player.ps == PlayerState::AI && player.aiInfo.type == AI::Type::Dummy)
                        GAMECLIENT.RequestSwapToPlayer(playerIdx);
                }
            }
            return true;
        }

        case 'b': // Jump back to last position
            gwv.MoveToLastPosition();
            return true;
        case 'v':
            if(game_->world_.IsSinglePlayer())
                GAMECLIENT.IncreaseSpeed();
            return true;
        case 'c': // Show building names
            gwv.ToggleShowNames();
            return true;
        case 'd': // Replay: toggle FoW
            ToggleFoW();
            return true;
        case 'h': // Jump to HQ
        {
            const GamePlayer& player = worldViewer.GetPlayer();
            // Check whether it still exists
            if(player.GetHQPos().isValid())
                gwv.MoveToMapPt(player.GetHQPos());
        }
            return true;
        case 'i': // Show inventory
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwInventory>(worldViewer.GetPlayer()));
            return true;
        case 'j': // Skip gameframes
            if(game_->world_.IsSinglePlayer() || GAMECLIENT.IsReplayModeOn())
                WINDOWMANAGER.ToggleWindow(std::make_unique<iwSkipGFs>(gwv));
            return true;
        case 'l': // Show minimap
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMinimap>(minimap, gwv));
            return true;
        case 'm': // Main menu
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwMainMenu>(gwv, GAMECLIENT));
            return true;
        case 'n': // Show Post window
            WINDOWMANAGER.ToggleWindow(std::make_unique<iwPostWindow>(gwv, GetPostBox()));
            UpdatePostIcon(GetPostBox().GetNumMsgs(), false);
            return true;
        case 'p': // Pause
            GAMECLIENT.TogglePause();
            return true;
        case 'q': // Quit game
            if(ke.alt)
                WINDOWMANAGER.ToggleWindow(std::make_unique<iwEndgame>());
            return true;
        case 's': // Show productivity
            gwv.ToggleShowProductivity();
            return true;
        case 26: // ctrl+z
            gwv.SetZoomFactor(ZOOM_FACTORS[ZOOM_DEFAULT_INDEX]);
            return true;
        case 'z': // zoom
            if(++zoomLvl >= ZOOM_FACTORS.size())
                zoomLvl = 0;

            gwv.SetZoomFactor(ZOOM_FACTORS[zoomLvl]);
            return true;
        case 'Z': // shift-z, reverse zoom
            if(zoomLvl == 0)
                zoomLvl = ZOOM_FACTORS.size() - 1;
            else
                zoomLvl--;

            gwv.SetZoomFactor(ZOOM_FACTORS[zoomLvl]);
            return true;
    }

    return false;
}

bool dskGameInterface::Msg_WheelUp(const MouseCoords&)
{
    WheelZoom(ZOOM_WHEEL_INCREMENT);
    return true;
}
bool dskGameInterface::Msg_WheelDown(const MouseCoords&)
{
    WheelZoom(-ZOOM_WHEEL_INCREMENT);
    return true;
}

void dskGameInterface::WheelZoom(float step)
{
    float new_zoom = gwv.GetCurrentTargetZoomFactor() * (1 + step);
    gwv.SetZoomFactor(new_zoom);

    // also keep track in terms of fixed defined zoom levels
    zoomLvl = ZOOM_DEFAULT_INDEX;
    for(size_t i = ZOOM_DEFAULT_INDEX; i < ZOOM_FACTORS.size(); ++i)
    {
        if(ZOOM_FACTORS[i] < new_zoom)
            zoomLvl = i;
    }

    for(size_t i = ZOOM_DEFAULT_INDEX; i-- > 0;)
    {
        if(ZOOM_FACTORS[i] > new_zoom)
            zoomLvl = i;
    }
}

void dskGameInterface::OnBuildingNote(const BuildingNote& note)
{
    switch(note.type)
    {
        case BuildingNote::Constructed:
        case BuildingNote::Destroyed:
        case BuildingNote::Lost:
            // Close the related window as the building does not exist anymore
            // In "Constructed" this means the buildingsite
            WINDOWMANAGER.Close(CGI_BUILDING + MapBase::CreateGUIID(note.pos));
            break;
        default: break;
    }
}

void dskGameInterface::Run()
{
    // Reset draw counter of the trees before drawing
    noTree::ResetDrawCounter();

    unsigned water_percent;
    // Draw mouse only if not on window
    bool drawMouse = WINDOWMANAGER.FindWindowAtPos(VIDEODRIVER.GetMousePos()) == nullptr;
    gwv.Draw(road, actionwindow != nullptr ? actionwindow->GetSelectedPt() : MapPoint::Invalid(), drawMouse,
             &water_percent);

    // Indicate that the game is paused by darkening the screen (dark semi-transparent overlay)
    if(GAMECLIENT.IsPaused())
        DrawRectangle(Rect(DrawPoint(0, 0), VIDEODRIVER.GetRenderSize()), COLOR_SHADOW);
    else
    {
        // Play ambient sounds if game is not paused
        worldViewer.GetSoundMgr().playOceanBrawling(water_percent);
        worldViewer.GetSoundMgr().playBirdSounds(noTree::QueryDrawCounter());
    }

    messenger.Draw();
}

void dskGameInterface::GI_StartRoadBuilding(const MapPoint startPt, bool waterRoad)
{
    // Do not build roads during replays
    if(GAMECLIENT.IsReplayModeOn())
        return;

    road.mode = waterRoad ? RoadBuildMode::Boat : RoadBuildMode::Normal;
    road.route.clear();
    road.start = road.point = startPt;
    WINDOWMANAGER.SetCursor(Cursor::Remove);
}

void dskGameInterface::GI_CancelRoadBuilding()
{
    if(road.mode == RoadBuildMode::Disabled)
        return;
    road.mode = RoadBuildMode::Disabled;
    worldViewer.RemoveVisualRoad(road.start, road.route);
    WINDOWMANAGER.SetCursor(isScrolling ? Cursor::Scroll : Cursor::Hand);
}

bool dskGameInterface::BuildRoadPart(MapPoint& cSel)
{
    std::vector<Direction> new_route =
      FindPathForRoad(worldViewer, road.point, cSel, road.mode == RoadBuildMode::Boat, 100);
    // Path found?
    if(new_route.empty())
        return false;

    // Test on water way length
    if(road.mode == RoadBuildMode::Boat)
    {
        unsigned char index = worldViewer.GetWorld().GetGGS().getSelection(AddonId::MAX_WATERWAY_LENGTH);

        RTTR_Assert(index < waterwayLengths.size());
        const unsigned max_length = waterwayLengths[index];

        unsigned length = road.route.size() + new_route.size();

        // max_length == 0 means unlimited length; otherwise
        // trim the path.
        if(max_length > 0)
        {
            while(length > max_length)
            {
                new_route.pop_back();
                --length;
            }
        }
    }

    // Build the path visually
    for(const auto dir : new_route)
    {
        worldViewer.SetVisiblePointRoad(road.point, dir,
                                        (road.mode == RoadBuildMode::Boat) ? PointRoad::Boat : PointRoad::Normal);
        worldViewer.RecalcBQForRoad(road.point);
        road.point = worldViewer.GetWorld().GetNeighbour(road.point, dir);
    }
    worldViewer.RecalcBQForRoad(road.point);

    // Update target point (for waterways)
    cSel = road.point;

    road.route.insert(road.route.end(), new_route.begin(), new_route.end());

    return true;
}

unsigned dskGameInterface::GetIdInCurBuildRoad(const MapPoint pt)
{
    MapPoint curPt = road.start;
    for(unsigned i = 0; i < road.route.size(); ++i)
    {
        if(curPt == pt)
            return i + 1;

        curPt = worldViewer.GetNeighbour(curPt, road.route[i]);
    }
    return 0;
}

void dskGameInterface::ShowRoadWindow(const Position& mousePos)
{
    roadwindow = &WINDOWMANAGER.Show(
      std::make_unique<iwRoadWindow>(*this, worldViewer.GetBQ(road.point) != BuildingQuality::Nothing, mousePos), true);
}

void dskGameInterface::ShowActionWindow(const iwAction::Tabs& action_tabs, MapPoint cSel, const DrawPoint& mousePos,
                                        const bool enable_military_buildings)
{
    const GameWorldBase& world = worldViewer.GetWorld();

    iwAction::Params params;

    // Are we at the water?
    if(action_tabs.setflag)
    {
        auto isWater = [](const auto& desc) { return desc.kind == TerrainKind::Water; };
        if(world.HasTerrain(cSel, isWater))
            params = iwAction::FlagType::WaterFlag;
    }

    // If there is a flag tab, determine the flag type and set the window type accordingly
    if(action_tabs.flag)
    {
        if(world.GetNO(world.GetNeighbour(cSel, Direction::NorthWest))->GetGOT() == GO_Type::NobHq)
            params = iwAction::FlagType::HQ;
        else if(world.GetNO(cSel)->GetType() == NodalObjectType::Flag)
        {
            if(world.GetSpecObj<noFlag>(cSel)->GetFlagType() == FlagType::Water)
                params = iwAction::FlagType::WaterFlag;
        }
    }

    // The attack tab needs to know the maximum number of soldiers that can be sent
    if(action_tabs.attack)
    {
        params = worldViewer.GetNumSoldiersForAttack(cSel);
    }

    actionwindow = &WINDOWMANAGER.Show(
      std::make_unique<iwAction>(*this, gwv, action_tabs, cSel, mousePos, params, enable_military_buildings), true);
}

void dskGameInterface::OnChatCommand(const std::string& cmd)
{
    cheatCommandTracker_.onChatCommand(cmd);

    if(cmd == "surrender")
        GAMECLIENT.Surrender();
    else if(cmd == "async")
        (void)RANDOM.Rand(RANDOM_CONTEXT2(0), 255);
    else if(cmd == "segfault")
    {
        char* x = nullptr;
        *x = 1; //-V522 // NOLINT
    } else if(cmd == "reload")
    {
        WorldDescription newDesc;
        GameDataLoader gdLoader(newDesc);
        if(gdLoader.Load())
        {
            const_cast<GameWorld&>(game_->world_).GetDescriptionWriteable() = newDesc;
            worldViewer.InitTerrainRenderer();
        }
    }
}

void dskGameInterface::GI_BuildRoad()
{
    if(GAMECLIENT.BuildRoad(road.start, road.mode == RoadBuildMode::Boat, road.route))
    {
        road.mode = RoadBuildMode::Disabled;
        WINDOWMANAGER.SetCursor(Cursor::Hand);
    }
}

void dskGameInterface::Msg_WindowClosed(IngameWindow& wnd)
{
    if(actionwindow == &wnd)
        actionwindow = nullptr;
    else if(roadwindow == &wnd)
        roadwindow = nullptr;
}

void dskGameInterface::GI_FlagDestroyed(const MapPoint pt)
{
    // Are we in road-building mode, and did we build a flag from here?
    if(road.mode != RoadBuildMode::Disabled && road.start == pt)
    {
        GI_CancelRoadBuilding();
    }

    // Close the action window if necessary because it also refers to this flag
    if(actionwindow)
    {
        if(actionwindow->GetSelectedPt() == pt)
            actionwindow->Close();
    }
}

void dskGameInterface::CI_PlayerLeft(const unsigned playerId)
{
    // Display information message
    std::string text =
      helpers::format(_("Player '%s' left the game!"), worldViewer.GetWorld().GetPlayer(playerId).name);
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_RED);
    // Display in-game message that the AI has joined the game
    text = helpers::format(_("Player '%s' joined the game!"), "KI");
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_GREEN);
}

void dskGameInterface::CI_GGSChanged(const GlobalGameSettings& /*ggs*/)
{
    // TODO: print what has changed
    const std::string text = helpers::format(_("Note: Game settings changed by the server%s"), "");
    messenger.AddMessage("", 0, ChatDestination::System, text);
}

void dskGameInterface::CI_Chat(const unsigned playerId, const ChatDestination cd, const std::string& msg)
{
    messenger.AddMessage(worldViewer.GetWorld().GetPlayer(playerId).name,
                         worldViewer.GetWorld().GetPlayer(playerId).color, cd, msg);
}

void dskGameInterface::CI_Async(const std::string& checksums_list)
{
    messenger.AddMessage("", 0, ChatDestination::System,
                         _("The Game is not in sync. Checksums of some players don't match."), COLOR_RED);
    messenger.AddMessage("", 0, ChatDestination::System, checksums_list, COLOR_YELLOW);
    messenger.AddMessage("", 0, ChatDestination::System, _("A auto-savegame is created..."), COLOR_RED);
}

void dskGameInterface::CI_ReplayAsync(const std::string& msg)
{
    messenger.AddMessage("", 0, ChatDestination::System, msg, COLOR_RED);
}

void dskGameInterface::CI_ReplayEndReached(const std::string& msg)
{
    messenger.AddMessage("", 0, ChatDestination::System, msg, COLOR_BLUE);
}

void dskGameInterface::CI_GamePaused()
{
    messenger.AddMessage(_("SYSTEM"), COLOR_GREY, ChatDestination::System, _("Game was paused."));
}

void dskGameInterface::CI_GameResumed()
{
    messenger.AddMessage(_("SYSTEM"), COLOR_GREY, ChatDestination::System, _("Game was resumed."));
}

void dskGameInterface::CI_Error(const ClientError ce)
{
    messenger.AddMessage("", 0, ChatDestination::System, ClientErrorToStr(ce), COLOR_RED);
    GAMECLIENT.SetPause(true);
}

/**
 *  Status: Connection lost.
 */
void dskGameInterface::LC_Status_ConnectionLost()
{
    messenger.AddMessage("", 0, ChatDestination::System, _("Lost connection to lobby!"), COLOR_RED);
}

/**
 *  (Lobby) status: Custom error
 */
void dskGameInterface::LC_Status_Error(const std::string& error)
{
    messenger.AddMessage("", 0, ChatDestination::System, error, COLOR_RED);
}

void dskGameInterface::CI_PlayersSwapped(const unsigned player1, const unsigned player2)
{
    // Display message
    std::string text = "Player '" + worldViewer.GetWorld().GetPlayer(player1).name + "' switched to player '"
                       + worldViewer.GetWorld().GetPlayer(player2).name + "'";
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_YELLOW);

    // Recalculate visibility and minimap if we are one of the two players
    const unsigned localPlayerId = worldViewer.GetPlayerId();
    if(player1 == localPlayerId || player2 == localPlayerId)
    {
        worldViewer.ChangePlayer(player1 == localPlayerId ? player2 : player1);
        // Set visual settings back to the actual ones
        GAMECLIENT.ResetVisualSettings();
        minimap.UpdateAll();
        InitPlayer();
    }
}

/**
 *  When a player has lost
 */
void dskGameInterface::GI_PlayerDefeated(const unsigned playerId)
{
    const std::string text =
      helpers::format(_("Player '%s' was defeated!"), worldViewer.GetWorld().GetPlayer(playerId).name);
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);

    /// Local player?
    if(playerId == worldViewer.GetPlayerId())
    {
        /// Recalculate visibility
        worldViewer.RecalcAllColors();
        // Update minimap
        minimap.UpdateAll();
    }
}

void dskGameInterface::GI_UpdateMinimap(const MapPoint pt)
{
    // Notify minimap
    minimap.UpdateNode(pt);
}

void dskGameInterface::GI_UpdateMapVisibility()
{
    // recalculate visibility
    worldViewer.RecalcAllColors();
    // update minimap
    minimap.UpdateAll();
}

/**
 *  Alliance treaty was concluded or canceled --> update minimap
 */
void dskGameInterface::GI_TreatyOfAllianceChanged(unsigned playerId)
{
    // Visibility can only change if team view is enabled
    if(playerId == worldViewer.GetPlayerId() && worldViewer.GetWorld().GetGGS().teamView)
    {
        /// Recalculate visibility
        worldViewer.RecalcAllColors();
        // Update minimap
        minimap.UpdateAll();
    }
}

/**
 *  Demolish path backwards from the end to start_id
 */
void dskGameInterface::DemolishRoad(const unsigned start_id)
{
    RTTR_Assert(start_id > 0);
    for(unsigned i = road.route.size(); i >= start_id; --i)
    {
        MapPoint t = road.point;
        road.point = worldViewer.GetWorld().GetNeighbour(road.point, road.route[i - 1] + 3u);
        worldViewer.SetVisiblePointRoad(road.point, road.route[i - 1], PointRoad::None);
        worldViewer.RecalcBQForRoad(t);
    }

    road.route.resize(start_id - 1);
}

/**
 *  Update the post icon with the number of messages and the pigeon
 */
void dskGameInterface::UpdatePostIcon(const unsigned postmessages_count, bool showPigeon)
{
    // Display the pigeon or not (post)
    if(postmessages_count == 0 || !showPigeon)
        GetCtrl<ctrlImageButton>(3)->SetImage(LOADER.GetImageN("io", 62));
    else
        GetCtrl<ctrlImageButton>(3)->SetImage(LOADER.GetImageN("io", 59));

    // Also update the number of post messages
    if(postmessages_count > 0)
    {
        GetCtrl<ctrlText>(ID_txtNumMsg)->SetText(std::to_string(postmessages_count));
    } else
        GetCtrl<ctrlText>(ID_txtNumMsg)->SetText("");
}

/**
 *  New post message received
 */
void dskGameInterface::NewPostMessage(const PostMsg& msg, const unsigned msgCt)
{
    UpdatePostIcon(msgCt, true);
    SoundEffect soundEffect = msg.GetSoundEffect();
    switch(soundEffect)
    {
        case SoundEffect::Pidgeon: LOADER.GetSoundN("sound", 114)->Play(100, false); break;
        case SoundEffect::Fanfare: LOADER.GetSoundN("sound", 110)->Play(100, false);
    }
}

/**
 *  A post message was deleted by the player
 */
void dskGameInterface::PostMessageDeleted(const unsigned msgCt)
{
    UpdatePostIcon(msgCt, false);
}

/**
 *  A player has won the game.
 */
void dskGameInterface::GI_Winner(const unsigned playerId)
{
    const std::string name = worldViewer.GetWorld().GetPlayer(playerId).name;
    const std::string text = (boost::format(_("Player '%s' is the winner!")) % name).str();
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);
    WINDOWMANAGER.Show(std::make_unique<iwVictory>(std::vector<std::string>(1, name)));
}

/**
 *  A team has won the game.
 */
void dskGameInterface::GI_TeamWinner(const unsigned playerMask)
{
    std::vector<std::string> winners;
    const GameWorldBase& world = worldViewer.GetWorld();
    for(unsigned i = 0; i < world.GetNumPlayers(); i++)
    {
        if(playerMask & (1 << i))
            winners.push_back(world.GetPlayer(i).name);
    }
    const std::string text =
      (boost::format(_("%1% are the winners!")) % helpers::join(winners, ", ", _(" and "))).str();
    messenger.AddMessage("", 0, ChatDestination::System, text, COLOR_ORANGE);
    WINDOWMANAGER.Show(std::make_unique<iwVictory>(winners));
}
