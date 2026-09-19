#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
void Require(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void Write(const std::filesystem::path& file, const std::string& source) {
    std::ofstream output(file, std::ios::binary);
    output << source;
    Require(static_cast<bool>(output), "write configuration fixture");
}
}

int main(int argc, char** argv) {
    Require(argc == 3, "config_test fixture-path shipped-config");
    const std::filesystem::path file(argv[1]);
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::remove(file);
    auto settings = sr::CoreSettings::Read(file, {});
    Require(settings.activity == 13 && settings.public_trigger == "!" && settings.silent_trigger == "/" && !settings.debug,
            "approved defaults apply when configuration is absent");

    Require(std::filesystem::is_regular_file(argv[2]), "shipped configuration exists");
    settings = sr::CoreSettings::Read(argv[2], {});
    Require(settings.activity == 13 && settings.public_trigger == "!" && settings.silent_trigger == "/" && !settings.debug,
            "shipped configuration matches approved runtime defaults");

    Write(file, "// Core settings\r\nsr_show_activity 5\r\nsr_chat_public_trigger \"/\" // public\n"
                "sr_chat_silent_trigger \"!\"\nsr_debug 1\n");

    settings = sr::CoreSettings::Read(file, settings);
    Require(settings.activity == 5 && settings.debug == 1 && settings.public_trigger == "/" &&
                settings.silent_trigger == "!",
            "quoted values, comments, Windows line endings and trigger swaps load atomically");

    for (const std::string source : {"sr_show_activity 16", "sr_debug 2", "sr_show_activity 1x", "unknown 1",
            "sr_chat_silent_trigger \"/\"", "sr_chat_public_trigger \"bad value\"", "sr_chat_public_trigger \"unterminated",
            "sr_show_activity 0;quit", "sr_show_activity 0 extra", "sr_chat_public_trigger \"!\"extra", "sr_show_activity"}) {
        Write(file, source);
        bool rejected = false;

        try {
            settings = sr::CoreSettings::Read(file, settings);
        } catch (const std::exception&) {
            rejected = true;
        }

        Require(rejected && settings.activity == 5 && settings.public_trigger == "/" && settings.silent_trigger == "!",
                "invalid configuration preserves prior settings");
    }

    Write(file, "sr_show_activity 0\nsr_debug invalid\n");

    try {
        settings = sr::CoreSettings::Read(file, settings);
    } catch (const std::exception& error) {
        Require(std::string(error.what()).find("line 2") != std::string::npos, "error identifies configuration line");
    }

    Require(settings.activity == 5, "earlier valid lines are not applied after a later invalid line");
    settings.Set("sr_chat_public_trigger", "");
    settings.Set("sr_chat_silent_trigger", "");
    Require(settings.public_trigger.empty() && settings.silent_trigger.empty(), "empty triggers disable chat shortcuts");
    settings.Set("sr_chat_public_trigger", "!");
    settings.Set("sr_chat_silent_trigger", "!!");
    Require(settings.Value("sr_chat_silent_trigger") == "!!", "overlapping prefixes support longest-match dispatch");
    bool rejected = false;

    try {
        settings.Set("sr_chat_public_trigger", "!!");
    } catch (const std::exception&) {
        rejected = true;
    }

    Require(rejected && settings.public_trigger == "!", "invalid runtime setting keeps prior value");
    Write(file, std::string(65537, ' '));
    rejected = false;

    try {
        sr::CoreSettings::Read(file, settings);
    } catch (const std::exception&) {
        rejected = true;
    }

    Require(rejected, "configuration has a bounded file size");
    std::cout << "core configuration parsing, defaults, atomic failures and trigger validation passed\n";
}
