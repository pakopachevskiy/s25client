// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"
#include "gameTypes/BuildingType.h"
#include <list>
#include <vector>

class GameCommandFactory;
class GameWorldView;

/// Window listing all buildings and building sites
class iwBuildings : public IngameWindow
{
    GameWorldView& gwv;
    GameCommandFactory& gcFactory;
    unsigned selectedPlayerId;
    unsigned numPlayingPlayers;

public:
    iwBuildings(GameWorldView& gwv, GameCommandFactory& gcFactory);

private:
    /// Draw the building counts
    void Msg_PaintAfter() override;

    void Draw_() override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_OptionGroupChange(unsigned ctrl_id, unsigned selection) override;
    template<class T_Window, class T_Building>
    void GoToFirstMatching(BuildingType bldType, const std::list<T_Building*>& blds);

    void setBuildingOrder();
    bool IsPlayerSelectionEnabled() const;
    Extent GetBuildingContentOffset() const;
    void AddPlayerSelection();
    void DrawPlayerSelection();
    void UpdateBuildingIcons();
    std::vector<BuildingType> bts;
};
