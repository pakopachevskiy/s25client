// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"
#include <vector>

class AIPlayer;
class ctrlComboBox;
class ctrlMultiline;
class GameWorldView;
namespace AIJH {
class AIDebugView;
}

class iwAIDebug : public IngameWindow
{
public:
    iwAIDebug(GameWorldView& gwv, const std::vector<const AIPlayer*>& ais);
    ~iwAIDebug() override;

private:
    void Msg_ComboSelectItem(unsigned ctrl_id, unsigned selection) override;
    void Msg_OptionGroupChange(unsigned ctrl_id, unsigned selection) override;
    // void Msg_ButtonClick(unsigned ctrl_id);
    // void Msg_ProgressChange(unsigned ctrl_id, unsigned short position);
    void Draw_() override;
    void Msg_PaintBefore() override;
    void DrawPlayerSelection();

    class DebugPrinter;

    GameWorldView& gwv;
    std::vector<const AIJH::AIDebugView*> ais_;
    std::vector<const AIPlayer*> aiPlayers_;
    ctrlComboBox* buildingType;
    ctrlMultiline* text;
    DebugPrinter* printer;
    unsigned selectedAIIndex;
};
