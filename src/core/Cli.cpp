#include "core/Cli.h"

#include <sstream>

namespace core {

bool ParseCli(int argc, char** argv, CliOptions& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--smoke-test") {
            options.smokeTest = true;
        } else if (arg == "--no-gl-debug") {
            options.noGlDebug = true;
        } else if (arg == "-h" || arg == "--help") {
            options.help = true;
        } else {
            error = "Unknown option: " + arg;
            return false;
        }
    }
    return true;
}

std::string Usage(const char* argv0) {
    std::ostringstream out;
    out << "Usage: " << (argv0 ? argv0 : "Mineclone") << " [options]\n"
        << "Options:\n"
        << "  --smoke-test     Run deterministic smoke test and exit.\n"
        << "  --no-gl-debug    Disable OpenGL debug context/output.\n"
        << "  -h, --help       Show this help message.\n";
    return out.str();
}

} // namespace core
