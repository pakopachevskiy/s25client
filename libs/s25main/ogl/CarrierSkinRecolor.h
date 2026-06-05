// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

class glArchivItem_Bitmap_Player;

namespace carrierSkinRecolor {

std::unique_ptr<glArchivItem_Bitmap_Player> createAfricanCarrierSkin(const glArchivItem_Bitmap_Player& src);
bool isCarrierSkinColor(unsigned argbColor);
unsigned recolorCarrierSkin(unsigned argbColor);

} // namespace carrierSkinRecolor
