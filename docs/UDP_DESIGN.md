# UDP 改造设计与实施计划

## 目标与解耦

本地项目改为 C++17 自封装 UDP 通信；保留 JSON 业务体、`EnMsgType`、Handler 注册方式和 MySQL/ORM 业务模型。

原耦合点：

| 原实现 | 改造 |
| --- | --- |
| `TcpServer`、`Buffer::findEOL` | 独立 UDP socket、二进制协议编解码 |
| Handler 参数 `TcpConnectionPtr`、`Timestamp` | 传输无关 `ConnectionPtr`、标准库时间 |
| `_userConnMap` 持有 TCP 连接 | 持有会话抽象，发送统一走 `Connection::send` |
| TCP close 回调 | UDP 心跳超时、重传失败及主动关闭事件 |
| 业务回调内同步 SQL | 网络线程维护收发和定时器，主线程串行业务事件 |
| muduo 日志和启动循环 | 标准库日志、自封装 UDP 运行循环 |

## 实施顺序

1. 明确协议字节布局、状态机、资源边界及交付语义。
2. 实现可独立测试的协议 codec、可靠会话状态机和 Windows/Linux socket。
3. 实现线程安全发送、有限事件队列、客户端和服务端运行封装。
4. 将原业务适配到连接抽象；处理身份绑定和离线消息交付确认。
5. 增加 C++ UDP 客户端、无需数据库的演示服务和测试；本地编译完整服务端。

## 选型

网络模块不再依赖 muduo；通过 WinSock/POSIX 实现 IPv4 UDP。v1 使用每个会话、每个方向独立的停等 ARQ：一个数据分片在途；ACK 后才发送下一个，天然保持同方向消息顺序。选择这一方案是为了使协议状态、错误处理和解析过程容易阅读及测试；吞吐量不是 v1 的优化目标。

所有业务消息均支持分片，避免将登录响应中的离线消息和好友列表塞进一个大 UDP 数据报。接收方只有在完整消息进入有限业务队列后才确认最后一个分片；队列满时不确认，由发送方退避重试。数据库不会在网络线程执行。

协议的 ACK 表示接收端已经接收分片（最后一片意味着消息已进入内存业务队列），不等于数据库事务成功或终端用户已读。会话内相同序号只投递一次；进程重启或建立新会话后的业务重试不提供持久化 exactly-once。

## 实施结果

上述五个步骤已经落地：协议见 [UDP_PROTOCOL.md](UDP_PROTOCOL.md)，独立网络模块在 `include/net`、`src/net`；业务适配在 `chatserver.cpp` 和 `chatservice.cpp`；本地构建与运行见 [README](../README.md)，验证证据和边界见 [VALIDATION.md](VALIDATION.md)。

最终将 muduo 从活动代码和构建依赖中移除，保留历史 TCP 示例。离线表增加消息主键以实现确认后按快照删除；提供本地 schema 和旧表副本迁移脚本。数据库配置改用环境变量，UTF-8 和动态 SQL 转义适配分片后的较大消息。

技术依据：[RFC 8085](https://www.rfc-editor.org/rfc/rfc8085.html)，[POSIX/Linux recvfrom](https://man7.org/linux/man-pages/man2/recvfrom.2.html)，[WinSock recvfrom](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recvfrom)。
