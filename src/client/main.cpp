#include "net/udp_node.hpp"
#include "net/cli.hpp"
#include "json.hpp"
#include <atomic>
#include <iostream>
#include <thread>
#include "console_io.hpp"
#include "common/settings.hpp"

int demoMain(int argc, char** argv);

// 控制台每行输入一条 JSON；UDP 消息使用二进制首部和长度字段划分边界。
int clientMain(int argc, char** argv) {
    using namespace chat::net;
    try {
        chat::settings::loadForExecutable(argv[0]);
        if (argc == 1 || (argc > 1 && std::string(argv[1]) == "--demo")) return demoMain(argc, argv);
        if (argc < 3 || argc > 4) throw std::invalid_argument("usage: UdpClient <IPv4> <port> [one JSON request]");
        // 使用系统分配的临时端口，握手完成后才允许发送业务请求。
        UdpNode client(false, {"0.0.0.0", 0});
        client.connect({argv[1], parsePort(argv[2])});
        ConnectionPtr connection; auto deadline = monotonicMs() + 15000;
        // 握手期间由当前线程驱动网络轮询，Opened 事件提供可用的逻辑连接。
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
            // 单次模式发送一条请求，打印收到的首条完整消息后关闭会话。
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
        // 持续模式独立处理收包、确认和心跳，主线程等待控制台输入时仍能接收推送。
        std::thread receiver([&] {
            try {
                while (!client.stopped()) {
                    client.poll(); Event event;
                    while (client.nextEvent(event)) {
                        // replyTo 标识响应对应的传输请求，0 通常表示服务端主动推送。
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
        // 输入异常和正常退出都先停止节点并等待接收线程，避免线程访问已销毁的对象。
        } catch (...) { quitting = true; client.stop(); receiver.join(); throw; }
        quitting = true; client.stop(); receiver.join(); return failed ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        // Windows 命令行先从 UTF-16 转为 UTF-8，再复用同一套 JSON 客户端逻辑。
        // 参数字符串全部构造完成后再取指针，避免容器扩容使已有指针失效。
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
