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
#include "controls/ctrlOptionGroup.h"
#include "controls/ctrlText.h"
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
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace {
enum
{
    ID_PlayerGroup = 100,
    ID_WareGroup,
    ID_WindowValue,
    ID_ProducedValue,
    ID_DemandValue,
    ID_StockpileValue
};

constexpr unsigned ID_PlayerButtonBase = 1000;
constexpr unsigned progressAreaX = 17;
constexpr unsigned progressAreaY = 116;
constexpr unsigned progressAreaWidth = 218;
constexpr unsigned progressAreaHeight = 20;
constexpr unsigned progressValueWidth = 44;
constexpr unsigned wareButtonRow1Y = 162;
constexpr unsigned wareButtonRow2Y = 197;

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

} // namespace

iwWaresFlows::iwWaresFlows(const GameWorldViewer& gwv)
    : IngameWindow(CGI_WARES_FLOWS, IngameWindow::posLastOrCenter, Extent(252, 248), _("Wares Flows"),
                   LOADER.GetImageN("resource", 41)),
      gwv(gwv), windowValue(nullptr), producedValue(nullptr), demandValue(nullptr), stockpileValue(nullptr),
      selectedPlayerId(gwv.GetPlayerId()), selectedWareId(categories.front().id),
      currentWindowIndex(std::numeric_limits<unsigned>::max()), numPlayingPlayers(0), progressFillPercent(0),
      progressPercentageText("---")
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

    windowValue = AddText(ID_WindowValue, DrawPoint(GetSize().x / 2, progressAreaY), "", COLOR_YELLOW,
                          FontStyle::CENTER | FontStyle::BOTTOM, SmallFont);
    windowValue->setMaxWidth(progressAreaWidth);
    producedValue = AddText(ID_ProducedValue, DrawPoint(progressAreaX + progressValueWidth / 2,
                                                        progressAreaY + progressAreaHeight / 2),
                            "0", COLOR_YELLOW, FontStyle::CENTER | FontStyle::VCENTER, SmallFont);
    demandValue =
      AddText(ID_DemandValue, DrawPoint(progressAreaX + progressAreaWidth - progressValueWidth / 2,
                                        progressAreaY + progressAreaHeight / 2),
              "0", COLOR_YELLOW, FontStyle::CENTER | FontStyle::VCENTER, SmallFont);
    stockpileValue =
      AddText(ID_StockpileValue, DrawPoint(GetSize().x / 2, progressAreaY + progressAreaHeight + 4), "",
              COLOR_YELLOW, FontStyle::CENTER | FontStyle::TOP, SmallFont);
    stockpileValue->setMaxWidth(progressAreaWidth);

    ctrlOptionGroup* types = AddOptionGroup(ID_WareGroup, GroupSelectType::Illuminate);
    types->AddImageButton(1, DrawPoint(17, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Wood), _("Wood"));
    types->AddImageButton(2, DrawPoint(48, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Boards), _("Boards"));
    types->AddImageButton(3, DrawPoint(79, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Stones), _("Stones"));
    types->AddImageButton(4, DrawPoint(110, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetImageN("io", 80), _("Food"));
    types->AddImageButton(5, DrawPoint(141, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Water), _("Water"));
    types->AddImageButton(6, DrawPoint(172, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Beer), _("Beer"));
    types->AddImageButton(7, DrawPoint(203, wareButtonRow1Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Coal), _("Coal"));

    types->AddImageButton(8, DrawPoint(17, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::IronOre), _("Ironore"));
    types->AddImageButton(9, DrawPoint(48, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Gold), _("Gold"));
    types->AddImageButton(10, DrawPoint(79, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Iron), _("Iron"));
    types->AddImageButton(11, DrawPoint(110, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Coins), _("Coins"));
    types->AddImageButton(12, DrawPoint(141, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetWareTex(GoodType::Hammer), _("Tools"));
    types->AddImageButton(13, DrawPoint(172, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
                          LOADER.GetImageN("io", 111), _("Weapons"));
    types->AddImageButton(14, DrawPoint(203, wareButtonRow2Y), Extent(30, 30), TextureColor::Grey,
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
    DrawDemandProgress();
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

void iwWaresFlows::DrawDemandProgress()
{
    const DrawPoint barPos = GetDrawPos() + DrawPoint(progressAreaX + progressValueWidth, progressAreaY);
    const Extent barSize(progressAreaWidth - 2 * progressValueWidth, progressAreaHeight);
    Draw3D(Rect(barPos, barSize), TextureColor::Grey, false);

    const DrawPoint innerPadding(4, 4);
    const Extent innerSize(barSize.x - 2 * innerPadding.x, barSize.y - 2 * innerPadding.y);
    const unsigned progressWidth = innerSize.x * progressFillPercent / 100;

    unsigned color = 0xFFD70000;
    if(progressFillPercent >= 60)
        color = 0xFF71B63C;
    else if(progressFillPercent >= 30)
        color = 0xFFFFBF33;
    else if(progressFillPercent >= 20)
        color = 0xFFDB7428;

    if(progressWidth > 0)
        DrawRectangle(Rect(barPos + innerPadding, progressWidth, innerSize.y), color);

    SmallFont->Draw(barPos + DrawPoint(barSize) / 2, progressPercentageText, FontStyle::CENTER | FontStyle::VCENTER,
                    COLOR_YELLOW);
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
    const uint64_t currentDemand = SumGoods(demand.demand, category.goods);
    const uint64_t stockpile =
      SumInventoryGoods(gwv.GetWorld().GetPlayer(selectedPlayerId).GetInventory(), category.goods);
    const bool demandCalculated = IsDemandCalculated(demand, category.goods);
    if(currentWindowIndex == 0)
        windowValue->SetText(std::string(_("Window")) + ": ---");
    else
    {
        const unsigned windowStart = (currentWindowIndex - 1) * WareProductionStatsHolder::WINDOW_SIZE_GF;
        const unsigned windowEnd = currentWindowIndex * WareProductionStatsHolder::WINDOW_SIZE_GF - 1;
        windowValue->SetText(std::string(_("Window")) + ": " + std::to_string(windowStart) + "-"
                             + std::to_string(windowEnd) + " gf");
    }

    producedValue->SetText(std::to_string(produced));
    demandValue->SetText(demandCalculated ? std::to_string(currentDemand) : "-");
    stockpileValue->SetText(std::string(_("Stockpile")) + ": " + std::to_string(stockpile));

    if(!demandCalculated || currentDemand == 0)
        progressPercentageText = "---";
    else
        progressPercentageText = std::to_string(produced * 100 / currentDemand) + "%";

    if(!demandCalculated)
        progressFillPercent = 0;
    else if(currentDemand == 0)
        progressFillPercent = produced > 0 ? 100 : 0;
    else
        progressFillPercent = static_cast<unsigned>(std::min<uint64_t>(produced * 100 / currentDemand, 100));
}
