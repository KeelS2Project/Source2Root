#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace source2root::topmenus {

class Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Menu;

enum class Kind { Category, Item };

enum class Access { Hidden, Enabled, Disabled };

struct Object {
    std::shared_ptr<Menu> menu;
    std::string key, category, label, permission, plugin;
    std::uint64_t owner = 0, sequence = 0;
    std::int32_t order = 0, handle = 0, data = 0;
    Kind kind = Kind::Item;
    bool live = true;
    std::function<void()> release;
    ~Object();
    void Close();
    void SetLabel(std::string value);
    void SetOrder(std::int32_t value);
};

struct Row {
    std::string label;
    bool enabled = true;
    std::weak_ptr<Object> object;
    int page = -1;
    bool back = false;
    bool empty = false;
};

struct Page {
    std::string title, category;
    std::vector<Row> rows;
    std::size_t page = 0;
    std::uint64_t revision = 0;
};

struct Menu : std::enable_shared_from_this<Menu> {
    std::string name, title;
    std::uint64_t revision = 1, next_sequence = 1;
    std::vector<std::weak_ptr<Object>> objects;
    std::shared_ptr<Object> Add(Kind kind, std::string key, std::string category, std::string label,
        std::string permission, std::string plugin, std::uint64_t owner, std::int32_t order);

    void SetTitle(std::string value);
    std::vector<std::shared_ptr<Object>> Active(const std::function<bool(std::uint64_t)>& running);
    Page Build(const std::string& category, std::size_t page,
        const std::function<bool(std::uint64_t)>& running,
        const std::function<Access(const std::shared_ptr<Object>&)>& access);
};

class Catalog {
public:
    std::shared_ptr<Menu> Open(const std::string& name, const std::string& title);

private:
    std::vector<std::weak_ptr<Menu>> menus_;
};

}
