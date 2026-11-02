#include "net/udp_node.hpp"
#include "net/cli.hpp"
#include "json.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include "console_io.hpp"

// Console input uses lines; UDP payloads have binary framing, no newline delimiter.
int clientMain(int argc, char** argv) {
    using namespace chat::net;
    try {
        if (argc < 3 || argc > 4) throw std::invalid_argument("usage: UdpClient <IPv4> <port> [one JSON request]");
        UdpNode client(false, {"0.0.0.0", 0});
        client.connect({argv[1], parsePort(argv[2])});
        ConnectionPtr connection; auto deadline = monotonicMs() + 15000;
        while (!connection && monotonicMs() < deadline) {
            client.poll(); Event event;
            while (client.nextEvent(event)) {
                if (event.type == EventType::Opened) connection = event.connection;
                if (event.type == EventType::Closed) throw std::runtime_error(event.reason);
            }
        }
        if (!connection) throw std::runtime_error("UDP handshake timeout");
        console::writeLine("UDP session " + std::to_string(connection->id()) + " established");
        if (argc == 4) {
            auto request = nlohmann::json::parse(argv[3]).dump();
            if (!connection->send(request)) throw std::runtime_error("request exceeds transport limits");
            deadline = monotonicMs() + 15000;
            while (monotonicMs() < deadline) {
                client.poll(); Event event;
                while (client.nextEvent(event)) {
                    if (event.type == EventType::Closed) throw std::runtime_error(event.reason);
                    if (event.type == EventType::Message) { console::writeLine(event.message.body); client.stop(); return 0; }
                }
            }
            throw std::runtime_error("response timeout");
        }
        std::atomic<bool> failed{false};
        std::atomic<bool> quitting{false};
        std::thread receiver([&] {
            try {
                while (!client.stopped()) {
                    client.poll(); Event event;
                    while (client.nextEvent(event)) {
                        if (event.type == EventType::Message) console::writeLine("reply_to=" + std::to_string(event.message.replyTo) + ' ' + event.message.body);
                        if (event.type == EventType::Closed) {
                            if (!quitting) { std::cerr << event.reason << std::endl; failed = true; }
                            client.stop();
                        }
                    }
                }
            } catch (const std::exception& error) { std::cerr << error.what() << std::endl; failed = true; client.stop(); }
        });
        console::writeLine("Enter JSON requests; /quit exits.");
        std::string line;
        try {
        while (console::readLine(line) && line != "/quit" && connection->alive()) {
            if (line.empty()) continue;
            try {
                auto payload = nlohmann::json::parse(line).dump();
                if (!connection->send(payload)) std::cerr << "send rejected: message/queue limit or closed session\n";
            } catch (const nlohmann::json::exception& error) { std::cerr << "invalid JSON: " << error.what() << '\n'; }
        }
        } catch (...) { quitting = true; client.stop(); receiver.join(); throw; }
        quitting = true; client.stop(); receiver.join(); return failed ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> arguments;
        for (int i = 0; i < argc; ++i) arguments.push_back(console::utf8(argv[i]));
        std::vector<char*> pointers;
        for (auto& argument : arguments) pointers.push_back(argument.data());
        return clientMain(argc, pointers.data());
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#else
int main(int argc, char** argv) { return clientMain(argc, argv); }
#endif
