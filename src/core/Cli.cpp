#include "core/Cli.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace core {

namespace {

bool ParseInt(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseUint32(const std::string& text, std::uint32_t& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') {
        return false;
    }
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

} // namespace

bool ParseCli(int argc, char** argv, CliOptions& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--smoke-test") {
            options.smokeTest = true;
        } else if (arg == "--interaction-test") {
            options.interactionTest = true;
        } else if (arg == "--soak-test") {
            options.soakTest = true;
        } else if (arg == "--soak-test-long") {
            options.soakTestLong = true;
        } else if (arg == "--world-test") {
            options.worldTest = true;
        } else if (arg == "--render-test") {
            options.renderTest = true;
        } else if (arg == "--seed" || arg.rfind("--seed=", 0) == 0) {
            std::string seedText;
            if (arg == "--seed") {
                if (i + 1 >= argc) {
                    error = "Missing value for --seed";
                    return false;
                }
                seedText = argv[++i];
            } else {
                seedText = arg.substr(std::string("--seed=").size());
            }
            std::uint32_t seed = 0;
            if (!ParseUint32(seedText, seed)) {
                error = "Invalid value for --seed";
                return false;
            }
            options.soakTestSeed = seed;
        } else if (arg == "--render-test-out") {
            if (i + 1 >= argc) {
                error = "Missing value for --render-test-out";
                return false;
            }
            options.renderTestOut = argv[++i];
        } else if (arg == "--render-test-size") {
            if (i + 2 >= argc) {
                error = "Missing values for --render-test-size";
                return false;
            }
            int width = 0;
            int height = 0;
            if (!ParseInt(argv[++i], width) || !ParseInt(argv[++i], height) || width <= 0 || height <= 0) {
                error = "Invalid values for --render-test-size";
                return false;
            }
            options.renderTestWidth = width;
            options.renderTestHeight = height;
        } else if (arg == "--render-test-frames") {
            if (i + 1 >= argc) {
                error = "Missing value for --render-test-frames";
                return false;
            }
            int frames = 0;
            if (!ParseInt(argv[++i], frames) || frames <= 0) {
                error = "Invalid value for --render-test-frames";
                return false;
            }
            options.renderTestFrames = frames;
        } else if (arg == "--render-test-seed") {
            if (i + 1 >= argc) {
                error = "Missing value for --render-test-seed";
                return false;
            }
            std::uint32_t seed = 0;
            if (!ParseUint32(argv[++i], seed)) {
                error = "Invalid value for --render-test-seed";
                return false;
            }
            options.renderTestSeed = seed;
        } else if (arg == "--render-test-compare") {
            if (i + 1 >= argc) {
                error = "Missing value for --render-test-compare";
                return false;
            }
            options.renderTestCompare = true;
            options.renderTestComparePath = argv[++i];
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
        << "  --interaction-test\n"
        << "                  Run deterministic interaction smoke test and exit.\n"
        << "  --soak-test      Run deterministic headless soak test and exit.\n"
        << "  --soak-test-long Run deterministic long soak test and exit.\n"
        << "  --seed <u32>     Soak test seed override (default: 1337).\n"
        << "  --world-test     Run deterministic world logic test and exit.\n"
        << "  --render-test    Run deterministic offscreen render test and exit.\n"
        << "  --render-test-out <path>\n"
        << "                  Output PNG path (default: render_test.png).\n"
        << "  --render-test-size <w> <h>\n"
        << "                  Render test resolution (default: 256 256).\n"
        << "  --render-test-frames <n>\n"
        << "                  Render test frame count (default: 3).\n"
        << "  --render-test-seed <u32>\n"
        << "                  Render test seed (default: 1337).\n"
        << "  --render-test-compare <path>\n"
        << "                  Compare output against PNG (exact pixel match).\n"
        << "  --no-gl-debug    Disable OpenGL debug context/output.\n"
        << "  -h, --help       Show this help message.\n";
    return out.str();
}

} // namespace core
