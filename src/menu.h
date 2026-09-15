#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <keels2/player_input.h>

namespace sr {

struct MenuItem {
    std::string text;
    bool enabled = true;
    std::int32_t value = -1;
};

enum class MenuInput { Up, Down, Select, Back };
enum class MenuAction { None, Changed, Selected, Cancelled };

class MenuControls {
public:
    void Baseline(const KeelPlayerInput& input);
    std::optional<MenuInput> Read(const KeelPlayerInput& input);
private:
    std::uint64_t buttons_ = 0, context_ = 0;
};

class Menu {
public:
    static constexpr std::size_t ItemsPerPage = 3;
    std::string title;
    std::string permission;
    std::vector<MenuItem> items;
    std::size_t selected = 0;
    MenuAction Input(MenuInput input);
    std::string Html() const;
    static std::string Escape(const std::string& text);
};

}
