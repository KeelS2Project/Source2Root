#include "menu.h"

#include <algorithm>

namespace sr {

void MenuControls::Baseline(const KeelPlayerInput& input) {
    const bool valid =
        input.size == sizeof(input) && !input.reserved && input.context && !(input.buttons & ~KEELS2_BUTTON_ALL);

    buttons_ = valid ? input.buttons : 0;
    context_ = valid ? input.context : 0;
}

std::optional<MenuInput> MenuControls::Read(const KeelPlayerInput& input) {
    const auto previous = buttons_, context = context_;
    Baseline(input);

    if (!context_ || context_ != context)
        return {};

    const auto pressed = buttons_ & ~previous;

    if (pressed & KEELS2_BUTTON_RELOAD)
        return MenuInput::Back;

    if (pressed & KEELS2_BUTTON_USE)
        return MenuInput::Select;

    constexpr auto directions = KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_BACK;

    if ((buttons_ & directions) == directions)
        return {};

    if (pressed & KEELS2_BUTTON_FORWARD)
        return MenuInput::Up;

    if (pressed & KEELS2_BUTTON_BACK)
        return MenuInput::Down;

    return {};
}

MenuAction Menu::Input(MenuInput input) {
    if (items.empty())
        return input == MenuInput::Back ? MenuAction::Cancelled : MenuAction::None;

    selected = std::min(selected, items.size() - 1);

    if (input == MenuInput::Select)
        return items[selected].enabled ? MenuAction::Selected : MenuAction::None;

    if (input == MenuInput::Back) {
        if (selected / ItemsPerPage == 0)
            return MenuAction::Cancelled;

        selected = (selected / ItemsPerPage - 1) * ItemsPerPage;
    } else if (input == MenuInput::Up) {
        selected = selected == 0 ? items.size() - 1 : selected - 1;
    } else {
        selected = (selected + 1) % items.size();
    }

    return MenuAction::Changed;
}

std::string Menu::Escape(const std::string& text) {
    std::string result;

    for (unsigned char c : text) {
        switch (c) {
        case '&':
            result += "&amp;";
            break;

        case '<':
            result += "&lt;";
            break;

        case '>':
            result += "&gt;";
            break;

        case '"':
            result += "&quot;";
            break;

        case '\'':
            result += "&#39;";
            break;

        default:
            if (c >= 32)
                result += static_cast<char>(c);

            break;
        }
    }

    return result;
}

std::string Menu::Html(bool has_parent) const {
    const auto page = selected / ItemsPerPage;
    const auto pages = std::max(std::size_t{1}, (items.size() + ItemsPerPage - 1) / ItemsPerPage);
    std::string html = "<font color='#78DCC8'>" + Escape(title) + " | Page " +
        std::to_string(page + 1) + "/" + std::to_string(pages) + "</font><br>";

    for (std::size_t i = page * ItemsPerPage; i < std::min(items.size(), page * ItemsPerPage + ItemsPerPage); ++i) {
        html += "<font color='";
        html += !items[i].enabled ? "#777777" : i == selected ? "#FFFF88" : "#FFFFFF";
        html += "'>";
        html += i == selected ? "&gt; " : "  ";
        html += Escape(items[i].text) + "</font><br>";
    }

    html += "<font color='#BBBBBB'>Forward/Back: move | Use: select<br>Reload: ";
    html += has_parent ? "parent menu" : page ? "previous page" : "close";
    return html + "</font>";
}

}
