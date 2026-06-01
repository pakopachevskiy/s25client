// Copyright (C) 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include <algorithm>
#include <cstddef>
#include <list>

namespace building_window_helpers {

template<typename T>
typename std::list<T*>::const_iterator GetNextBuilding(const GameWorldView& gwv, const std::list<T*>& buildings,
                                                       const T* current, bool visibleOnly)
{
    auto it = std::find(buildings.begin(), buildings.end(), current);
    if(it == buildings.end())
        return it;

    for(size_t remaining = buildings.size(); remaining > 0; --remaining)
    {
        if(++it == buildings.end())
            it = buildings.begin();
        if(!visibleOnly || gwv.GetViewer().GetVisibility((*it)->GetPos()) == Visibility::Visible)
            return it;
    }
    return buildings.end();
}

} // namespace building_window_helpers
