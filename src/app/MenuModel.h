#pragma once

#include <string_view>

namespace app {

enum class MenuChoice {
    None,
    Option1,
    Option2,
    Option3
};

struct MenuModel {
    static constexpr std::string_view kMainMenuTitle = "[MENU] 1-New 2-Load 3-Exit";
    static constexpr std::string_view kMainMenuMissingTitle = "[MENU] No save found. 1-New 2-Load 3-Exit";
    static constexpr std::string_view kPauseMenuTitle = "[PAUSED] 1-Continue 2-Save 3-Load 4-Settings 5-Leave";
    static constexpr std::string_view kLoadingTitle = "[LOADING] Preparing world...";
};

} // namespace app
