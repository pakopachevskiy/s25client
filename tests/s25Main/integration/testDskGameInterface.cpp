// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GamePlayer.h"
#include "Loader.h"
#include "PointOutput.h"
#include "Settings.h"
#include "WindowManager.h"
#include "buildings/nobBaseWarehouse.h"
#include "controls/ctrlButton.h"
#include "controls/ctrlGroup.h"
#include "controls/ctrlOptionGroup.h"
#include "desktops/dskGameInterface.h"
#include "driver/KeyEvent.h"
#include "driver/MouseCoords.h"
#include "factories/BuildingFactory.h"
#include "ingameWindows/iwBaseWarehouse.h"
#include "ingameWindows/iwBuilding.h"
#include "ingameWindows/iwHQ.h"
#include "ingameWindows/iwHarborBuilding.h"
#include "ingameWindows/iwMilitaryBuilding.h"
#include "ingameWindows/iwTempleBuilding.h"
#include "mockupDrivers/MockupVideoDriver.h"
#include "uiHelper/uiHelpers.hpp"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include <boost/test/unit_test.hpp>

// LCOV_EXCL_START
static std::ostream& operator<<(std::ostream& os, const Cursor& cursor)
{
    return os << static_cast<unsigned>(cursor);
}
// LCOV_EXCL_STOP

// Test stuff related to building/building quality
BOOST_AUTO_TEST_SUITE(GameInterfaceDesktop)

namespace {

struct dskGameInterfaceMock : public dskGameInterface
{
    dskGameInterfaceMock(std::shared_ptr<Game> game)
        : dskGameInterface(std::move(game), std::shared_ptr<NWFInfo>(), 0, false)
    {}
    void Msg_PaintBefore() override {}
    void Msg_PaintAfter() override {}
    using dskGameInterface::actionwindow;
};
struct GameInterfaceFixture : uiHelper::Fixture
{
    WorldFixture<CreateEmptyWorld, 1> worldFixture;
    dskGameInterfaceMock* gameDesktop;
    const GameWorldView* view;
    GameInterfaceFixture()
    {
        gameDesktop = static_cast<dskGameInterfaceMock*>(
          WINDOWMANAGER.Switch(std::make_unique<dskGameInterfaceMock>(worldFixture.game)));
        WINDOWMANAGER.Draw();
        view = &gameDesktop->GetView();
    }
};
struct AiBuildingInspectionFixture : uiHelper::Fixture
{
    WorldFixture<CreateEmptyWorld, 2> worldFixture;
    dskGameInterfaceMock* gameDesktop;

    AiBuildingInspectionFixture()
    {
        LOADER.LoadDummyMapFiles();
        worldFixture.world.GetPlayer(1).ps = PlayerState::AI;
        gameDesktop = static_cast<dskGameInterfaceMock*>(
          WINDOWMANAGER.Switch(std::make_unique<dskGameInterfaceMock>(worldFixture.game)));
        WINDOWMANAGER.Draw();
    }

    template<class T>
    T* OpenBuilding(MapPoint pt, bool readOnly = true)
    {
        worldFixture.world.SetVisibility(pt, 0, Visibility::Visible);
        gameDesktop->ShowBuildingWindow(pt, readOnly);
        auto* wnd = dynamic_cast<T*>(WINDOWMANAGER.GetTopMostWindow());
        BOOST_TEST_REQUIRE(wnd);
        return wnd;
    }

    void CloseTopWindow()
    {
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow());
        WINDOWMANAGER.GetTopMostWindow()->Close();
        WINDOWMANAGER.Draw();
        BOOST_TEST_REQUIRE(!WINDOWMANAGER.GetTopMostWindow());
    }
};
void checkNotScrolling(const GameWorldView& view, Cursor cursor = Cursor::Hand)
{
    const DrawPoint pos = view.GetOffset();
    MouseCoords mouse(Position(40, 11));
    WINDOWMANAGER.Msg_MouseMove(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == cursor);
    BOOST_TEST_REQUIRE(view.GetOffset() == pos);
    mouse.pos += Position(-20, 30);
    WINDOWMANAGER.Msg_MouseMove(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == cursor);
    BOOST_TEST_REQUIRE(view.GetOffset() == pos);
}
} // namespace

BOOST_FIXTURE_TEST_CASE(Scrolling, GameInterfaceFixture)
{
    const int acceleration = 2;
    SETTINGS.interface.invertMouse = false;

    Position startPos(10, 15);
    MouseCoords mouse(startPos, false, true);
    // Regular scrolling: Right down, 2 moves, right up
    {
        WINDOWMANAGER.Msg_RightDown(mouse);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        DrawPoint pos = view->GetOffset();
        mouse.pos = startPos + Position(4, 3);
        WINDOWMANAGER.Msg_MouseMove(mouse);
        pos += acceleration * Position(4, 3);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        mouse.pos = startPos + Position(-6, 7);
        WINDOWMANAGER.Msg_MouseMove(mouse);
        pos += acceleration * Position(-6, 7);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        mouse.rdown = false;
        WINDOWMANAGER.Msg_RightUp(mouse);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Hand);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        checkNotScrolling(*view);
    }

    // Inverted scrolling
    {
        SETTINGS.interface.invertMouse = true;
        WINDOWMANAGER.Msg_RightDown(mouse);
        startPos = mouse.pos;
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        const DrawPoint pos = view->GetOffset();
        mouse.pos = startPos + Position(4, 3);
        WINDOWMANAGER.Msg_MouseMove(mouse);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos - acceleration * Position(4, 3));
        mouse.rdown = false;
        WINDOWMANAGER.Msg_RightUp(mouse);
        SETTINGS.interface.invertMouse = false;
    }

    // Opening a window does not cancel scrolling
    {
        mouse.rdown = true;
        WINDOWMANAGER.Msg_RightDown(mouse);
        startPos = mouse.pos;
        DrawPoint pos = view->GetOffset();
        KeyEvent key;
        key.kt = KeyType::Char;
        key.c = 'm';
        key.ctrl = key.alt = key.shift = false;
        WINDOWMANAGER.Msg_KeyDown(key);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow());
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        mouse.pos = startPos + Position(-6, 7);
        WINDOWMANAGER.Msg_MouseMove(mouse);
        pos += acceleration * Position(-6, 7);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        // Closing it doesn't either
        WINDOWMANAGER.Msg_KeyDown(key);
        WINDOWMANAGER.Draw();
        BOOST_TEST_REQUIRE(gameDesktop->IsActive());
        BOOST_TEST_REQUIRE(!WINDOWMANAGER.GetTopMostWindow());
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        mouse.pos = startPos + Position(-6, 7);
        WINDOWMANAGER.Msg_MouseMove(mouse);
        pos += acceleration * Position(-6, 7);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        // Left click does cancel it
        mouse.ldown = true;
        WINDOWMANAGER.Msg_LeftDown(mouse);
        mouse.ldown = false;
        WINDOWMANAGER.Msg_LeftUp(mouse);
        BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Hand);
        BOOST_TEST_REQUIRE(view->GetOffset() == pos);
        checkNotScrolling(*view);
    }
}

BOOST_FIXTURE_TEST_CASE(ScrollingWhileRoadBuilding, GameInterfaceFixture)
{
    const int acceleration = 2;
    MapPoint hqPos = worldFixture.world.GetPlayer(0).GetFirstWH()->GetFlagPos(); //-V522
    gameDesktop->GI_StartRoadBuilding(hqPos, false);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Remove);
    Position startPos(10, 15);
    MouseCoords mouse(startPos, false, true);
    // Regular scrolling
    WINDOWMANAGER.Msg_RightDown(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    DrawPoint pos = view->GetOffset();
    mouse.pos = startPos + Position(4, 3);
    WINDOWMANAGER.Msg_MouseMove(mouse);
    pos += acceleration * Position(4, 3);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    mouse.rdown = false;
    WINDOWMANAGER.Msg_RightUp(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Remove);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    checkNotScrolling(*view, Cursor::Remove);

    // left click also stops scrolling
    mouse.rdown = true;
    WINDOWMANAGER.Msg_RightDown(mouse);
    startPos = mouse.pos;
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    mouse.pos = startPos + Position(-6, 7);
    WINDOWMANAGER.Msg_MouseMove(mouse);
    pos += acceleration * Position(-6, 7);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    mouse.ldown = true;
    WINDOWMANAGER.Msg_LeftDown(mouse);
    mouse.ldown = false;
    WINDOWMANAGER.Msg_LeftUp(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Remove);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    checkNotScrolling(*view, Cursor::Remove);
}

BOOST_FIXTURE_TEST_CASE(ScrollingWithCtrl, GameInterfaceFixture)
{
    const int acceleration = 2;
    Position startPos(10, 15);
    MouseCoords mouse(startPos, true);
    uiHelper::GetVideoDriver()->modKeyState_.ctrl = true;
    WINDOWMANAGER.Msg_LeftDown(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    DrawPoint pos = view->GetOffset();
    mouse.pos = startPos + Position(4, 3);
    WINDOWMANAGER.Msg_MouseMove(mouse);
    pos += acceleration * Position(4, 3);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Scroll);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    mouse.ldown = false;
    WINDOWMANAGER.Msg_LeftUp(mouse);
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetCursor() == Cursor::Hand);
    BOOST_TEST_REQUIRE(view->GetOffset() == pos);
    checkNotScrolling(*view);
}

BOOST_FIXTURE_TEST_CASE(IwActionClose, GameInterfaceFixture)
{
    gameDesktop->ShowActionWindow(iwAction::Tabs{}, MapPoint(0, 1), DrawPoint(42, 37), false);
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow());
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() == gameDesktop->actionwindow);
    WINDOWMANAGER.GetTopMostWindow()->Close();
    WINDOWMANAGER.Draw();
    BOOST_TEST_REQUIRE(WINDOWMANAGER.GetTopMostWindow() == nullptr);
    BOOST_TEST_REQUIRE(gameDesktop->actionwindow == nullptr);
}

BOOST_FIXTURE_TEST_CASE(AiBuildingInspectionWindowsAreReadOnly, AiBuildingInspectionFixture)
{
    GameWorld& world = worldFixture.world;
    worldFixture.ggs.setSelection(AddonId::MILITARY_CONTROL, 2);

    const MapPoint usualPos(22, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::Woodcutter, usualPos, 1, Nation::Romans);
    auto* usual = OpenBuilding<iwBuilding>(usualPos);
    BOOST_TEST(usual->GetCtrl<ctrlButton>(4)->GetEnabled());
    BOOST_TEST(!usual->GetCtrl<ctrlButton>(5)->GetEnabled());
    BOOST_TEST(!usual->GetCtrl<ctrlButton>(6)->GetEnabled());
    BOOST_TEST(usual->GetCtrl<ctrlButton>(7)->GetEnabled());
    BOOST_TEST(usual->GetCtrl<ctrlButton>(12)->GetEnabled());
    CloseTopWindow();

    const MapPoint templePos(24, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::Temple, templePos, 1, Nation::Romans);
    auto* temple = OpenBuilding<iwTempleBuilding>(templePos);
    BOOST_TEST(!temple->GetCtrl<ctrlButton>(8)->GetEnabled());
    CloseTopWindow();

    const MapPoint militaryPos(26, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::Guardhouse, militaryPos, 1, Nation::Romans);
    auto* military = OpenBuilding<iwMilitaryBuilding>(militaryPos);
    BOOST_TEST(military->GetCtrl<ctrlButton>(4)->GetEnabled());
    BOOST_TEST(!military->GetCtrl<ctrlButton>(5)->GetEnabled());
    BOOST_TEST(!military->GetCtrl<ctrlButton>(6)->GetEnabled());
    BOOST_TEST(military->GetCtrl<ctrlButton>(7)->GetEnabled());
    BOOST_TEST(military->GetCtrl<ctrlButton>(9)->GetEnabled());
    BOOST_TEST(!military->GetCtrl<ctrlButton>(10)->GetEnabled());
    BOOST_TEST(!military->GetCtrl<ctrlButton>(11)->GetEnabled());
    BOOST_TEST(!military->GetCtrl<ctrlButton>(14)->GetEnabled());
    CloseTopWindow();

    const MapPoint storehousePos(28, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::Storehouse, storehousePos, 1, Nation::Romans);
    auto* storehouse = OpenBuilding<iwBaseWarehouse>(storehousePos);
    auto* settings = storehouse->GetCtrl<ctrlOptionGroup>(13);
    BOOST_TEST(!settings->GetButton(14)->GetEnabled());
    BOOST_TEST(!settings->GetButton(15)->GetEnabled());
    BOOST_TEST(!settings->GetButton(16)->GetEnabled());
    BOOST_TEST(!storehouse->GetCtrl<ctrlButton>(17)->GetEnabled());
    BOOST_TEST(storehouse->GetCtrl<ctrlButton>(18)->GetEnabled());
    BOOST_TEST(storehouse->GetCtrl<ctrlButton>(19)->GetEnabled());
    BOOST_TEST(!storehouse->GetCtrl<ctrlButton>(20)->GetEnabled());
    BOOST_TEST(!storehouse->GetCtrl<ctrlGroup>(100)->GetCtrl<ctrlButton>(100)->GetEnabled());
    static_cast<Window*>(storehouse)->Msg_ButtonClick(0);
    BOOST_TEST(!settings->GetButton(14)->GetEnabled());
    BOOST_TEST(!settings->GetButton(15)->GetEnabled());
    BOOST_TEST(!settings->GetButton(16)->GetEnabled());
    BOOST_TEST(!storehouse->GetCtrl<ctrlButton>(17)->GetEnabled());
    CloseTopWindow();

    auto* hq = OpenBuilding<iwHQ>(world.GetPlayer(1).GetHQPos());
    auto* reserve = hq->GetCtrl<ctrlGroup>(102);
    BOOST_TEST(!reserve->GetCtrl<ctrlButton>(11)->GetEnabled());
    BOOST_TEST(!reserve->GetCtrl<ctrlButton>(16)->GetEnabled());
    CloseTopWindow();

    const MapPoint harborPos(30, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::HarborBuilding, harborPos, 1, Nation::Romans);
    auto* harbor = OpenBuilding<iwHarborBuilding>(harborPos);
    auto* expedition = harbor->GetCtrl<ctrlGroup>(103);
    BOOST_TEST(!expedition->GetCtrl<ctrlButton>(1)->GetEnabled());
    BOOST_TEST(!expedition->GetCtrl<ctrlButton>(3)->GetEnabled());
    CloseTopWindow();
}

BOOST_FIXTURE_TEST_CASE(LocalBuildingInspectionRemainsEditable, AiBuildingInspectionFixture)
{
    const MapPoint usualPos(22, 2);
    BuildingFactory::CreateBuilding(worldFixture.world, BuildingType::Woodcutter, usualPos, 0, Nation::Romans);
    OpenBuilding<iwBuilding>(usualPos);
    gameDesktop->ShowBuildingWindow(usualPos, false);
    auto* usual = OpenBuilding<iwBuilding>(usualPos, false);
    BOOST_TEST(usual->GetCtrl<ctrlButton>(5)->GetEnabled());
    BOOST_TEST(usual->GetCtrl<ctrlButton>(6)->GetEnabled());
}

BOOST_FIXTURE_TEST_CASE(AiBuildingNavigationSkipsHiddenBuildings, AiBuildingInspectionFixture)
{
    GameWorld& world = worldFixture.world;
    const MapPoint firstPos(22, 2);
    const MapPoint hiddenPos(24, 2);
    const MapPoint visiblePos(26, 2);
    BuildingFactory::CreateBuilding(world, BuildingType::Woodcutter, firstPos, 1, Nation::Romans);
    BuildingFactory::CreateBuilding(world, BuildingType::Woodcutter, hiddenPos, 1, Nation::Romans);
    BuildingFactory::CreateBuilding(world, BuildingType::Woodcutter, visiblePos, 1, Nation::Romans);
    world.SetVisibility(firstPos, 0, Visibility::Visible);
    world.SetVisibility(hiddenPos, 0, Visibility::FogOfWar);
    world.SetVisibility(visiblePos, 0, Visibility::Visible);

    auto* usual = OpenBuilding<iwBuilding>(firstPos);
    static_cast<Window*>(usual)->Msg_ButtonClick(12);
    auto* next = dynamic_cast<iwBuilding*>(WINDOWMANAGER.GetTopMostWindow());
    BOOST_TEST_REQUIRE(next);
    BOOST_TEST(next->GetID() == CGI_BUILDING + MapBase::CreateGUIID(visiblePos));
    BOOST_TEST(!next->GetCtrl<ctrlButton>(5)->GetEnabled());
}

BOOST_AUTO_TEST_SUITE_END()
