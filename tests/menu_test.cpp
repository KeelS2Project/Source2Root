#include "menu.h"

#include <cstdlib>
#include <iostream>

static void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    sr::Menu menu{"<test>", "demo.hello", {{"first"}, {"disabled", false}, {"third"}, {"fourth"}, {"fifth"}}};
    Require(menu.Html().find("&lt;test&gt;") != std::string::npos, "escape title");
    const auto first_page = menu.Html();
    Require(first_page.find("first") != std::string::npos && first_page.find("third") != std::string::npos &&
        first_page.find("fourth") == std::string::npos && first_page.find("fifth") == std::string::npos,
        "three choices fit each page without hiding later choices");
    std::size_t line_breaks = 0;
    for (auto offset = first_page.find("<br>"); offset != std::string::npos; offset = first_page.find("<br>", offset + 4))
        ++line_breaks;
    Require(line_breaks == 5 && first_page.find("Page 1/2") < first_page.find("<br>"),
        "full page has six logical lines with page number in the title");
    Require(menu.Input(sr::MenuInput::Down) == sr::MenuAction::Changed, "navigate");
    Require(menu.Input(sr::MenuInput::Select) == sr::MenuAction::None, "disabled selection");
    for (int i = 0; i < 3; ++i) menu.Input(sr::MenuInput::Down);
    Require(menu.Html().find("Page 2/2") != std::string::npos, "pagination");
    Require(menu.Html().find("fourth") != std::string::npos && menu.Html().find("fifth") != std::string::npos &&
        menu.Html().find("third") == std::string::npos, "remaining choices appear on second page");
    Require(menu.Html().find("Reload: previous page") != std::string::npos &&
        menu.Html(true).find("Reload: parent menu") != std::string::npos,
        "footer distinguishes previous page from parent menu");
    Require(menu.Input(sr::MenuInput::Select) == sr::MenuAction::Selected, "select second page");
    Require(menu.Input(sr::MenuInput::Back) == sr::MenuAction::Changed && menu.selected == 0, "back page");
    Require(menu.Input(sr::MenuInput::Back) == sr::MenuAction::Cancelled, "cancel");
    sr::MenuControls controls;
    KeelPlayerInput input{sizeof(input), 0, KEELS2_BUTTON_USE, 1};
    Require(!controls.Read(input) && !controls.Read(input), "first held input is baseline");
    const auto read = [&](std::uint64_t buttons) { input.buttons = buttons; return controls.Read(input); };
    Require(!read(0) && read(KEELS2_BUTTON_FORWARD) == sr::MenuInput::Up, "forward press");
    Require(!read(KEELS2_BUTTON_FORWARD | KEELS2_BUTTON_BACK), "opposite directions suppress navigation");
    Require(!read(KEELS2_BUTTON_BACK), "releasing opposite direction is not a new press");
    Require(!read(0) && read(KEELS2_BUTTON_BACK) == sr::MenuInput::Down, "fresh back press");
    Require(!read(0) && read(KEELS2_BUTTON_USE | KEELS2_BUTTON_BACK) == sr::MenuInput::Select, "select takes priority over navigation");
    Require(!read(0) && read(KEELS2_BUTTON_RELOAD | KEELS2_BUTTON_USE) == sr::MenuInput::Back, "back takes priority over selection");
    Require(!read(0) && !read(KEELS2_BUTTON_ATTACK | KEELS2_BUTTON_JUMP), "unrelated gameplay input ignored");
    input.context = 2; Require(!read(KEELS2_BUTTON_USE), "changed input context resets baseline");
    input.reserved = 1; Require(!read(0), "malformed snapshot rejected");
    input.reserved = 0; Require(!read(KEELS2_BUTTON_USE), "malformed snapshot resets later held input");
    input.buttons = UINT64_MAX; Require(!controls.Read(input), "unknown bits rejected");
    Require(!read(KEELS2_BUTTON_USE), "unknown bits reset later held input");
    input.context = 0; Require(!read(0), "zero context rejected");
    input.context = 2; Require(!read(KEELS2_BUTTON_USE), "zero context resets later held input");
    input.size = 0; Require(!read(0), "wrong snapshot size rejected");
    input.size = sizeof(input); Require(!read(KEELS2_BUTTON_USE), "wrong size resets later held input");
    std::cout << "menu navigation, disabled selection, pages, cancel and escaping passed\n";
}
