// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IngameWindow.h"

class nobMilitary;
class GameWorldView;
class GameCommandFactory;
class GlobalGameSettings;

class iwMilitaryBuilding : public IngameWindow
{
private:
    GameWorldView& gwv;
    GameCommandFactory& gcFactory;
    nobMilitary* const building;
    const bool readOnly;

public:
    iwMilitaryBuilding(GameWorldView& gwv, GameCommandFactory& gcFactory, nobMilitary* building, bool readOnly = false);
    bool IsReadOnly() const { return readOnly; }

    /// Zeigt Messagebox an, dass das Militärgebäude nicht abgerissen werden kann (Abriss-Verbot)
    static void DemolitionNotAllowed(const GlobalGameSettings& ggs);

private:
    void Draw_() override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
};
