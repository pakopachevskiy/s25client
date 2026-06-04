// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"
#include "gameData/MaxPlayers.h"
#include <array>
#include <string>

class ctrlMultiline;
class GameWorldViewer;

class iwWaresFlows : public IngameWindow
{
public:
    iwWaresFlows(const GameWorldViewer& gwv);

private:
    const GameWorldViewer& gwv;
    std::array<bool, MAX_PLAYERS> activePlayers{};
    ctrlMultiline* text;
    unsigned selectedPlayerId;
    unsigned selectedWareId;
    unsigned currentWindowIndex;
    unsigned numPlayingPlayers;
    std::string currentText;

    void Draw_() override;
    void Msg_OptionGroupChange(unsigned ctrl_id, unsigned selection) override;
    void Msg_PaintBefore() override;
    void DrawPlayerSelection();
    void UpdateText();
};
