// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ProtoWriters.h"

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message_lite.h"
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/fstream.hpp>
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

} // namespace

void WriteSnapshotFiles(const SnapshotFiles& snapshots, const boost::filesystem::path& outputDir)
{
    if(boost::filesystem::exists(outputDir))
    {
        if(!boost::filesystem::is_directory(outputDir))
            throw std::runtime_error("Output path is not a directory: " + outputDir.string());
    } else
    {
        boost::filesystem::create_directories(outputDir);
    }

    WriteDeterministicBinary(snapshots.buildingLocations, outputDir / "building_locations.pb");
    WriteDeterministicBinary(snapshots.roadLocations, outputDir / "road_locations.pb");
    WriteDeterministicBinary(snapshots.environmentSnapshot, outputDir / "environment_snapshot.pb");
    WriteDeterministicBinary(snapshots.buildingQualitySnapshot, outputDir / "building_quality_snapshot.pb");
}

} // namespace QuantityExtractor
