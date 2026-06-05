// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "nofFarmhand.h"
class SerializedGameData;
class nobUsual;

class nofWoodcutter : public nofFarmhand
{
private:
    /// Draws the worker while working
    void DrawWorking(DrawPoint drawPt) override;
    /// Id in jobs.bob or carrier.bob when carrying a ware
    unsigned short GetCarryID() const override;

    /// Informs the derived class when work starts (preparations)
    void WorkStarted() override;
    /// Informs the derived class when work is finished
    void WorkFinished() override;

    /// Returns the quality of this working point or determines if the worker can work here at all
    PointQuality GetPointQuality(MapPoint pt, bool isBeforeWork) const override;

    /// Called when work is aborted (called by nofBuildingWorker)
    void WorkAborted() override;

public:
    nofWoodcutter(MapPoint pos, unsigned char player, nobUsual* workplace);
    nofWoodcutter(SerializedGameData& sgd, unsigned obj_id);

    GO_Type GetGOT() const final { return GO_Type::NofWoodcutter; }
};
