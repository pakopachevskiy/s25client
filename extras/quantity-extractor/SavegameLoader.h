// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Game.h"
#include <boost/filesystem/path.hpp>
#include <memory>

namespace QuantityExtractor {

struct LoadedGame
{
    std::unique_ptr<Game> game;
    unsigned gameframe = 0;
};

LoadedGame LoadSavegame(const boost::filesystem::path& savePath);

} // namespace QuantityExtractor
