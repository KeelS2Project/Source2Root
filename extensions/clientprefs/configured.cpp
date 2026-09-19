#include "store.h"
#include "../database/settings.h"
#if defined(SR_MYSQL_DRIVER)
#include "mysql_store.h"
#endif
#include <mutex>
#include <optional>
#include <tuple>

namespace source2root::prefs {
StorageFactory ConfiguredStorage(std::filesystem::path data, std::filesystem::path config) {
    using Target = std::tuple<std::string, std::string, std::string, unsigned, std::string>;

    struct Selection {
        std::mutex mutex;
        std::optional<Target> target;
    };
    auto selection = std::make_shared<Selection>();
    return [data = std::move(data), config = std::move(config), selection]() -> std::unique_ptr<Storage> {
        db::Settings settings;
        settings.driver = "sqlite";
        settings.database = "clientprefs";
        // Broken links and unreadable files must not silently select SQLite.
        const auto status = std::filesystem::symlink_status(config);

        if (status.type() != std::filesystem::file_type::not_found)
            settings = db::ReadSettings(config, "clientprefs", "source2root.clientprefs");

        const bool local = settings.driver == "sqlite";

        if (!local && settings.driver != "mysql" && settings.driver != "mariadb")
            throw Error("Client preferences supports SQLite and MySQL/MariaDB profiles only.");

        const auto filename = data / (settings.database + ".sqlite");
        const Target target = local ? Target{"sqlite", filename.lexically_normal().string(), "", 0, ""}

            : Target{"mysql", settings.database, settings.socket.empty() ? settings.host : "",
                settings.socket.empty() ? settings.port : 0, settings.socket};

        {
            std::lock_guard lock(selection->mutex);

            if (selection->target && *selection->target != target)
                throw Error(
                    "Client preferences database target changed; restore its configuration, drain writes and reload the extension.");

            selection->target = target;
        }

        if (local)
            return std::make_unique<Store>(filename);
#if defined(SR_MYSQL_DRIVER)
        return std::make_unique<MysqlStore>(std::move(settings));
#else
        throw Error("MySQL/MariaDB driver is not installed.");
#endif
    };
}
}
