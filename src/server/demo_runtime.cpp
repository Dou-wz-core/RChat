#ifdef _WIN32
#include <windows.h>
#endif
#include "demo_runtime.hpp"
#include "common/settings.hpp"
#include "common/log.hpp"
#include "db.hpp"
#include "json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

void ensureDemoDatabase() {
#ifdef _WIN32
    if (chat::settings::get("CHAT_DEMO_AUTOSTART", "0") != "1") return;
    { MySQL probe; if (probe.connect(false)) return; }
    const auto root = std::filesystem::u8path(chat::settings::get("CHAT_DEMO_PROJECT_ROOT"));
    const auto executable = std::filesystem::u8path(chat::settings::get("CHAT_DEMO_MYSQL_EXE"));
    const auto options = root / ".local" / "mysql-server.cnf";
    std::ifstream metadata(root / ".local" / "demo-state.json");
    if (!metadata || !std::filesystem::is_regular_file(executable) || !std::filesystem::is_regular_file(options))
        throw std::runtime_error("Demo database not prepared. Run scripts/build.ps1 -PrepareDemo first.");
    nlohmann::json state;
    try { state = nlohmann::json::parse(metadata); }
    catch (const nlohmann::json::exception&) { throw std::runtime_error("Invalid demo ownership metadata; run setup-demo.ps1 to inspect it."); }
    if (std::filesystem::u8path(state.at("ProjectRoot").get<std::string>()) != root ||
        std::filesystem::u8path(state.at("DataDirectory").get<std::string>()) != root / "db" / "data" ||
        chat::settings::get("CHAT_DB_HOST") != "127.0.0.1" ||
        std::to_string(state.at("Port").get<int>()) != chat::settings::get("CHAT_DB_PORT"))
        throw std::runtime_error("Demo database ownership/configuration mismatch; no process was started.");
    auto command = L"\"" + executable.wstring() + L"\" --defaults-file=\"" + options.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, root.c_str(), &startup, &process))
        throw std::runtime_error("Unable to start the project demo MySQL process");
    CloseHandle(process.hThread);
    LOG_INFO << "Starting project demo MySQL on " << chat::settings::get("CHAT_DB_PORT");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        { MySQL probe; if (probe.connect(false)) { CloseHandle(process.hProcess); return; } }
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    CloseHandle(process.hProcess);
    throw std::runtime_error("Project demo MySQL did not start; inspect .local/mysql-server.log");
#endif
}
