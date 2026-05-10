// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "nofWoodcutter.h"

#include "GamePlayer.h"
#include "Loader.h"
#include "SoundManager.h"
#include "network/GameClient.h"
#include "ogl/glArchivItem_Bitmap_Player.h"
#include "ogl/glSmartBitmap.h"
#include "world/GameWorld.h"
#include "nodeObjs/noTree.h"

nofWoodcutter::nofWoodcutter(const MapPoint pos, const unsigned char player, nobUsual* workplace)
    : nofFarmhand(Job::Woodcutter, pos, player, workplace)
{}

nofWoodcutter::nofWoodcutter(SerializedGameData& sgd, const unsigned obj_id) : nofFarmhand(sgd, obj_id) {}

/// Draws the worker while working
void nofWoodcutter::DrawWorking(DrawPoint drawPt)
{
    unsigned short nowId = GAMECLIENT.Interpolate(118, current_ev);

    if(nowId < 10)
    {
        // 1. Walk left a bit from the tree
        LOADER.getBobSprite(world->GetPlayer(player).nation, Job::Woodcutter, Direction::West, nowId % 8)
          .draw(drawPt - DrawPoint(nowId, 0), COLOR_WHITE, world->GetPlayer(player).color);
    } else if(nowId < 82)
    {
        // 2. Chop
        LOADER.GetPlayerImage("rom_bobs", 24 + (nowId - 10) % 8)
          ->DrawFull(drawPt - DrawPoint(9, 0), COLOR_WHITE, world->GetPlayer(player).color);

        if((nowId - 10) % 8 == 3)
        {
            world->GetSoundMgr().playNOSound(53, *this, nowId);
            was_sounding = true;
        }

    } else if(nowId < 105)
    {
        // 3. Wait until the tree falls
        LOADER.GetPlayerImage("rom_bobs", 24)
          ->DrawFull(drawPt - DrawPoint(9, 0), COLOR_WHITE, world->GetPlayer(player).color);

        if(nowId == 90)
        {
            world->GetSoundMgr().playNOSound(85, *this, nowId);
            was_sounding = true;
        }
    } else if(nowId < 115)
    {
        // 4. Walk back to the right
        LOADER.getBobSprite(world->GetPlayer(player).nation, Job::Woodcutter, Direction::East, (nowId - 105) % 8)
          .draw(drawPt - DrawPoint(9 - (nowId - 105), 0), COLOR_WHITE, world->GetPlayer(player).color);
    } else
    {
        // 5. Wait briefly at the tree (essentially taking the log in hand)
        LOADER.getBobSprite(world->GetPlayer(player).nation, Job::Woodcutter, Direction::East, 1)
          .draw(drawPt, COLOR_WHITE, world->GetPlayer(player).color);
    }
}

unsigned short nofWoodcutter::GetCarryID() const
{
    return 61;
}

/// Inform the derived class when it starts working (preparations)
void nofWoodcutter::WorkStarted()
{
    RTTR_Assert(world->GetSpecObj<noTree>(dest)->GetType() == NodalObjectType::Tree);

    world->GetSpecObj<noTree>(dest)->FallSoon();
}

/// Inform the derived class when it has finished working
void nofWoodcutter::WorkFinished()
{
    // Take the wood in hand
    ware = GoodType::Wood;
}

/// Returns the quality of this working point or determines if the worker can work here at all
nofFarmhand::PointQuality nofWoodcutter::GetPointQuality(const MapPoint pt, bool /* isBeforeWork */) const
{
    // Is there a tree at this position and is it fully grown?
    // Also do not chop down pineapple plants!
    const noBase* no = world->GetNO(pt);
    if(no->GetType() == NodalObjectType::Tree)
    {
        if(static_cast<const noTree*>(no)->IsFullyGrown() && static_cast<const noTree*>(no)->ProducesWood())
            return PointQuality::Class1;
    }

    return PointQuality::NotPossible;
}

void nofWoodcutter::WorkAborted()
{
    nofFarmhand::WorkAborted();
    // Notify the tree
    if(state == State::Work)
        world->GetSpecObj<noTree>(pos)->DontFall();
}
