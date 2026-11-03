#include "net/udp_node.hpp"
#include "net/cli.hpp"
#include <csignal>
#include <iostream>
// 信号处理函数只修改退出标志，套接字与会话清理由主循环结束后执行。
namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: UdpEchoServer [bind-ip] [port]");
        chat::net::UdpNode server(true, {argc > 1 ? argv[1] : "127.0.0.1", argc > 2 ? chat::net::parsePort(argv[2]) : std::uint16_t{6000}});
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        std::cout << "UDP echo listening " << server.localEndpoint().text() << std::endl;
        while (running) {
            // 示例服务在一个线程内轮询并消费事件，不涉及数据库操作。
            server.poll(); chat::net::Event event;
            while (server.nextEvent(event)) {
                // 原样回显完整消息，并用收到的消息号关联响应。
                if (event.type == chat::net::EventType::Message && !event.connection->send(event.message.body, {}, event.message.id))
                    std::cerr << "echo send queue full\n";
            }
        }
        server.stop();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
