#include "soh/OTRGlobals.h"
#include "location_access.h"
#include <cstdio>

#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/entrance.h"
#include "soh/Enhancements/debugger/performanceTimer.h"

#include <fstream>

#include "3drando/shops.hpp"
extern "C" {
extern PlayState* gPlayState;
}

// generic grotto event list

void ReplaceFirstInString(std::string& s, std::string const& toReplace, std::string const& replaceWith) {
    size_t pos = s.find(toReplace);
    if (pos == std::string::npos) {
        return;
    }
    s.replace(pos, toReplace.length(), replaceWith);
}

void ReplaceAllInString(std::string& s, std::string const& toReplace, std::string const& replaceWith) {
    std::string buf;
    size_t pos = 0;
    size_t prevPos;

    buf.reserve(s.size());

    while (true) {
        prevPos = pos;
        pos = s.find(toReplace, pos);
        if (pos == std::string::npos) {
            break;
        }
        buf.append(s, prevPos, pos - prevPos);
        buf += replaceWith;
        pos += toReplace.size();
    }

    buf.append(s, prevPos, s.size() - prevPos);
    s.swap(buf);
}

std::string CleanConditionString(std::string condition) {
    ReplaceAllInString(condition, "logic->", "");
    ReplaceAllInString(condition, "ctx->", "");
    ReplaceAllInString(condition, ".Get()", "");
    ReplaceAllInString(condition, "GetSaveContext()->", "");
    return condition;
}

namespace Regions {
auto GetAllRegions() {
    static const size_t regionCount = RR_MAX - (RR_NONE + 1);

    static std::array<RandomizerRegion, regionCount> allRegions = {};

    static bool initialized = false;
    if (!initialized) {
        for (size_t i = 0; i < regionCount; i++) {
            allRegions[i] = (RandomizerRegion)((RR_NONE + 1) + i);
        }
        initialized = true;
    }

    return allRegions;
}

void AccessReset() {
    auto ctx = Rando::Context::GetInstance();
    for (const RandomizerRegion region : GetAllRegions()) {
        RegionTable(region)->ResetVariables();
    }

    if (/*Settings::HasNightStart TODO:: Randomize Starting Time*/ false) {
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD)) {
            RegionTable(RR_ROOT)->childNight = true;
        } else {
            RegionTable(RR_ROOT)->adultNight = true;
        }
    } else {
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD)) {
            RegionTable(RR_ROOT)->childDay = true;
        } else {
            RegionTable(RR_ROOT)->adultDay = true;
        }
    }
}

// Reset exits and clear items from locations
void ResetAllLocations() {
    auto ctx = Rando::Context::GetInstance();
    for (const RandomizerRegion region : GetAllRegions()) {
        RegionTable(region)->ResetVariables();
        // Erase item from every location in this exit
        for (LocationAccess& locPair : RegionTable(region)->locations) {
            RandomizerCheck location = locPair.GetLocation();
            Rando::Context::GetInstance()->GetItemLocation(location)->ResetVariables();
        }
    }

    if (/*Settings::HasNightStart TODO:: Randomize Starting Time*/ false) {
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD)) {
            RegionTable(RR_ROOT)->childNight = true;
        } else {
            RegionTable(RR_ROOT)->adultNight = true;
        }
    } else {
        if (ctx->GetOption(RSK_SELECTED_STARTING_AGE).Is(RO_AGE_CHILD)) {
            RegionTable(RR_ROOT)->childDay = true;
        } else {
            RegionTable(RR_ROOT)->adultDay = true;
        }
    }
}

bool HasTimePassAccess(uint8_t age) {
    for (const RandomizerRegion regionKey : GetAllRegions()) {
        auto region = RegionTable(regionKey);
        if (region->TimePass() &&
            ((age == RO_AGE_CHILD && region->Child()) || (age == RO_AGE_ADULT && region->Adult()))) {
            return true;
        }
    }
    return false;
}

// Will dump a file which can be turned into a visual graph using graphviz
// https://graphviz.org/download/
// Use command: dot -Tsvg <filename> -o world.svg
// Then open in a browser and CTRL + F to find the area of interest
void DumpWorldGraph(std::string str) {
    std::ofstream worldGraph;
    worldGraph.open(str + ".dot");
    worldGraph << "digraph {\n\tcenter=true;\n";

    for (const RandomizerRegion regionKey : GetAllRegions()) {
        auto region = RegionTable(regionKey);
        for (auto exit : region->exits) {
            if (exit.GetConnectedRegion()->regionName != "Invalid Region") {
                std::string parent = exit.GetParentRegion()->regionName;
                if (region->childDay) {
                    parent += " CD";
                }
                if (region->childNight) {
                    parent += " CN";
                }
                if (region->adultDay) {
                    parent += " AD";
                }
                if (region->adultNight) {
                    parent += " AN";
                }
                Region* connected = exit.GetConnectedRegion();
                auto connectedStr = connected->regionName;
                if (connected->childDay) {
                    connectedStr += " CD";
                }
                if (connected->childNight) {
                    connectedStr += " CN";
                }
                if (connected->adultDay) {
                    connectedStr += " AD";
                }
                if (connected->adultNight) {
                    connectedStr += " AN";
                }
                worldGraph << "\t\"" + parent + "\"[shape=\"plain\"];\n";
                worldGraph << "\t\"" + connectedStr + "\"[shape=\"plain\"];\n";
                worldGraph << "\t\"" + parent + "\" -> \"" + connectedStr + "\"\n";
            }
        }
    }
    worldGraph << "}";
    worldGraph.close();
}
} // namespace Regions

Region* RegionTable(const RandomizerRegion regionKey) {
    assert(regionKey < RR_MAX);
    return &areaTable[regionKey];
}

// Retrieve all the shuffable entrances of a specific type
std::vector<Rando::Entrance*> GetShuffleableEntrances(Rando::EntranceType type, bool onlyPrimary /*= true*/) {
    std::vector<Rando::Entrance*> entrancesToShuffle = {};
    for (RandomizerRegion region : Regions::GetAllRegions()) {
        for (auto& exit : RegionTable(region)->exits) {
            if ((exit.GetType() == type || type == Rando::EntranceType::All) && (exit.IsPrimary() || !onlyPrimary) &&
                exit.GetType() != Rando::EntranceType::None) {
                entrancesToShuffle.push_back(&exit);
            }
        }
    }
    return entrancesToShuffle;
}

Rando::Entrance* GetEntrance(RandomizerRegion source, RandomizerRegion destination) {
    for (auto& exit : RegionTable(source)->exits) {
        if (exit.GetOriginalConnectedRegionKey() == destination) {
            return &exit;
        }
    }

    return nullptr;
}
