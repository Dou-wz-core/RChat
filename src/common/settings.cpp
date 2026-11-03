#include "common/settings.hpp"
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>

namespace chat::settings {
namespace {
std::map<std::string, std::string> values;
std::mutex mutex;
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
}
void load(const std::filesystem::path& file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open local configuration: " + file.string());
    std::map<std::string, std::string> parsed;
    std::string line; unsigned number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#') continue;
        const auto equal = line.find('=');
        const auto key = trim(line.substr(0, equal));
        bool valid = equal != std::string::npos && key.compare(0, 5, "CHAT_") == 0;
        for (unsigned char c : key) valid = valid && (std::isalnum(c) || c == '_');
        if (!valid) throw std::runtime_error("Invalid local configuration line " + std::to_string(number));
        auto value = trim(line.substr(equal + 1));
        if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
            if (value.size() < 2 || value.back() != value.front())
                throw std::runtime_error("Unclosed quote in local configuration line " + std::to_string(number));
            value = value.substr(1, value.size() - 2);
        }
        parsed[key] = std::move(value);
    }
    std::lock_guard<std::mutex> guard(mutex);
    values = std::move(parsed);
}
void loadForExecutable(const char* executable) {
    if (const char* explicitFile = std::getenv("CHAT_ENV_FILE")) { load(explicitFile); return; }
    const auto local = std::filesystem::current_path() / ".env";
    if (std::filesystem::is_regular_file(local)) { load(local); return; }
    const auto adjacent = std::filesystem::absolute(executable).parent_path() / ".env";
    if (std::filesystem::is_regular_file(adjacent)) load(adjacent);
}
std::string get(const char* key, const char* fallback) {
    if (const char* value = std::getenv(key)) return value;
    std::lock_guard<std::mutex> guard(mutex);
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}
}
