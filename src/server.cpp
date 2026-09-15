#include "foundation.h"

#include <algorithm>
#include <fstream>

namespace sr {
void Foundation::CancelMap(std::uint64_t owner) {
    if (!pending_map_ || (owner && pending_map_->owner != owner)) return;
    const auto request = std::move(*pending_map_);
    pending_map_.reset();
    host_.Log(request.plugin + ": Map change to " + request.name + " cancelled before execution.");
}

bool Foundation::RunPendingMap() {
    if (!pending_map_ || !runtime_.Idle()) return false;
    const auto found = scripts_.find(pending_map_->plugin);
    if (found == scripts_.end() || !found->second.current || found->second.current->owner != pending_map_->owner ||
        found->second.current->state != PluginState::Running) { CancelMap(); return false; }
    const auto request = std::move(*pending_map_);
    pending_map_.reset();
    const auto result = host_.ChangeMap(request.name);
    if (result != KEEL_RESULT_OK) host_.Log(request.plugin + ": Requested map change to " + request.name +
        " failed (KeelResult " + std::to_string(result) + ").");
    return true;
}

void Foundation::BindServer(Script& script) {
    auto bind = [&](const char* name, int count, auto callback) {
        runtime_.Bind(*script.vm, name, count, [&, callback](const Arguments& args) -> Cell { Thread(); return callback(args); });
    };
    const auto map_name = [&](const std::string& name) {
        if (!name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) return true;
        script.error = "Map names require 1..64 lowercase letters, digits or underscores.";
        return false;
    };
    const auto result = [&](KeelResult value, const char* action) {
        if (value == KEEL_RESULT_OK) return true;
        script.error = std::string(action) + (value == KEEL_RESULT_UNSUPPORTED ? " is unavailable on this game host." :
            value == KEEL_RESULT_NOT_FOUND ? " is unavailable." : " request failed.");
        if (settings_.debug) host_.Log(script.manifest.id + ": " + script.error + " (KeelResult " + std::to_string(value) + ")");
        return false;
    };
    bind("IsMapInstalled", 1, [&, map_name, result](const Arguments& args) {
        const auto name = args.String(1);
        if (!map_name(name)) return false;
        bool installed = false;
        if (!result(host_.MapInstalled(name, installed), "Map lookup")) return false;
        if (!installed) script.error = "Map is not installed.";
        return installed;
    });
    bind("ChangeMap", 1, [&, map_name, result](const Arguments& args) {
        if (script.state != PluginState::Running) { script.error = "Change maps from a running callback."; return false; }
        const auto name = args.String(1);
        if (!map_name(name)) return false;
        if (pending_map_) { script.error = "A map change is already pending."; return false; }
        bool installed = false;
        if (!result(host_.MapInstalled(name, installed), "Map lookup")) return false;
        if (!installed) { script.error = "Map is not installed."; return false; }
        pending_map_ = PendingMap{script.owner, script.manifest.id, name};
        return true;
    });
    bind("RestartRound", 1, [&, result](const Arguments& args) {
        if (script.state != PluginState::Running) { script.error = "Restart rounds from a running callback."; return false; }
        const auto seconds = args.Int(1);
        if (seconds < 1 || seconds > 60) { script.error = "Restart delay must be from 1 to 60 seconds."; return false; }
        return result(host_.RestartRound(seconds), "Round restart");
    });
    bind("OpenConfigFile", 1, [&](const Arguments& args) -> Cell {
        Limit(script);
        const auto name = args.String(1);
        if (name.empty() || name.size() > 64 || name.front() == '.' || name.find("..") != std::string::npos ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
            })) { script.error = "Use a filename in the Source2Root configs directory."; return 0; }
        if (handles_.Owned(script.owner, ConfigFileType).size() >= 8) { script.error = "Close a configuration file before opening another (limit 8)."; return 0; }
        try {
            const auto root = std::filesystem::canonical(root_);
            const auto path = std::filesystem::canonical(root / "configs" / name);
            if (path.parent_path() != root / "configs" || !std::filesystem::is_regular_file(path))
                throw std::runtime_error("file must be inside Source2Root configs");
            const auto size = std::filesystem::file_size(path);
            if (size > 131072) throw std::runtime_error("file exceeds 128 KiB");
            std::ifstream input(path, std::ios::binary);
            std::string text(static_cast<std::size_t>(size), '\0');
            if (!input.read(text.data(), static_cast<std::streamsize>(size)) || input.peek() != std::char_traits<char>::eof())
                throw std::runtime_error("could not read a complete snapshot");
            if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return (c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127; }))
                throw std::runtime_error("file contains non-text control bytes");
            if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
            return handles_.Add(script.owner, ConfigFileType, ConfigFile{std::move(text)});
        } catch (const std::filesystem::filesystem_error&) {
            script.error = "Could not open " + name + ": file is missing or unreadable.";
            return 0;
        } catch (const std::exception& error) {
            script.error = "Could not open " + name + ": " + error.what();
            return 0;
        }
    });
    bind("ReadConfigLine", 3, [&](const Arguments& args) {
        auto& file = std::get<ConfigFile>(handles_.Get(args.Int(1), script.owner, ConfigFileType));
        const auto capacity = args.Int(3);
        args.Output(2, capacity, "");
        if (file.cursor == file.text.size()) return 0;
        auto end = file.text.find('\n', file.cursor);
        if (end == std::string::npos) end = file.text.size();
        const auto next = end == file.text.size() ? end : end + 1;
        if (end > file.cursor && file.text[end - 1] == '\r') --end;
        if (end - file.cursor >= static_cast<std::size_t>(capacity)) { script.error = "Configuration line exceeds output capacity."; return -1; }
        args.Output(2, capacity, file.text.substr(file.cursor, end - file.cursor));
        file.cursor = next;
        return 1;
    });
    bind("CloseConfigFile", 1, [&](const Arguments& args) {
        handles_.Remove(args.Int(1), script.owner, ConfigFileType);
        return 1;
    });
}
}
