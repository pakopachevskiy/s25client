// Copyright (C) 2005 - 2026 Settlers Freaks <sf-team at siedler25.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ProtoWriters.h"
#include "QuantityExtractor.h"
#include "RttrConfig.h"
#include "SavegameLoader.h"
#include "s25util/Log.h"
#include <boost/filesystem/operations.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/iostream.hpp>
#include <exception>
#include <string>
#include <vector>

namespace bfs = boost::filesystem;
namespace bnw = boost::nowide;

namespace {

struct Arguments
{
    bfs::path savePath;
    bfs::path outputDir;
};

void PrintUsage(const char* executable)
{
    bnw::cerr << "Usage: " << executable << " <save-file> [output-dir]\n"
              << "       " << executable << " <save-file> --output-dir <output-dir>\n";
}

bool SetOutputDir(Arguments& args, const bfs::path& outputDir, bool& hasOutputDir)
{
    if(hasOutputDir)
    {
        bnw::cerr << "Output directory specified more than once\n";
        return false;
    }

    args.outputDir = outputDir;
    hasOutputDir = true;
    return true;
}

bool ParseArguments(int argc, char** argv, Arguments& args)
{
    if(argc < 2)
    {
        PrintUsage(argv[0]);
        return false;
    }

    bool hasOutputDir = false;
    std::vector<std::string> positionalArgs;

    for(int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if(arg == "--output-dir")
        {
            if(i + 1 == argc)
            {
                bnw::cerr << "--output-dir requires a directory argument\n";
                PrintUsage(argv[0]);
                return false;
            }
            if(!SetOutputDir(args, argv[++i], hasOutputDir))
            {
                PrintUsage(argv[0]);
                return false;
            }
            continue;
        }

        positionalArgs.push_back(arg);
    }

    if(positionalArgs.empty() || positionalArgs.size() > 2)
    {
        PrintUsage(argv[0]);
        return false;
    }

    args.savePath = positionalArgs[0];
    if(positionalArgs.size() == 2 && !SetOutputDir(args, positionalArgs[1], hasOutputDir))
    {
        PrintUsage(argv[0]);
        return false;
    }
    if(!hasOutputDir)
        args.outputDir = bfs::current_path();

    return true;
}

bool ValidateSavePath(const bfs::path& savePath)
{
    if(!bfs::exists(savePath))
    {
        bnw::cerr << "Savegame file not found: " << savePath.string() << '\n';
        return false;
    }
    if(!bfs::is_regular_file(savePath))
    {
        bnw::cerr << "Savegame path is not a regular file: " << savePath.string() << '\n';
        return false;
    }
    if(savePath.extension() != ".sav")
    {
        bnw::cerr << "Savegame file must have a .sav extension: " << savePath.string() << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    bnw::nowide_filesystem();
    bnw::args args(argc, argv);

    Arguments parsedArgs;
    if(!ParseArguments(argc, argv, parsedArgs))
        return 1;

    if(!ValidateSavePath(parsedArgs.savePath))
        return 1;

    try
    {
        if(!RTTRCONFIG.Init())
            return 1;

        const bfs::path logDir = bfs::temp_directory_path() / "rttr-quantity-extractor-logs";
        bfs::create_directories(logDir);
        LOG.setLogFilepath(logDir);

        const QuantityExtractor::LoadedGame loadedGame = QuantityExtractor::LoadSavegame(parsedArgs.savePath);
        const QuantityExtractor::SnapshotFiles snapshots =
          QuantityExtractor::ExtractSnapshots(*loadedGame.game, loadedGame.gameframe);
        QuantityExtractor::WriteSnapshotFiles(snapshots, parsedArgs.outputDir);
        QuantityExtractor::WritePlayersMetadata(*loadedGame.game, parsedArgs.outputDir);

        bnw::cout << "Wrote quantity snapshot files to " << parsedArgs.outputDir.string() << '\n';
    } catch(const std::exception& e)
    {
        bnw::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
