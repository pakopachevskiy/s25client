// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TransmitSettingsIgwAdapter.h"
#include "notifications/Subscription.h"
#include "gameTypes/GoodTypes.h"
#include "gameTypes/SettingsTypes.h"
#include <array>

class GameCommandFactory;
class GameWorldViewer;

/// Tool settings window
class iwTools final : public TransmitSettingsIgwAdapter
{
public:
    iwTools(const GameWorldViewer& gwv, GameCommandFactory& gcFactory);

private:
    const GameWorldViewer& gwv;
    GameCommandFactory& gcFactory;
    /// How the order for each tool should be changed (pending actual transmission)
    helpers::EnumArray<int8_t, Tool> pendingOrderChanges;
    /// Were settings changed again after the last network transmission?
    bool ordersChanged;
    bool shouldUpdateTexts;
    bool isReplay;
    unsigned selectedPlayerId;
    unsigned numPlayingPlayers;
    Subscription toolSubscription;

    void AddToolSettingSlider(unsigned id, GoodType ware);
    /// Updates the controls with the given settings
    void UpdateSettings(const ToolSettings& tool_settings);
    void UpdateSettings() override;
    /// Sends changed settings to the client if they were modified
    void TransmitSettings() override;

    void Draw_() override;
    void Msg_ButtonClick(unsigned ctrl_id) override;
    void Msg_ProgressChange(unsigned ctrl_id, unsigned short position) override;
    void Msg_OptionGroupChange(unsigned ctrl_id, unsigned selection) override;
    void Msg_Timer(unsigned ctrl_id) override;

    void UpdateTexts();
    void Msg_PaintBefore() override;
    bool IsPlayerSelectionEnabled() const;
    bool IsSelectedPlayerLocal() const;
    Extent GetToolsContentOffset() const;
    void AddPlayerSelection();
    void DrawPlayerSelection();
};
