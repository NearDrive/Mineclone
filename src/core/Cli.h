#pragma once

#include <string>

namespace core {

struct CliOptions {
    bool smokeTest = false;
    bool noGlDebug = false;
    bool help = false;
};

bool ParseCli(int argc, char** argv, CliOptions& options, std::string& error);
std::string Usage(const char* argv0);

} // namespace core
