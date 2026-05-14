#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace cubey::tests {

inline std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open source file: " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

inline void require_contains(const std::string& text, const std::string& needle,
                             const char* message) {
    if (text.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

inline void require_not_contains(const std::string& text, const std::string& needle,
                                 const char* message) {
    if (text.find(needle) != std::string::npos) {
        throw std::runtime_error(message);
    }
}

} // namespace cubey::tests
