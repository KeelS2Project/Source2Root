#include "types.h"
#include <cmath>

namespace source2root::db {

void QueryInput::Bind(int index, Parameter value) {
    if (index < 1 || index > 64)
        throw Error("Parameter index must be from 1 to 64.");

    if (const auto* text = std::get_if<std::string>(&value);
        text && (text->size() > 4095 || text->find('\0') != std::string::npos))
        throw Error("Parameter text must contain at most 4095 bytes without NUL.");

    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
        throw Error("Parameter number must be finite.");

    if (parameters.size() < static_cast<std::size_t>(index))
        parameters.resize(index);

    parameters[index - 1] = std::move(value);
}

void QueryInput::Validate() const {
    if (sql.empty() || sql.size() > 4095 || sql.find('\0') != std::string::npos)
        throw Error("SQL must contain one statement of at most 4095 bytes without NUL.");

    if (parameters.size() > 64)
        throw Error("Query exceeds 64 parameters.");

    std::size_t bytes = 0;

    for (const auto& parameter : parameters) {
        if (!parameter)
            throw Error("Every parameter up to the highest bound index must be set.");

        if (const auto* text = std::get_if<std::string>(&*parameter)) {
            if (text->size() > 4095 || text->find('\0') != std::string::npos)
                throw Error("Invalid parameter text.");

            bytes += text->size();
        }

        if (const auto* number = std::get_if<double>(&*parameter); number && !std::isfinite(*number))
            throw Error("Parameter number must be finite.");
    }

    if (bytes > 65536)
        throw Error("Query parameters exceed 64 KiB.");
}

}
