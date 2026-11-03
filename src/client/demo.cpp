#include "net/udp_node.hpp"
#include "net/cli.hpp"
#include "common/settings.hpp"
#include "public.hpp"
#include "json.hpp"
#include "console_io.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
using namespace chat::net;
using json = nlohmann::json;
std::mutex outputMutex;
void output(const std::string& text) { std::lock_guard<std::mutex> lock(outputMutex); console::writeLine(text); }
void display(const json& message) {
    const int type = message.value("msgid", ERROR_MSG);
    if (message.value("errno", 0) != 0) { output("ERROR " + message.value("errmsg", "request failed")); return; }
    if (type == ONE_CHAT_MSG || type == GROUP_CHAT_MSG) {
        output(std::string(type == ONE_CHAT_MSG ? "CHAT" : "GROUP") + " from=" + std::to_string(message.value("id", 0)) +
               " " + message.value("msg", std::string{}));
    } else if (type != LOGIN_MSG_ACK) output("ACK " + message.dump());
    if (message.contains("offlinemsg")) {
        for (const auto& saved : message["offlinemsg"]) output("OFFLINE " + saved.get<std::string>());
        if (message.value("offline_more", false)) output("More offline messages: /offline");
    }
}
int targetId(const std::string& name) {
    if (name == "alice" || name == "Alice") return 1;
    if (name == "bob" || name == "Bob") return 2;
    const auto number = parsePort(name); // demo user IDs use the same positive bounded numeric syntax
    return number;
}
}

// Friendly demo front end; the original IPv4/port/JSON mode remains available.
int demoMain(int argc, char** argv) {
    using namespace chat::net;
    std::string role;
    if (argc == 1) {
        output("选择演示账号：1 = Alice，2 = Bob（密码已配置，直接输入 1 或 2）");
        if (!console::readLine(role)) return 0;
    } else {
        if (argc != 3) throw std::invalid_argument("usage: UdpClient --demo alice|bob");
        role = argv[2];
    }
    const int user = targetId(role);
    if (user != 1 && user != 2) throw std::invalid_argument("demo role must be alice/1 or bob/2");
    const std::string name = user == 1 ? "Alice" : "Bob";
    UdpNode node(false, {"0.0.0.0", 0});
    node.connect({chat::settings::get("CHAT_SERVER_IP", "127.0.0.1"),
                  parsePort(chat::settings::get("CHAT_SERVER_PORT", "6000"))});
    ConnectionPtr connection;
    auto deadline = monotonicMs() + 15000;
    while (!connection && monotonicMs() < deadline) {
        node.poll(); Event event;
        while (node.nextEvent(event)) {
            if (event.type == EventType::Opened) connection = event.connection;
            if (event.type == EventType::Closed) throw std::runtime_error(event.reason);
        }
    }
    if (!connection) throw std::runtime_error("No server. Start ChatServer.exe or scripts/start-server.ps1 first.");
    const json login = {{"msgid", LOGIN_MSG}, {"id", user},
                        {"password", chat::settings::get("CHAT_DEMO_PASSWORD", "demo123456")}};
    if (!connection->send(login.dump())) throw std::runtime_error("login send rejected");
    bool loggedIn = false;
    deadline = monotonicMs() + 15000;
    while (!loggedIn && monotonicMs() < deadline) {
        node.poll(); Event event;
        while (node.nextEvent(event)) {
            if (event.type == EventType::Closed) throw std::runtime_error(event.reason);
            if (event.type == EventType::Message) {
                const auto response = json::parse(event.message.body);
                if (response.value("msgid", ERROR_MSG) == LOGIN_MSG_ACK && response.value("errno", 1) == 0) {
                    loggedIn = true; display(response);
                } else if (response.value("msgid", ERROR_MSG) == LOGIN_MSG_ACK || response.value("msgid", ERROR_MSG) == ERROR_MSG)
                    throw std::runtime_error(response.value("errmsg", "demo login failed"));
                else display(response);
            }
        }
    }
    if (!loggedIn) throw std::runtime_error("demo login timeout");
    output("LOGIN_OK name=" + name + " id=" + std::to_string(user));
    output("直接输入文字发送给对方；/to alice|bob 文字；/all 群聊文字；/offline；/quit");
    std::atomic<bool> quitting{false}, failed{false}, logoutDone{false};
    std::condition_variable logoutReady; std::mutex logoutMutex;
    std::thread receiver([&] {
        try {
            while (!node.stopped()) {
                node.poll(); Event event;
                while (node.nextEvent(event)) {
                    if (event.type == EventType::Message) {
                        const auto message = json::parse(event.message.body);
                        if (message.value("msgid", ERROR_MSG) == LOGOUT_MSG_ACK) { logoutDone = true; logoutReady.notify_all(); }
                        display(message);
                    }
                    if (event.type == EventType::Closed) {
                        if (!quitting) { failed = true; output("ERROR " + event.reason); }
                        node.stop(); logoutReady.notify_all();
                    }
                }
            }
        } catch (const std::exception& error) { failed = true; output("ERROR " + std::string(error.what())); node.stop(); logoutReady.notify_all(); }
    });
    try {
        std::string line;
        while (console::readLine(line) && line != "/quit" && connection->alive()) {
            if (line.empty()) continue;
            try {
                json request;
                if (line == "/offline") request = {{"msgid", OFFLINE_PULL_MSG}};
                else if (line == "/help") { output("文字 -> 对方；/to bob 文字；/all 文字；/offline；/quit"); continue; }
                else if (line.compare(0, 5, "/all ") == 0) request = {{"msgid", GROUP_CHAT_MSG}, {"groupid", 1}, {"msg", line.substr(5)}};
                else if (line.compare(0, 4, "/to ") == 0) {
                    const auto space = line.find(' ', 4);
                    if (space == std::string::npos || space + 1 == line.size()) throw std::invalid_argument("usage: /to alice|bob|id message");
                    request = {{"msgid", ONE_CHAT_MSG}, {"to", targetId(line.substr(4, space - 4))}, {"msg", line.substr(space + 1)}};
                } else if (line[0] == '/') throw std::invalid_argument("unknown command; /help");
                else request = {{"msgid", ONE_CHAT_MSG}, {"to", user == 1 ? 2 : 1}, {"msg", line}};
                const auto payload = request.dump();
                if (payload.size() > 8192) throw std::invalid_argument("message exceeds the 8192-byte business request limit");
                if (!connection->send(payload)) output("ERROR send rejected: queue full or session closed");
            } catch (const std::exception& error) { output("ERROR " + std::string(error.what())); }
        }
        if (connection->alive() && connection->send(json{{"msgid", LOGOUT_MSG}}.dump())) {
            std::unique_lock<std::mutex> lock(logoutMutex);
            logoutReady.wait_for(lock, std::chrono::seconds(3), [&] { return logoutDone || failed || node.stopped(); });
        }
    } catch (...) { quitting = true; node.stop(); receiver.join(); throw; }
    quitting = true; node.stop(); receiver.join();
    output("BYE " + name);
    return failed ? 1 : 0;
}
