#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sr {

class CoreSettings {
public:
    int activity = 13;
    int debug = 0;
    std::string public_trigger = "!", silent_trigger = "/";
    static constexpr std::array<std::string_view, 4> Names{
        "sr_show_activity", "sr_chat_public_trigger", "sr_chat_silent_trigger", "sr_debug"};

    std::string Value(std::string_view name) const;
    void Set(std::string_view name, const std::string& value);
    void SetAll(const std::array<std::string, 4>& values);
    static CoreSettings Read(const std::filesystem::path& path, CoreSettings initial);

private:
    void SetField(std::string_view name, const std::string& value);
    void Validate() const;
    static std::vector<std::string> ParseLine(const std::string& line);
};

}
