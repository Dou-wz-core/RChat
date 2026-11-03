#include "chatserver.hpp"
#include "net/cli.hpp"
#include "common/settings.hpp"
#include "demo_runtime.hpp"
#include <csignal>
#include <iostream>

// 信号处理函数只设置退出标志，由服务循环完成会话关闭和业务状态清理。
namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: ChatServer [bind-ip] [port]");
        
        // 读取配置文件，确保数据库存在并初始化；注册信号处理函数，启动服务循环。
        chat::settings::loadForExecutable(argv[0]);
        ensureDemoDatabase();
        // 处理 SIGINT 和 SIGTERM 信号，优雅退出服务循环。
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);

        // 运行服务循环，直到收到退出信号；异常退出时打印错误信息并返回非零状态码。
        ChatServer server({argc > 1 ? argv[1] : chat::settings::get("CHAT_BIND_IP", "127.0.0.1"),
            chat::net::parsePort(argc > 2 ? argv[2] : chat::settings::get("CHAT_BIND_PORT", "6000"))});
        server.run(running);

    } 
    
    catch (const std::exception& error) 
    { std::cerr << error.what() << '\n'; return 1; }
}
