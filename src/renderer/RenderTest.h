#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace renderer {

struct RenderTestOptions {
    std::string outputPath = "render_test.png";
    int width = 256;
    int height = 256;
    int frames = 3;
    std::uint32_t seed = 1337;
    std::optional<std::string> comparePath;
    bool enableGlDebug = false;
};

int RunRenderTest(const RenderTestOptions& options);

} // namespace renderer
