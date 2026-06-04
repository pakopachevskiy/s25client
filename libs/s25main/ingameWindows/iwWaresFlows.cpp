// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwWaresFlows.h"
#include "EventManager.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "WareDemandStatsHolder.h"
#include "WareProductionStatsHolder.h"
#include "addons/const_addons.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlMultiline.h"
#include "controls/ctrlOptionGroup.h"
#include "gameData/ShieldConsts.h"
#include "gameData/ToolConsts.h"
#include "gameData/const_gui_ids.h"
#include "gameTypes/Inventory.h"
#include "mygettext/mygettext.h"
#include "network/GameClient.h"
#include "ogl/FontStyle.h"
#include "ogl/glFont.h"
#include "s25util/colors.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include <array>
#include <limits>
#include <sstream>
#include <vector>

namespace {
enum
{
    ID_PlayerGroup = 100,
    ID_Text,
    ID_WareGroup
};

constexpr unsigned ID_PlayerButtonBase = 1000;

std::string GetPlayerStatus(const GamePlayer& player)
{
    if(player.IsDefeated())
        return "---";
    else if(player.isHuman())
        return "#" + std::to_string(player.GetPlayerId() + 1);
    else
        return _("COMP");
}

struct WareFlowCategory
{
    unsigned id;
    const char* name;
    std::vector<GoodType> goods;
};

const std::array<WareFlowCategory, 14> categories = {{
  {1, gettext_noop("Wood"), {GoodType::Wood}},
  {2, gettext_noop("Boards"), {GoodType::Boards}},
  {3, gettext_noop("Stones"), {GoodType::Stones}},
  {4, gettext_noop("Food"), {GoodType::Fish, GoodType::Bread, GoodType::Meat}},
  {5, gettext_noop("Water"), {GoodType::Water}},
  {6, gettext_noop("Beer"), {GoodType::Beer}},
  {7, gettext_noop("Coal"), {GoodType::Coal}},
  {8, gettext_noop("Iron ore"), {GoodType::IronOre}},
  {9, gettext_noop("Gold"), {GoodType::Gold}},
  {10, gettext_noop("Iron"), {GoodType::Iron}},
  {11, gettext_noop("Coins"), {GoodType::Coins}},
  {12,
   gettext_noop("Tools"),
   {GoodType::Tongs, GoodType::Hammer, GoodType::Axe, GoodType::Saw, GoodType::PickAxe, GoodType::Shovel,
    GoodType::Crucible, GoodType::RodAndLine, GoodType::Scythe, GoodType::Cleaver, GoodType::Rollingpin,
    GoodType::Bow}},
  {13, gettext_noop("Weapons"), {GoodType::Sword, GoodType::ShieldRomans}},
  {14, gettext_noop("Boats"), {GoodType::Boat}},
}};

const WareFlowCategory& GetCategory(const unsigned id)
{
    for(const WareFlowCategory& category : categories)
    {
        if(category.id == id)
            return category;
    }
    return categories.front();
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

uint64_t SumGoods(const helpers::EnumArray<uint32_t, GoodType>& values, const std::vector<GoodType>& goods)
{
    uint64_t sum = 0;
    for(const GoodType good : goods)
        sum += values[ConvertShields(good)];
    return sum;
}

uint64_t SumInventoryGoods(const Inventory& inventory, const std::vector<GoodType>& goods)
{
    uint64_t sum = 0;
    for(const GoodType good : goods)
    {
        if(good == GoodType::ShieldRomans)
        {
            sum += inventory[GoodType::ShieldRomans];
            sum += inventory[GoodType::ShieldVikings];
            sum += inventory[GoodType::ShieldAfricans];
            sum += inventory[GoodType::ShieldJapanese];
        } else
            sum += inventory[good];
    }
    return sum;
}

bool IsDemandCalculated(const WareDemandSnapshot& demand, const std::vector<GoodType>& goods)
{
    for(const GoodType good : goods)
    {
        if(demand.calculated[ConvertShields(good)])
            return true;
    }
    return false;
}

std::string TrimPlayerName(const std::string& name)
{
    constexpr size_t maxPlayerNameLength = 16;
    if(name.size() <= maxPlayerNameLength)
        return name;
    return name.substr(0, maxPlayerNameLength);
}
} // namespace

iwWaresFlows::iwWaresFlows(const GameWorldViewer& gwv)
    : IngameWindow(CGI_WARES_FLOWS, IngameWindow::posLastOrCenter, Extent(252, 304), _("Wares Flows"),
                   LOADER.GetImageN("resource", 41)),
      gwv(gwv), text(nullptr), selectedPlayerId(gwv.GetPlayerId()), selectedWareId(categories.front().id),
      currentWindowIndex(std::numeric_limits<unsigned>::max()), numPlayingPlayers(0)
{
    const GameWorldBase& world = gwv.GetWorld();

    for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
    {
        if(world.GetPlayer(i).isUsed())
            ++numPlayingPlayers;
    }

    const unsigned short startX = 126 - (numPlayingPlayers - 1) * 17;
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

        switch(GAMECLIENT.IsReplayModeOn() ? 0 : world.GetGGS().getSelection(AddonId::STATISTICS_VISIBILITY))
        {
            default: activePlayers[i] = false; break;
            case 0: activePlayers[i] = true; break;
            case 1: activePlayers[i] = gwv.GetPlayer().IsAlly(i); break;
            case 2: activePlayers[i] = gwv.GetPlayerId() == i; break;
        }

        players->GetButton(buttonId)->SetEnabled(activePlayers[i]);
        pos++;
    }

    if(!activePlayers[selectedPlayerId])
    {
        for(unsigned i = 0; i < world.GetNumPlayers(); ++i)
        {
            if(activePlayers[i])
            {
                selectedPlayerId = i;
                break;
            }
        }
    }
    players->SetSelection(ID_PlayerButtonBase + selectedPlayerId);

    text = AddMultiline(ID_Text, DrawPoint(17, 88), Extent(218, 122), TextureColor::Grey, NormalFont);
    text->SetScrollBarAllowed(false);

    ctrlOptionGroup* types = AddOptionGroup(ID_WareGroup, GroupSelectType::Illuminate);
    types->AddImageButton(1, DrawPoint(17, 218), Extent(30, 30), TextureColor::Grey, LOADER.GetWareTex(GoodType::Wood),
                          _("Wood"));
    types->AddImageButton(2, DrawPoint(48, 218), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Boards), _("Boards"));
    types->AddImageButton(3, DrawPoint(79, 218), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Stones), _("Stones"));
    types->AddImageButton(4, DrawPoint(110, 218), Extent(30, 30), TextureColor::Grey, LOADER.GetImageN("io", 80),
                          _("Food"));
    types->AddImageButton(5, DrawPoint(141, 218), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Water), _("Water"));
    types->AddImageButton(6, DrawPoint(172, 218), Extent(30, 30), TextureColor::Grey, LOADER.GetWareTex(GoodType::Beer),
                          _("Beer"));
    types->AddImageButton(7, DrawPoint(203, 218), Extent(30, 30), TextureColor::Grey, LOADER.GetWareTex(GoodType::Coal),
                          _("Coal"));

    types->AddImageButton(8, DrawPoint(17, 253), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::IronOre), _("Ironore"));
    types->AddImageButton(9, DrawPoint(48, 253), Extent(30, 30), TextureColor::Grey, LOADER.GetWareTex(GoodType::Gold),
                          _("Gold"));
    types->AddImageButton(10, DrawPoint(79, 253), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Iron), _("Iron"));
    types->AddImageButton(11, DrawPoint(110, 253), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Coins), _("Coins"));
    types->AddImageButton(12, DrawPoint(141, 253), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Hammer), _("Tools"));
    types->AddImageButton(13, DrawPoint(172, 253), Extent(30, 30), TextureColor::Grey, LOADER.GetImageN("io", 111),
                          _("Weapons"));
    types->AddImageButton(14, DrawPoint(203, 253), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Boat), _("Boats"));
    types->SetSelection(selectedWareId);

    UpdateText();
}

void iwWaresFlows::Draw_()
{
    IngameWindow::Draw_();

    if(IsMinimized())
        return;

    DrawPlayerSelection();
}

void iwWaresFlows::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
{
    switch(ctrl_id)
    {
        case ID_PlayerGroup:
            selectedPlayerId = selection - ID_PlayerButtonBase;
            UpdateText();
            break;
        case ID_WareGroup:
            selectedWareId = selection;
            UpdateText();
            break;
    }
}

void iwWaresFlows::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();

    const unsigned windowIndex = gwv.GetWorld().GetEvMgr().GetCurrentGF() / WareProductionStatsHolder::WINDOW_SIZE_GF;
    if(windowIndex != currentWindowIndex)
        UpdateText();
}

void iwWaresFlows::DrawPlayerSelection()
{
    DrawPoint drawPt = GetDrawPos() + DrawPoint(126 - numPlayingPlayers * 17, 22);
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

void iwWaresFlows::UpdateText()
{
    const unsigned currentGf = gwv.GetWorld().GetEvMgr().GetCurrentGF();
    currentWindowIndex = currentGf / WareProductionStatsHolder::WINDOW_SIZE_GF;

    const WareFlowCategory& category = GetCategory(selectedWareId);
    const WareProductionWindowStats& stats =
      WareProductionStatsHolder::GetPreviousWindowStats(static_cast<unsigned char>(selectedPlayerId), currentGf);
    const WareDemandSnapshot& demand =
      WareDemandStatsHolder::GetCurrentDemand(gwv.GetWorld(), static_cast<unsigned char>(selectedPlayerId), currentGf,
                                              GAMECLIENT.GetAIPlayer(selectedPlayerId));
    const uint64_t produced = SumGoods(stats.produced, category.goods);
    const uint64_t consumed = SumGoods(stats.consumed, category.goods);
    const uint64_t currentDemand = SumGoods(demand.demand, category.goods);
    const uint64_t stockpile =
      SumInventoryGoods(gwv.GetWorld().GetPlayer(selectedPlayerId).GetInventory(), category.goods);
    const bool demandCalculated = IsDemandCalculated(demand, category.goods);

    std::stringstream ss;
    ss << _("Player") << ": " << TrimPlayerName(gwv.GetWorld().GetPlayer(selectedPlayerId).name) << '\n';
    if(currentWindowIndex == 0)
        ss << _("Window") << ": " << _("no completed 5000-gf window") << '\n';
    else
    {
        const unsigned windowStart = (currentWindowIndex - 1) * WareProductionStatsHolder::WINDOW_SIZE_GF;
        const unsigned windowEnd = currentWindowIndex * WareProductionStatsHolder::WINDOW_SIZE_GF - 1;
        ss << _("Window") << ": " << windowStart << "-" << windowEnd << " gf\n";
    }
    ss << _("Ware") << ": " << _(category.name) << '\n';
    ss << _("Produced") << ": " << produced << '\n';
    ss << _("Consumed") << ": " << consumed << '\n';
    ss << _("Demand") << ": " << (demandCalculated ? std::to_string(currentDemand) : "-") << '\n';
    ss << _("Stockpile") << ": " << stockpile;

    const std::string content = ss.str();
    if(content == currentText)
        return;

    currentText = content;
    text->Clear();
    text->AddString(currentText, COLOR_YELLOW, false);
}
