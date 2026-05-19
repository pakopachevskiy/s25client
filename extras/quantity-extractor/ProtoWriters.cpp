// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ProtoWriters.h"

#include "Game.h"
#include "GamePlayer.h"
#include "gameData/NationConsts.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message_lite.h"
#include "nlohmann/json.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace QuantityExtractor {
namespace {

void WriteDeterministicBinary(const google::protobuf::MessageLite& message, const boost::filesystem::path& path)
{
    boost::nowide::ofstream stream(path.string(), std::ios::binary | std::ios::trunc);
    if(!stream)
        throw std::runtime_error("Could not open " + path.string() + " for writing");

    {
        google::protobuf::io::OstreamOutputStream zeroCopyOut(&stream);
        google::protobuf::io::CodedOutputStream codedOut(&zeroCopyOut);
        codedOut.SetSerializationDeterministic(true);
        if(!message.SerializeToCodedStream(&codedOut) || codedOut.HadError())
            throw std::runtime_error("Could not serialize " + path.string());
    }

    if(!stream.good())
        throw std::runtime_error("Could not finish writing " + path.string());
}

void EnsureOutputDir(const boost::filesystem::path& outputDir)
{
    if(boost::filesystem::exists(outputDir))
    {
        if(!boost::filesystem::is_directory(outputDir))
            throw std::runtime_error("Output path is not a directory: " + outputDir.string());
    } else
    {
        boost::filesystem::create_directories(outputDir);
    }
}

std::string ToHexColor(const unsigned color)
{
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << color;
    return ss.str();
}

} // namespace

void WriteSnapshotFiles(const SnapshotFiles& snapshots, const boost::filesystem::path& outputDir)
{
    EnsureOutputDir(outputDir);

    WriteDeterministicBinary(snapshots.buildingLocations, outputDir / "building_locations.pb");
    WriteDeterministicBinary(snapshots.roadLocations, outputDir / "road_locations.pb");
    WriteDeterministicBinary(snapshots.environmentSnapshot, outputDir / "environment_snapshot.pb");
    WriteDeterministicBinary(snapshots.buildingQualitySnapshot, outputDir / "building_quality_snapshot.pb");
}

void WritePlayersMetadata(const Game& game, const boost::filesystem::path& outputDir)
{
    EnsureOutputDir(outputDir);

    nlohmann::json players = nlohmann::json::array();
    for(unsigned playerId = 0; playerId < game.world_.GetNumPlayers(); ++playerId)
    {
        const GamePlayer& player = game.world_.GetPlayer(playerId);
        if(!player.isUsed())
            continue;

        players.push_back({{"color", player.color},
                           {"colorHex", ToHexColor(player.color)},
                           {"nation", static_cast<unsigned>(player.nation)},
                           {"nationName", NationNames[player.nation]},
                           {"playerId", player.GetPlayerId() + 1u},
                           {"team", static_cast<unsigned>(player.team)}});
    }

    const boost::filesystem::path path = outputDir / "players.json";
    boost::nowide::ofstream stream(path.string(), std::ios::trunc);
    if(!stream)
        throw std::runtime_error("Could not open " + path.string() + " for writing");
    stream << players.dump(2) << '\n';
    if(!stream.good())
        throw std::runtime_error("Could not finish writing " + path.string());
}

} // namespace QuantityExtractor
