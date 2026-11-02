# 本地验证记录

日期：2026-11-02。平台：Windows x64，MSVC 19.51.36260，C++17，MySQL 8.0.46。没有向 VMware 项目写入文件，构建、运行和临时数据均在本地进行。

| 验证项 | 结果与证据 |
| --- | --- |
| 完整构建 | `scripts/build.ps1` 构建 chat_udp、ChatServer、UdpClient、UdpEchoServer 和两个测试程序成功 |
| 协议/网络 CTest | `udp_protocol_and_transport` 通过；覆盖格式、CRC、丢包、乱序/重复、64KiB 分片、背压、资源限制、超时、真实 socket 和线程边界 |
| 业务适配 CTest | `udp_chat_business_adapter` 通过；真实网络与业务类，内存模型隔离数据库；覆盖注册、登录、鉴权、单/群聊、离线确认、失败保留、整数溢出和 JSON 深度限制 |
| 独立网络配置 | `CHAT_BUILD_SERVER=OFF` 在独立 build-transport 中配置、构建与 CTest 成功，客户端最终版本也完成重建 |
| 命令行 UTF-8 | UdpClient → UdpEchoServer 实测中文和 emoji 参数，JSON 内容精确回显 |
| 交互控制台 | 实际控制台输入中文/emoji，收到 `reply_to=1` 回显，`/quit` 返回 0；使用 Unicode 控制台 API，不改系统代码页 |
| MySQL 运行依赖 | 验证 libmysql 依赖，并自动复制 libssl/libcrypto；ChatServer 可启动且在数据库不可达时明确报错退出 |
| 真正 MySQL 端到端 | build 下初始化独立临时数据目录；MySQL 仅监听 127.0.0.1:60633，ChatServer 监听 60634；实际 schema、注册/登录、好友/群组、UTF-8/引号 SQL、分片单聊、群聊、离线存储与确认删除、注销全部通过 |
| 临时资源清理 | 临时客户端/服务端/MySQL 进程已结束，mysql-smoke-data 数据目录已删除；未使用已有数据库 |

完整 CTest 最后一次结果为 **2/2 通过**。详细输出在本地 `build/Testing/Temporary/LastTest.log`，完整构建日志在 `build/verification.log`。独立模块日志为 `build/transport-only.log`。MySQL 端到端记录 `build/mysql-smoke-result.log` 包含 `MYSQL_UDP_END_TO_END_OK` 和 `TEMPORARY_MYSQL_DATA_CLEANED`。这些构建日志不属于版本化源文件。

## 验证边界

Linux/POSIX 分支没有在 Linux 主机编译或实测；本轮所有运行证据来自 Windows。没有测量吞吐量、长时间压力、多机公网或 NAT 迁移行为。v1 是有明确资源上限的停等协议，不能把这些测试等同于公网生产认证、持久化 exactly-once 或完整拥塞控制验证。

原业务仍有其原有的产品范围和约束，例如单实例在线状态、用户密码模型、好友和群组业务规则；本次重点是 UDP 协议和网络边界，没有扩展成完整的生产业务系统。
