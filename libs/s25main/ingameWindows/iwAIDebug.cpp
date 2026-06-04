// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "iwAIDebug.h"
#include "Loader.h"
#include "ai/AIPlayer.h"
#include "ai/AIEvents.h"
#include "ai/aijh/debug/AIDebugView.h"
#include "ai/aijh/planning/Jobs.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlComboBox.h"
#include "controls/ctrlMultiline.h"
#include "controls/ctrlOptionGroup.h"
#include "GamePlayer.h"
#include "helpers/EnumArray.h"
#include "helpers/EnumRange.h"
#include "helpers/toString.h"
#include "ogl/FontStyle.h"
#include "ogl/glFont.h"
#include "world/GameWorldView.h"
#include "gameData/BuildingConsts.h"
#include "gameData/GoodConsts.h"
#include "gameData/const_gui_ids.h"
#include "s25util/colors.h"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

namespace {
enum
{
    ID_PlayerGroup,
    ID_CbOverlay,
    ID_CbBuildingType,
    ID_Text
};

constexpr unsigned ID_PlayerButtonBase = 1000;
constexpr unsigned OVERLAY_POSITION_RATING = 13;
constexpr unsigned OVERLAY_BUILDINGS_WANTED = 14;
constexpr unsigned OVERLAY_INVENTORY = 15;
constexpr unsigned OVERLAY_ROAD_WORKLOAD = 16;
constexpr unsigned BUILDINGS_WANTED_DISABLED = std::numeric_limits<unsigned>::max();

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

std::vector<std::pair<std::string, BuildingType>> GetSortedBuildingTypes()
{
    std::vector<std::pair<std::string, BuildingType>> buildingTypes;
    for(const BuildingType type : helpers::enumRange<BuildingType>())
    {
        if(BUILDING_SIZE[type] != BuildingQuality::Nothing)
            buildingTypes.emplace_back(BUILDING_NAMES[type], type);
    }
    std::sort(buildingTypes.begin(), buildingTypes.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return buildingTypes;
}

BuildingType GetBuildingTypeFromSelection(const unsigned selection)
{
    const auto buildingTypes = GetSortedBuildingTypes();
    if(selection < buildingTypes.size())
        return buildingTypes[selection].second;
    return BuildingType::Headquarters;
}

std::vector<std::pair<std::string, GoodType>> GetSortedGoodTypes()
{
    std::vector<std::pair<std::string, GoodType>> goodTypes;
    for(const GoodType good : helpers::enumRange<GoodType>())
        goodTypes.emplace_back(GOOD_NAMES_1.at(good), good);
    std::sort(goodTypes.begin(), goodTypes.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return goodTypes;
}

void SetTextIfChanged(ctrlMultiline& text, const std::string& content)
{
    if(text.GetNumLines() == 1 && text.GetLine(0) == content)
        return;

    text.Clear();
    text.AddString(content, COLOR_YELLOW);
}
}

class iwAIDebug::DebugPrinter : public IDrawNodeCallback
{
    helpers::EnumArray<ITexture*, BuildingQuality> bqImgs;
    std::array<ITexture*, 2> ticks;
    glFont& font;

public:
    DebugPrinter(const AIJH::AIDebugView* ai, unsigned overlay)
        : font(*NormalFont), ai(ai), overlay(overlay), buildingType(BuildingType::Headquarters)
    {
        // Cache images
        bqImgs[BuildingQuality::Nothing] = nullptr;
        bqImgs[BuildingQuality::Flag] = LOADER.GetMapTexture(50);
        bqImgs[BuildingQuality::Hut] = LOADER.GetMapTexture(51);
        bqImgs[BuildingQuality::House] = LOADER.GetMapTexture(52);
        bqImgs[BuildingQuality::Castle] = LOADER.GetMapTexture(53);
        bqImgs[BuildingQuality::Mine] = LOADER.GetMapTexture(54);
        bqImgs[BuildingQuality::Harbor] = LOADER.GetMapTexture(55);

        ticks[0] = LOADER.GetTextureN("io", 40);
        ticks[1] = LOADER.GetTextureN("io", 32);
    }

    const AIJH::AIDebugView* ai;
    unsigned overlay;
    BuildingType buildingType;

    void onDraw(const MapPoint& pt, const DrawPoint& curPos) override
    {
        if(overlay == 0)
            return;
        if(overlay == 1)
        {
            auto* img = bqImgs[ai->GetAINode(pt).bq];
            if(img)
                img->DrawFull(curPos);
        } else if(overlay == 2)
            ticks[ai->GetAINode(pt).reachable]->DrawFull(curPos);
        else if(overlay == 3)
            ticks[ai->GetAINode(pt).farmed]->DrawFull(curPos);
        else if(overlay < 13)
        {
            const AIResource resource = AIResource(overlay - 4);
            if(resource != AIResource::Borderland || ai->GetAINode(pt).owned)
                font.Draw(curPos, helpers::toString(ai->GetResourceValueForDebug(pt, resource)), FontStyle{},
                          COLOR_YELLOW);
        }
        else if(overlay == OVERLAY_POSITION_RATING && ai->GetAINode(pt).owned)
        {
            const std::optional<int> rating = ai->GetPointRating(buildingType, pt);
            if(rating)
                font.Draw(curPos, helpers::toString(*rating), FontStyle{}, COLOR_YELLOW);
        }
        else if(overlay == OVERLAY_ROAD_WORKLOAD)
        {
            const std::optional<unsigned> workload = ai->GetRoadWorkload(pt);
            if(workload)
                font.Draw(curPos, helpers::toString(*workload), FontStyle{}, COLOR_YELLOW);
        }
    }
};

iwAIDebug::iwAIDebug(GameWorldView& gwv, const std::vector<const AIPlayer*>& ais)
    : IngameWindow(CGI_AI_DEBUG, IngameWindow::posLastOrCenter, Extent(280, 515), _("AI Debug"),
                   LOADER.GetImageN("resource", 41)),
      gwv(gwv), buildingType(nullptr), text(nullptr), printer(nullptr), selectedAIIndex(0)
{
    for(const AIPlayer* ai : ais)
    {
        const auto* aiDebugView = dynamic_cast<const AIJH::AIDebugView*>(ai);
        if(aiDebugView)
        {
            ais_.push_back(aiDebugView);
            aiPlayers_.push_back(ai);
        }
    }
    // Wenn keine KI-Spieler, schließen
    if(ais_.empty())
    {
        Close();
        return;
    }

    const unsigned short startX = 140 - (aiPlayers_.size() - 1) * 17;
    ctrlOptionGroup* players = AddOptionGroup(ID_PlayerGroup, GroupSelectType::Illuminate);
    for(unsigned i = 0; i < aiPlayers_.size(); ++i)
    {
        const GamePlayer& player = aiPlayers_[i]->player;
        players
          ->AddImageButton(ID_PlayerButtonBase + i, DrawPoint(startX + i * 34 - 17, 30), Extent(34, 47),
                           TextureColor::Green1, GetPlayerImage(player.nation), player.name)
          ->SetBorder(false);
    }

    ctrlComboBox* overlays =
      AddComboBox(ID_CbOverlay, DrawPoint(15, 105), Extent(250, 20), TextureColor::Grey, NormalFont, 100);
    overlays->AddString("None");
    overlays->AddString("BuildingQuality");
    overlays->AddString("Reachability");
    overlays->AddString("Farmed");
    overlays->AddString("Gold");
    overlays->AddString("Ironore");
    overlays->AddString("Coal");
    overlays->AddString("Granite");
    overlays->AddString("Fish");
    overlays->AddString("Wood");
    overlays->AddString("Stones");
    overlays->AddString("Plantspace");
    overlays->AddString("Borderland");
    overlays->AddString("Position rating");
    overlays->AddString("Buildings wanted");
    overlays->AddString("Inventory");
    overlays->AddString("Road Workload");

    buildingType = AddComboBox(ID_CbBuildingType, DrawPoint(15, 135), Extent(250, 20), TextureColor::Grey, NormalFont,
                               100);
    for(const auto& type : GetSortedBuildingTypes())
        buildingType->AddString(type.first);
    buildingType->SetSelection(0);
    buildingType->SetVisible(false);

    // Show 15 lines of text and 1 empty line
    text = AddMultiline(ID_Text, DrawPoint(15, 135), Extent(250, 16 * NormalFont->getHeight()), TextureColor::Grey,
                        NormalFont, FontStyle::NO_OUTLINE);

    SetIwSize(Extent(GetIwSize().x, text->GetPos().y + text->GetSize().y));

    players->SetSelection(ID_PlayerButtonBase);
    overlays->SetSelection(0);
    printer = new DebugPrinter(ais_[0], 0);
    gwv.AddDrawNodeCallback(printer);
}

iwAIDebug::~iwAIDebug()
{
    if(printer)
    {
        gwv.RemoveDrawNodeCallback(printer);
        delete printer;
    }
}

void iwAIDebug::Draw_()
{
    IngameWindow::Draw_();

    if(IsMinimized())
        return;

    DrawPlayerSelection();
}

void iwAIDebug::Msg_ComboSelectItem(const unsigned ctrl_id, const unsigned selection)
{
    switch(ctrl_id)
    {
        case ID_CbOverlay:
            printer->overlay = selection;
            buildingType->SetVisible(selection == OVERLAY_POSITION_RATING);
            text->SetPos(DrawPoint(15, selection == OVERLAY_POSITION_RATING ? 165 : 135));
            SetIwSize(Extent(GetIwSize().x, text->GetPos().y + text->GetSize().y));
            break;
        case ID_CbBuildingType: printer->buildingType = GetBuildingTypeFromSelection(selection); break;
    }
}

void iwAIDebug::Msg_OptionGroupChange(const unsigned ctrl_id, const unsigned selection)
{
    switch(ctrl_id)
    {
        case ID_PlayerGroup:
            selectedAIIndex = selection - ID_PlayerButtonBase;
            printer->ai = ais_[selectedAIIndex];
            break;
    }
}

void iwAIDebug::DrawPlayerSelection()
{
    const unsigned short startX = 140 - (aiPlayers_.size() - 1) * 17;
    const GamePlayer& player = aiPlayers_[selectedAIIndex]->player;
    const DrawPoint drawPt = GetDrawPos() + DrawPoint(startX + selectedAIIndex * 34 - 17, 30);
    const Rect playerBoxRect(DrawPoint(drawPt.x, drawPt.y + 47), Extent(34, 12));
    const DrawPoint playerStatusPosition =
      DrawPoint(playerBoxRect.getOrigin() + playerBoxRect.getSize() / 2 + DrawPoint(0, 1));
    DrawRectangle(playerBoxRect, player.color);
    SmallFont->Draw(playerStatusPosition, GetPlayerStatus(player), FontStyle::CENTER | FontStyle::VCENTER,
                    COLOR_YELLOW);
}

void iwAIDebug::Msg_PaintBefore()
{
    IngameWindow::Msg_PaintBefore();
    std::stringstream ss;

    if(printer->overlay == OVERLAY_BUILDINGS_WANTED)
    {
        ss << "Buildings wanted:" << std::endl << std::endl;
        std::vector<std::pair<std::string, unsigned>> wantedBuildings;
        for(const BuildingType type : helpers::enumRange<BuildingType>())
        {
            if(BUILDING_SIZE[type] == BuildingQuality::Nothing)
                continue;

            const unsigned wanted = printer->ai->GetNumBuildingsWanted(type);
            if(wanted == BUILDINGS_WANTED_DISABLED)
                continue;

            wantedBuildings.emplace_back(BUILDING_NAMES[type], wanted);
        }
        std::sort(wantedBuildings.begin(), wantedBuildings.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        for(const auto& wantedBuilding : wantedBuildings)
            ss << wantedBuilding.first << ": " << wantedBuilding.second << std::endl;

        SetTextIfChanged(*text, ss.str());
        return;
    }

    if(printer->overlay == OVERLAY_INVENTORY)
    {
        const auto* aiPlayer = dynamic_cast<const AIPlayer*>(printer->ai);
        if(!aiPlayer)
        {
            SetTextIfChanged(*text, _("No inventory available"));
            return;
        }

        ss << "Inventory:" << std::endl << std::endl;
        const Inventory& inventory = aiPlayer->player.GetInventory();
        for(const auto& good : GetSortedGoodTypes())
            ss << good.first << ": " << inventory.goods[good.second] << std::endl;

        SetTextIfChanged(*text, ss.str());
        return;
    }

    const AIJH::AIJob* currentJob = printer->ai->GetCurrentJob();
    if(!currentJob)
    {
        SetTextIfChanged(*text, _("No current job"));
        return;
    }

    ss << "Jobs to do: " << printer->ai->GetNumJobs() << std::endl << std::endl;

    const auto* bj = dynamic_cast<const AIJH::BuildJob*>(currentJob);
    const auto* ej = dynamic_cast<const AIJH::EventJob*>(currentJob);

    if(bj)
    {
        ss << "BuildJob:" << std::endl;
        ss << BUILDING_NAMES[bj->GetType()] << std::endl;
        ss << bj->GetTarget().x << " / " << bj->GetTarget().y << std::endl;
    } else if(ej)
    {
#define RTTR_PRINT_EV(ev) \
    case AIEvent::EventType::ev: ss << #ev << std::endl; break
        switch(ej->GetEvent().GetType())
        {
            RTTR_PRINT_EV(BuildingDestroyed);
            RTTR_PRINT_EV(BuildingConquered);
            RTTR_PRINT_EV(BuildingLost);
            RTTR_PRINT_EV(BorderChanged);
            RTTR_PRINT_EV(NoMoreResourcesReachable);
            RTTR_PRINT_EV(BuildingFinished);
            RTTR_PRINT_EV(ExpeditionWaiting);
            RTTR_PRINT_EV(TreeChopped);
            RTTR_PRINT_EV(ShipBuilt);
            RTTR_PRINT_EV(ResourceUsed);
            RTTR_PRINT_EV(RoadConstructionComplete);
            RTTR_PRINT_EV(RoadConstructionFailed);
            RTTR_PRINT_EV(NewColonyFounded);
            RTTR_PRINT_EV(LuaConstructionOrder);
            RTTR_PRINT_EV(ResourceFound);
            RTTR_PRINT_EV(LostLand);
            default: ss << "Unknown Event" << std::endl; break;
        }
#undef RTTR_PRINT_EV

        const auto* evb = dynamic_cast<const AIEvent::Building*>(&ej->GetEvent());
        if(evb)
        {
            ss << evb->GetX() << " / " << evb->GetY() << std::endl;
            ss << BUILDING_NAMES[evb->GetBuildingType()] << std::endl;
        }
    }

#define RTTR_PRINT_STATUS(state) \
    case AIJH::state: ss << #state << std::endl; break
    switch(currentJob->GetState())
    {
        RTTR_PRINT_STATUS(JobState::Waiting);
        RTTR_PRINT_STATUS(JobState::Start);
        RTTR_PRINT_STATUS(JobState::ExecutingRoad1);
        RTTR_PRINT_STATUS(JobState::ExecutingRoad2);
        RTTR_PRINT_STATUS(JobState::ExecutingRoad2_2);
        RTTR_PRINT_STATUS(JobState::Finished);
        RTTR_PRINT_STATUS(JobState::Failed);
        default: ss << "Unknown status"; break;
    }

    SetTextIfChanged(*text, ss.str());
}
