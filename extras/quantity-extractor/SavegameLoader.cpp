// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SavegameLoader.h"

#include "Game.h"
#include "ILocalGameState.h"
#include "PlayerInfo.h"
#include "Savegame.h"
#include <boost/nowide/iostream.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace QuantityExtractor {
namespace {

class LocalGameState final : public ILocalGameState
{
public:
    unsigned GetPlayerId() const override { return 0u; }
    bool IsHost() const override { return true; }
    std::string FormatGFTime(unsigned numGFs) const override { return std::to_string(numGFs); }
    void SystemChat(const std::string& text) override { boost::nowide::cout << text << '\n'; }
};

} // namespace

LoadedGame LoadSavegame(const boost::filesystem::path& savePath)
{
    Savegame savegame;
    if(!savegame.Load(savePath, SaveGameDataToLoad::All))
    {
        std::string message = "Could not load savegame " + savePath.string();
        if(!savegame.GetLastErrorMsg().empty())
            message += ": " + savegame.GetLastErrorMsg();
        throw std::runtime_error(message);
    }

    const unsigned numPlayers = savegame.GetNumPlayers();
    std::vector<PlayerInfo> players;
    players.reserve(numPlayers);
    for(unsigned i = 0; i < numPlayers; ++i)
        players.emplace_back(savegame.GetPlayer(i));

    auto game = std::make_unique<Game>(savegame.ggs, savegame.start_gf, players);
    LocalGameState localGameState;
    savegame.sgd.ReadSnapshot(*game, localGameState);
    game->world_.InitAfterLoad();

    return LoadedGame{std::move(game), savegame.start_gf};
}

} // namespace QuantityExtractor
