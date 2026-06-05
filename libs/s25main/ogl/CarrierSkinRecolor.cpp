// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "CarrierSkinRecolor.h"
#include "DrawPoint.h"
#include "RttrForeachPt.h"
#include "ogl/glArchivItem_Bitmap_Player.h"
#include "libsiedler2/ColorBGRA.h"
#include "libsiedler2/enumTypes.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr std::array<unsigned, 4> ORIGINAL_SKIN_COLORS = {{0xFFDBC797, 0xFFBFA373, 0xFFA78353, 0xFFA49571}};
constexpr unsigned REFERENCE_SKIN_COLOR = 0xFFBFA373;
constexpr unsigned AFRICAN_SKIN_COLOR = 0xFF5B2F13;

unsigned red(const libsiedler2::ColorBGRA& color)
{
    return color.getRed();
}

unsigned green(const libsiedler2::ColorBGRA& color)
{
    return color.getGreen();
}

unsigned blue(const libsiedler2::ColorBGRA& color)
{
    return color.getBlue();
}

double luminance(const libsiedler2::ColorBGRA& color)
{
    return 0.2126 * red(color) + 0.7152 * green(color) + 0.0722 * blue(color);
}

uint8_t scaleChannel(unsigned channel, double factor)
{
    return static_cast<uint8_t>(std::max(0.0, std::min(255.0, std::round(channel * factor))));
}
} // namespace

namespace carrierSkinRecolor {

bool isCarrierSkinColor(const unsigned argbColor)
{
    const libsiedler2::ColorBGRA color(argbColor);
    if(color.getAlpha() == 0)
        return false;

    for(const unsigned skinColor : ORIGINAL_SKIN_COLORS)
    {
        const libsiedler2::ColorBGRA original(skinColor);
        const int redDiff = static_cast<int>(red(color)) - static_cast<int>(red(original));
        const int greenDiff = static_cast<int>(green(color)) - static_cast<int>(green(original));
        const int blueDiff = static_cast<int>(blue(color)) - static_cast<int>(blue(original));

        if(std::abs(redDiff) <= 16 && std::abs(greenDiff) <= 16 && std::abs(blueDiff) <= 16
           && redDiff * redDiff + greenDiff * greenDiff + blueDiff * blueDiff <= 24 * 24)
            return true;
    }

    return false;
}

unsigned recolorCarrierSkin(const unsigned argbColor)
{
    const libsiedler2::ColorBGRA color(argbColor);
    const libsiedler2::ColorBGRA original(REFERENCE_SKIN_COLOR);
    const libsiedler2::ColorBGRA target(AFRICAN_SKIN_COLOR);

    const double factor = luminance(color) / luminance(original);
    return libsiedler2::ColorBGRA(scaleChannel(blue(target), factor), scaleChannel(green(target), factor),
                                  scaleChannel(red(target), factor), color.getAlpha())
      .asValue();
}

std::unique_ptr<glArchivItem_Bitmap_Player> createAfricanCarrierSkin(const glArchivItem_Bitmap_Player& src)
{
    auto result = std::make_unique<glArchivItem_Bitmap_Player>(src);
    result->convertFormat(libsiedler2::TextureFormat::BGRA);

    RTTR_FOREACH_PT(Position, Extent(result->getWidth(), result->getHeight()))
    {
        if(result->isPlayerColor(pt.x, pt.y))
            continue;

        const unsigned color = result->getPixel(pt.x, pt.y).asValue();
        if(isCarrierSkinColor(color))
            result->setPixel(pt.x, pt.y, libsiedler2::ColorBGRA(recolorCarrierSkin(color)));
    }
    return result;
}

} // namespace carrierSkinRecolor
