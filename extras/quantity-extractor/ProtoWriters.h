// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "QuantityExtractor.h"
#include <boost/filesystem/path.hpp>

class Game;

namespace QuantityExtractor {

void WriteSnapshotFiles(const SnapshotFiles& snapshots, const boost::filesystem::path& outputDir);
void WritePlayersMetadata(const Game& game, const boost::filesystem::path& outputDir);

} // namespace QuantityExtractor
