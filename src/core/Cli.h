#pragma once

#include <cstdint>
#include <string>

namespace core {

struct CliOptions {
    bool smokeTest = false;
    bool interactionTest = false;
    bool worldTest = false;
    bool noGlDebug = false;
    bool help = false;
    bool renderTest = false;
    std::string renderTestOut = "render_test.png";
    int renderTestWidth = 256;
    int renderTestHeight = 256;
    int renderTestFrames = 3;
    std::uint32_t renderTestSeed = 1337;
    bool renderTestCompare = false;
    std::string renderTestComparePath;
};

bool ParseCli(int argc, char** argv, CliOptions& options, std::string& error);
std::string Usage(const char* argv0);

} // namespace core
