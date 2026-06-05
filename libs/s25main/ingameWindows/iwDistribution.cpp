// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwDistribution.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "WindowManager.h"
#include "WineLoader.h"
#include "addons/const_addons.h"
#include "controls/ctrlOptionGroup.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlProgress.h"
#include "controls/ctrlTab.h"
#include "iwHelp.h"
#include "network/GameClient.h"
#include "ogl/FontStyle.h"
#include "ogl/glFont.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameData/BuildingConsts.h"
#include "gameData/GoodConsts.h"
#include "gameData/const_gui_ids.h"
#include "gameTypes/VisualSettings.h"
#include <utility>

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

/// Determines width of the progress bars: distance to the window borders
const unsigned PROGRESS_BORDER_DISTANCE = 20;

iwDistribution::iwDistribution(const GameWorldViewer& gwv, GameCommandFactory& gcFactory)
    : TransmitSettingsIgwAdapter(CGI_DISTRIBUTION, IngameWindow::posLastOrCenter,
                                 Extent(290, 312 + (gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW) ?
                                                      playerSelectionHeight :
                                                      0)),
                                 _("Distribution of goods"), LOADER.GetImageN("resource", 41)),
      gwv(gwv), gcFactory(gcFactory), selectedPlayerId(gwv.GetPlayerId()), numPlayingPlayers(0)
{
    CreateGroups();

    if(IsPlayerSelectionEnabled())
    {
        const GameWorldBase& world = gwv.GetWorld();
        for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
        {
            if(world.GetPlayer(i).isUsed())
                ++numPlayingPlayers;
        }
    }

    AddPlayerSelection();

    const Extent distributionContentOffset = GetDistributionContentOffset();

    // Tab Control
    ctrlTab* tab = AddTabCtrl(0, DrawPoint(10, 20) + distributionContentOffset, 270);
    DrawPoint txtPos(GetSize().x / 2, 60);
    DrawPoint progPos(PROGRESS_BORDER_DISTANCE - tab->GetPos().x, txtPos.y);
    const Extent progSize(GetSize().x - 2 * PROGRESS_BORDER_DISTANCE, 20);

    for(unsigned groupId = 0; groupId < groups.size(); groupId++)
    {
        const DistributionGroup& group = groups[groupId];
        ctrlGroup* tabGrp = tab->AddTab(group.img, group.name, groupId);
        txtPos.y = progPos.y = 60;
        unsigned curId = 0;
        for(const auto& entry : group.entries)
        {
            unsigned txtId = group.entries.size() + curId;
            tabGrp->AddText(txtId, txtPos, std::get<0>(entry), COLOR_YELLOW, FontStyle::CENTER | FontStyle::BOTTOM,
                            SmallFont);
            tabGrp->AddProgress(curId++, progPos, progSize, TextureColor::Grey, 139, 138, 10);
            txtPos.y = progPos.y += progSize.y * 2;
        }
    }

    // Select group
    tab->SetSelection(0);

    const Extent btSize(32, 32);
    // "Help" button
    AddImageButton(2, DrawPoint(15, GetFullSize().y - 15 - btSize.y), btSize, TextureColor::Grey,
                   LOADER.GetImageN("io", 225), _("Help"));
    // "Default" button
    AddImageButton(10, GetFullSize() - DrawPoint::all(15) - btSize, btSize, TextureColor::Grey,
                   LOADER.GetImageN("io", 191), _("Default"));

    iwDistribution::UpdateSettings();
}

void iwDistribution::TransmitSettings()
{
    if(GAMECLIENT.IsReplayModeOn() || !IsSelectedPlayerLocal())
        return;
    if(settings_changed)
    {
        // Read values from the progress ctrls to the struct
        Distributions newDistribution{0};

        for(unsigned i = 0; i < groups.size(); ++i)
        {
            ctrlGroup* tab = GetCtrl<ctrlTab>(0)->GetGroup(i);
            const DistributionGroup& group = groups[i];
            // Read group values
            for(unsigned j = 0; j < group.entries.size(); ++j)
            {
                auto value = static_cast<uint8_t>(tab->GetCtrl<ctrlProgress>(j)->GetPosition());
                newDistribution[std::get<1>(group.entries[j])] = value;
            }
        }

        // And transmit them
        if(gcFactory.ChangeDistribution(newDistribution))
        {
            GAMECLIENT.visual_settings.distribution = newDistribution;
            settings_changed = false;
        }
    }
}

void iwDistribution::Msg_Group_ProgressChange(const unsigned /*group_id*/, const unsigned /*ctrl_id*/,
                                              const unsigned short /*position*/)
{
    if(GAMECLIENT.IsReplayModeOn() || !IsSelectedPlayerLocal())
    {
        UpdateSettings();
        return;
    }
    settings_changed = true;
}

void iwDistribution::UpdateSettings(const Distributions& distribution)
{
    for(unsigned g = 0; g < groups.size(); ++g)
    {
        // Look for correct group
        const DistributionGroup& group = groups[g];
        ctrlGroup* tab = GetCtrl<ctrlTab>(0)->GetGroup(g);
        // And correct entry
        for(unsigned i = 0; i < group.entries.size(); ++i)
            tab->GetCtrl<ctrlProgress>(i)->SetPosition(distribution[std::get<1>(group.entries[i])]);
    }
}

void iwDistribution::UpdateSettings()
{
    if(IsSelectedPlayerLocal() && !GAMECLIENT.IsReplayModeOn())
        UpdateSettings(GAMECLIENT.visual_settings.distribution);
    else
    {
        VisualSettings visualSettings{};
        gwv.GetWorld().GetPlayer(selectedPlayerId).FillVisualSettings(visualSettings);
        UpdateSettings(visualSettings.distribution);
    }
}

void iwDistribution::Draw_()
{
    TransmitSettingsIgwAdapter::Draw_();

    if(IsMinimized() || !IsPlayerSelectionEnabled())
        return;

    DrawPlayerSelection();
}

void iwDistribution::Msg_ButtonClick(const unsigned ctrl_id)
{
    if(GAMECLIENT.IsReplayModeOn())
        return;
    switch(ctrl_id)
    {
        default: return;

        case 2:
        {
            WINDOWMANAGER.ReplaceWindow(
              std::make_unique<iwHelp>(_("The priority of goods for the individual buildings can be set here. "
                                         "The higher the value, the quicker the required goods are delivered "
                                         "to the building concerned.")));
        }
        break;
        // Default button
        case 10:
        {
            if(!IsSelectedPlayerLocal())
                return;

            UpdateSettings(GAMECLIENT.default_settings.distribution);
            settings_changed = true;
        }
        break;
    }
}

void iwDistribution::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
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
                if(settings_changed)
                {
                    GetCtrl<ctrlOptionGroup>(ID_PlayerGroup)->SetSelection(ID_PlayerButtonBase + selectedPlayerId);
                    return;
                }
            }

            selectedPlayerId = newSelectedPlayerId;
            settings_changed = false;
            UpdateSettings();
            break;
        }
    }
}

void iwDistribution::Msg_Timer(const unsigned ctrl_id)
{
    if(IsSelectedPlayerLocal())
        TransmitSettingsIgwAdapter::Msg_Timer(ctrl_id);
    else
        UpdateSettings();
}

void iwDistribution::CreateGroups()
{
    if(!groups.empty())
        return;

    GoodType lastGood = GoodType::Nothing;
    unsigned pos = 0;
    for(const DistributionMapping& mapping : distributionMap)
    {
        // New group?
        if(lastGood != std::get<0>(mapping))
        {
            lastGood = std::get<0>(mapping);
            // Fish = all foodstuff
            std::string name = lastGood == GoodType::Fish ? gettext_noop("Foodstuff") : WARE_NAMES[lastGood];
            glArchivItem_Bitmap* img = nullptr;
            switch(lastGood)
            {
                case GoodType::Fish: img = LOADER.GetImageN("io", 80); break;
                case GoodType::Grain: img = LOADER.GetImageN("io", 90); break;
                case GoodType::Iron: img = LOADER.GetImageN("io", 81); break;
                case GoodType::Coal: img = LOADER.GetImageN("io", 91); break;
                case GoodType::Wood: img = LOADER.GetImageN("io", 89); break;
                case GoodType::Boards: img = LOADER.GetImageN("io", 82); break;
                case GoodType::Water: img = LOADER.GetImageN("io", 92); break;
                default: break;
            }
            if(!img)
                throw std::runtime_error("Unexpected good in distribution");
            groups.push_back(DistributionGroup(_(name), img));
        }
        // HQ = Construction
        std::string name = std::get<1>(mapping) == BuildingType::Headquarters ? gettext_noop("Construction") :
                                                                                BUILDING_NAMES[std::get<1>(mapping)];
        groups.back().entries.push_back(std::tuple(_(name), pos));
        pos++;
    }

    auto isUnused = [&](std::tuple<std::string, unsigned> const& bts) {
        const BuildingType buildingType = std::get<1>(distributionMap[std::get<1>(bts)]);
        if(!wineaddon::isAddonActive(gwv.GetWorld()) && wineaddon::isWineAddonBuildingType(buildingType))
            return true;
        if(!gwv.GetWorld().GetGGS().isEnabled(AddonId::CHARBURNER) && buildingType == BuildingType::Charburner)
            return true;
        return false;
    };
    for(auto& group : groups)
        helpers::erase_if(group.entries, isUnused);
}

bool iwDistribution::IsPlayerSelectionEnabled() const
{
    return gwv.GetWorld().GetGGS().isEnabled(AddonId::AI_DEBUG_WINDOW);
}

bool iwDistribution::IsSelectedPlayerLocal() const
{
    return selectedPlayerId == gwv.GetPlayerId();
}

Extent iwDistribution::GetDistributionContentOffset() const
{
    return Extent(0, IsPlayerSelectionEnabled() ? playerSelectionHeight : 0);
}

void iwDistribution::AddPlayerSelection()
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

void iwDistribution::DrawPlayerSelection()
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
