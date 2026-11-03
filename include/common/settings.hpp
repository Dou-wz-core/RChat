#pragma once
#include <filesystem>
#include <string>

namespace chat::settings {
// Explicit environment variables win; otherwise read local .env (never execute it).
void load(const std::filesystem::path& file);
void loadForExecutable(const char* executable);
std::string get(const char* key, const char* fallback = "");
}
