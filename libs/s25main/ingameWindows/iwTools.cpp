// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwTools.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "WindowManager.h"
#include "addons/const_addons.h"
#include "controls/ctrlBaseText.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlOptionGroup.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTextDeepening.h"
#include "helpers/mathFuncs.h"
#include "helpers/toString.h"
#include "iwHelp.h"
#include "network/GameClient.h"
#include "notifications/NotificationManager.h"
#include "notifications/ToolNote.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameData/GoodConsts.h"
#include "gameData/ToolConsts.h"
#include "gameData/const_gui_ids.h"
#include "gameTypes/VisualSettings.h"
#include "ogl/FontStyle.h"
#include "ogl/glFont.h"
#include "s25util/colors.h"
#include <algorithm>

namespace {
enum
{
    ID_PlayerGroup = 1000,
};

constexpr unsigned ID_PlayerButtonBase = 2000;
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

unsigned CountPlayingPlayers(const GameWorldBase& world)
{
    unsigned numPlayingPlayers = 0;
    for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
    {
        if(world.GetPlayer(i).isUsed())
            ++numPlayingPlayers;
    }
    return numPlayingPlayers;
}
} // namespace

iwTools::iwTools(const GameWorldViewer& gwv, GameCommandFactory& gcFactory)
    : TransmitSettingsIgwAdapter(
      CGI_TOOLS, IngameWindow::posLastOrCenter,
      Extent(std::max(166 + (gwv.GetWorld().GetGGS().isEnabled(AddonId::TOOL_ORDERING) ? 46u : 0u),
                      gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW) ?
                        CountPlayingPlayers(gwv.GetWorld()) * 34u :
                        0u),
             432 + (gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW) ? playerSelectionHeight : 0)),
      _("Tools"),
      LOADER.GetImageN("io", 5)),
      gwv(gwv), gcFactory(gcFactory), ordersChanged(false), shouldUpdateTexts(false),
      isReplay(GAMECLIENT.IsReplayModeOn()), selectedPlayerId(gwv.GetPlayerId()),
      numPlayingPlayers(gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW) ?
                          CountPlayingPlayers(gwv.GetWorld()) :
                          0)
{
    AddPlayerSelection();

    const Extent toolsContentOffset = GetToolsContentOffset();

    // Individual bars
    for(const auto tool : helpers::enumRange<Tool>())
        AddToolSettingSlider(rttr::enum_cast(tool), TOOL_TO_GOOD[tool]);

    const GlobalGameSettings& settings = gwv.GetWorld().GetGGS();
    if(settings.isEnabled(AddonId::TOOL_ORDERING))
    {
        // qx:tools
        for(const auto tool : helpers::enumRange<Tool>())
        {
            const auto toolIdx = rttr::enum_cast(tool);
            constexpr Extent btSize(20, 13);
            auto* txt = static_cast<ctrlTextDeepening*>(
              AddTextDeepening(200 + toolIdx, DrawPoint(151, 31 + toolsContentOffset.y + toolIdx * 28),
                               Extent(22, 18), TextureColor::Grey, "", NormalFont, COLOR_YELLOW));
            txt->ResizeForMaxChars(2);
            const auto txtSize = txt->GetSize();
            ctrlButton* bt = AddImageButton(100 + toolIdx * 2, txt->GetPos() + DrawPoint(txtSize.x + 1, -4), btSize,
                                            TextureColor::Grey, LOADER.GetImageN("io", 33), "+1");
            AddImageButton(101 + toolIdx * 2, bt->GetPos() + DrawPoint(0, btSize.y), btSize, TextureColor::Grey,
                           LOADER.GetImageN("io", 34), "-1");
        }
        std::fill(pendingOrderChanges.begin(), pendingOrderChanges.end(), 0);
        UpdateTexts();
    }

    // Info
    AddImageButton(12, DrawPoint(18, 384) + toolsContentOffset, Extent(30, 32), TextureColor::Grey,
                   LOADER.GetImageN("io", 225), _("Help"));
    if(settings.isEnabled(AddonId::TOOL_ORDERING))
        AddImageButton(15, DrawPoint(130, 384) + toolsContentOffset, Extent(30, 32), TextureColor::Grey,
                       LOADER.GetImageN("io", 216), _("Zero all production"));
    // Default
    AddImageButton(13, DrawPoint(118 + (settings.isEnabled(AddonId::TOOL_ORDERING) ? 46 : 0), 384) + toolsContentOffset,
                   Extent(30, 32), TextureColor::Grey, LOADER.GetImageN("io", 191), _("Default"));

    // Set settings
    UpdateSettings();

    toolSubscription =
      gwv.GetWorld().GetNotifications().subscribe<ToolNote>([this](auto) noexcept { this->shouldUpdateTexts = true; });
}

void iwTools::AddToolSettingSlider(unsigned id, GoodType ware)
{
    const Extent toolsContentOffset = GetToolsContentOffset();
    ctrlProgress* el =
      AddProgress(id, DrawPoint(17, 25 + id * 28) + toolsContentOffset, Extent(132, 26), TextureColor::Grey,
                  140 + id * 2 + 1, 140 + id * 2, 10, _(WARE_NAMES[ware]), Extent(4, 4), 0, _("Less often"),
                  _("More often"));
    if(isReplay)
        el->ActivateControls(false);
}

void iwTools::TransmitSettings()
{
    if(isReplay || !IsSelectedPlayerLocal())
        return;
    // Were settings changed?
    settings_changed |= ordersChanged;
    if(settings_changed)
    {
        // Save settings
        ToolSettings newSettings;
        for(const auto tool : helpers::enumRange<Tool>())
            newSettings[tool] = static_cast<uint8_t>(GetCtrl<ctrlProgress>(rttr::enum_cast(tool))->GetPosition());

        int8_t* orderDelta = nullptr;
        if(ordersChanged)
        {
            orderDelta = pendingOrderChanges.data();
            const GamePlayer& localPlayer = gwv.GetPlayer();
            for(const auto tool : helpers::enumRange<Tool>())
            {
                auto curOrder = static_cast<int>(localPlayer.GetToolsOrderedVisual(tool));
                pendingOrderChanges[tool] = helpers::clamp<int>(pendingOrderChanges[tool], -curOrder, 100 - curOrder);
            }
        }

        if(gcFactory.ChangeTools(newSettings, orderDelta))
        {
            GAMECLIENT.visual_settings.tools_settings = newSettings;
            if(ordersChanged)
            {
                const GamePlayer& localPlayer = gwv.GetPlayer();
                for(const auto tool : helpers::enumRange<Tool>())
                    localPlayer.ChangeToolOrderVisual(tool, pendingOrderChanges[tool]);
                std::fill(pendingOrderChanges.begin(), pendingOrderChanges.end(), 0);
            }
            settings_changed = false;
            ordersChanged = false;
        }
    }
}

void iwTools::UpdateTexts()
{
    if(gwv.GetWorld().GetGGS().isEnabled(AddonId::TOOL_ORDERING))
    {
        const GamePlayer& selectedPlayer = gwv.GetWorld().GetPlayer(selectedPlayerId);
        for(const auto tool : helpers::enumRange<Tool>())
        {
            auto* field = GetCtrl<ctrlBaseText>(200 + rttr::enum_cast(tool));
            int curOrders = IsSelectedPlayerLocal() && !isReplay ?
                              selectedPlayer.GetToolsOrderedVisual(tool) + pendingOrderChanges[tool] :
                              selectedPlayer.GetToolsOrdered(tool);
            field->SetText(helpers::toString(curOrders));
        }
    }
}

void iwTools::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();

    if(shouldUpdateTexts)
    {
        UpdateTexts();
        shouldUpdateTexts = false;
    }
}

void iwTools::Msg_ButtonClick(const unsigned ctrl_id)
{
    if(isReplay)
        return;
    // qx:tools
    if(ctrl_id >= 100 && ctrl_id < (100 + 2 * helpers::NumEnumValues_v<Tool>))
    {
        if(!IsSelectedPlayerLocal())
            return;

        const auto tool = static_cast<Tool>((ctrl_id - 100) / 2);
        int curOrders = gwv.GetPlayer().GetToolsOrderedVisual(tool) + pendingOrderChanges[tool];

        if(ctrl_id & 0x1)
        {
            if(curOrders < 1)
                return;
            --pendingOrderChanges[tool];
            --curOrders;
        } else if(curOrders >= 99)
            return;
        else
        {
            ++pendingOrderChanges[tool];
            ++curOrders;
        }
        ordersChanged = true;
        GetCtrl<ctrlBaseText>(200 + rttr::enum_cast(tool))->SetText(helpers::toString(curOrders));
    } else
        switch(ctrl_id)
        {
            default: return;
            case 12:
                WINDOWMANAGER.ReplaceWindow(
                  std::make_unique<iwHelp>(_("These settings control the tool production of your metalworks. "
                                             "The higher the value, the more likely this tool is to be produced.")));
                break;
            case 13: // Default
                if(!IsSelectedPlayerLocal())
                    return;
                UpdateSettings(GAMECLIENT.default_settings.tools_settings);
                settings_changed = true;
                break;
            case 15: // Zero all
                if(!IsSelectedPlayerLocal())
                    return;
                ToolSettings zero{};
                UpdateSettings(zero);
                settings_changed = true;
                break;
        }
}

void iwTools::Msg_ProgressChange(const unsigned /*ctrl_id*/, const unsigned short /*position*/)
{
    if(isReplay || !IsSelectedPlayerLocal())
    {
        UpdateSettings();
        return;
    }

    // Settings were changed
    settings_changed = true;
}

void iwTools::UpdateSettings(const ToolSettings& tool_settings)
{
    for(const auto tool : helpers::enumRange<Tool>())
        GetCtrl<ctrlProgress>(rttr::enum_cast(tool))->SetPosition(tool_settings[tool]);
}

void iwTools::UpdateSettings()
{
    if(IsSelectedPlayerLocal() && !isReplay)
        UpdateSettings(GAMECLIENT.visual_settings.tools_settings);
    else
    {
        VisualSettings visualSettings{};
        gwv.GetWorld().GetPlayer(selectedPlayerId).FillVisualSettings(visualSettings);
        UpdateSettings(visualSettings.tools_settings);
    }
    UpdateTexts();
}

void iwTools::Draw_()
{
    TransmitSettingsIgwAdapter::Draw_();

    if(IsMinimized() || !IsPlayerSelectionEnabled())
        return;

    DrawPlayerSelection();
}

void iwTools::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
{
    switch(ctrl_id)
    {
        case ID_PlayerGroup:
        {
            const unsigned newSelectedPlayerId = selection - ID_PlayerButtonBase;
            if(newSelectedPlayerId == selectedPlayerId)
                return;

            if(IsSelectedPlayerLocal())
            {
                TransmitSettings();
                if(settings_changed || ordersChanged)
                {
                    GetCtrl<ctrlOptionGroup>(ID_PlayerGroup)->SetSelection(ID_PlayerButtonBase + selectedPlayerId);
                    return;
                }
            }

            selectedPlayerId = newSelectedPlayerId;
            settings_changed = false;
            ordersChanged = false;
            std::fill(pendingOrderChanges.begin(), pendingOrderChanges.end(), 0);
            UpdateSettings();
            break;
        }
    }
}

void iwTools::Msg_Timer(const unsigned ctrl_id)
{
    if(IsSelectedPlayerLocal())
        TransmitSettingsIgwAdapter::Msg_Timer(ctrl_id);
    else
        UpdateSettings();
}

bool iwTools::IsPlayerSelectionEnabled() const
{
    return gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW);
}

bool iwTools::IsSelectedPlayerLocal() const
{
    return selectedPlayerId == gwv.GetPlayerId();
}

Extent iwTools::GetToolsContentOffset() const
{
    return Extent(0, IsPlayerSelectionEnabled() ? playerSelectionHeight : 0);
}

void iwTools::AddPlayerSelection()
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

void iwTools::DrawPlayerSelection()
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
