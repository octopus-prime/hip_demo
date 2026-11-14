#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

struct SceneDiagnostics {
    std::string_view text;
    std::string_view source;

    [[noreturn]] void fail(std::size_t offset, const std::string& message) const {
        const auto line = 1 + std::count(text.begin(), text.begin() + offset, '\n');
        const auto previous = offset == 0 ? std::string_view::npos : text.rfind('\n', offset - 1);
        const auto column = previous == std::string_view::npos ? offset + 1 : offset - previous;
        throw std::runtime_error(std::string(source) + ":" + std::to_string(line) + ":" +
                                 std::to_string(column) + ": " + message);
    }
};
