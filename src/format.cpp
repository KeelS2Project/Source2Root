#include "runtime.h"

#include <array>
#include <charconv>
#include <cstring>

namespace sr {

std::string Arguments::Format(int index) const {
    const auto format = String(index, 4095);
    std::string result;
    auto value_index = index + 1;

    for (std::size_t position = 0; position < format.size();) {
        if (format[position] != '%') {
            result += format[position++];
            continue;
        }

        ++position;

        if (position == format.size())
            throw NativeError("incomplete format specifier");

        if (format[position] == '%') {
            result += '%';
            ++position;
            continue;
        }

        bool left = false, zero = false;

        if (format[position] == '-') {
            left = true;
            ++position;
        }

        if (position < format.size() && format[position] == '0') {
            zero = true;
            ++position;
        }

        int width = 0, precision = -1;

        while (position < format.size() && format[position] >= '0' && format[position] <= '9') {
            width = width * 10 + format[position++] - '0';

            if (width > 128)
                throw NativeError("format width exceeds 128");
        }

        if (position < format.size() && format[position] == '.') {
            ++position;
            precision = 0;

            if (position == format.size() || format[position] < '0' || format[position] > '9')
                throw NativeError("invalid format precision");

            while (position < format.size() && format[position] >= '0' && format[position] <= '9') {
                precision = precision * 10 + format[position++] - '0';

                if (precision > 256)
                    throw NativeError("format precision exceeds 256");
            }
        }

        if (position == format.size())
            throw NativeError("incomplete format specifier");

        const char spec = format[position++];
        std::string field;

        if (spec == 's') {
            field = String(value_index++, 4095);

            if (precision >= 0 && field.size() > static_cast<std::size_t>(precision))
                field.resize(precision);
        } else {
            Cell value = 0;
            std::memcpy(&value, Address(Int(value_index++), sizeof(value), true), sizeof(value));
            std::array<char, 384> buffer{};
            std::to_chars_result converted{};

            if (spec == 'f') {
                if (precision > 9)
                    throw NativeError("float precision exceeds 9");

                float number;
                std::memcpy(&number, &value, sizeof(number));
                converted = std::to_chars(buffer.data(),
                                          buffer.data() + buffer.size(),
                                          number,
                                          std::chars_format::fixed,
                                          precision < 0 ? 6 : precision);
            } else {
                if (precision >= 0)
                    throw NativeError("precision requires %s or %f");

                if (spec == 'c') {
                    if (value <= 0 || value > 255)
                        throw NativeError("%c requires a nonzero byte");

                    buffer[0] = static_cast<char>(value);
                    converted = {buffer.data() + 1, {}};
                } else if (spec == 'd' || spec == 'i')
                    converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
                else if (spec == 'u' || spec == 'x' || spec == 'X')
                    converted = std::to_chars(buffer.data(),
                                              buffer.data() + buffer.size(),
                                              static_cast<std::uint32_t>(value),
                                              spec == 'u' ? 10 : 16);
                else
                    throw NativeError("unsupported format specifier");
            }

            if (converted.ec != std::errc{})
                throw NativeError("number formatting failed");

            field.assign(buffer.data(), converted.ptr);

            if (spec == 'X')
                for (auto& c : field)
                    if (c >= 'a' && c <= 'f')
                        c -= 'a' - 'A';
        }

        if (field.size() < static_cast<std::size_t>(width)) {
            const auto padding = static_cast<std::size_t>(width) - field.size();

            if (left)
                field.append(padding, ' ');
            else if (zero && spec != 's' && spec != 'c')
                field.insert(!field.empty() && field.front() == '-' ? 1 : 0, padding, '0');
            else
                field.insert(0, padding, ' ');
        }

        if (result.size() + field.size() > 4095)
            throw NativeError("formatted message exceeds 4095 bytes");

        result += field;
    }

    if (result.size() > 4095)
        throw NativeError("formatted message exceeds 4095 bytes");

    if (value_index != Count() + 1)
        throw NativeError("unused format arguments");

    return result;
}

}
