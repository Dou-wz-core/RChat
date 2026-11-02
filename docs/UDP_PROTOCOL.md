# CHUD / UDP v1 协议规范

## 数据报布局

一个 UDP 数据报只包含一个协议包。所有多字节整数无符号、大端序；禁止直接发送 C++ struct。固定头 44 字节，总长最多 1200 字节，payload 最多 1156 字节；逻辑消息最多 65536 字节、最多 57 个分片。业务 JSON 是 UTF-8，不添加换行分隔符；传输模块自身也能传输包含 NUL 的二进制消息。

| 偏移 | 字节 | 字段 | 含义 |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `CHUD`，即 `43 48 55 44` |
| 4 | 1 | version | 1 |
| 5 | 1 | type | 下表定义 |
| 6 | 2 | flags | v1 必须为 0 |
| 8 | 8 | session | 服务端分配的随机非零会话 ID |
| 16 | 4 | sequence | DATA 分片序号，每方向从 1 递增 |
| 20 | 4 | ack | ACK 确认的分片序号 |
| 24 | 4 | message | DATA 逻辑消息编号，每方向从 1 递增 |
| 28 | 4 | replyTo | 响应关联的对端 message；请求/主动推送为 0 |
| 32 | 2 | fragment | 分片下标，从 0 开始 |
| 34 | 2 | fragments | 总分片数；非末片必须满 1156 字节 |
| 36 | 2 | payloadLength | 实际 payload 字节数 |
| 38 | 2 | headerLength | 必须为 44 |
| 40 | 4 | crc32 | 整个协议包的 CRC32，计算时本字段置 0 |
| 44 | 可变 | payload | 精确等于 payloadLength，禁止尾随字节 |

CRC32 使用反射多项式 `0xEDB88320`，初始 `0xFFFFFFFF`、最终异或 `0xFFFFFFFF`。参考向量：`123456789` 的 CRC 为 `0xCBF43926`。CRC 检错不提供加密或消息认证。

## 包类型与合法字段

| type | 名称 | 字段约束 |
| --- | --- | --- |
| 1 | HELLO | session=0；payload=8 字节大端非零 client nonce |
| 2 | WELCOME | session 非零；payload 原样回显 nonce |
| 3 | DATA | session、sequence、message 非零；ack=0；合法分片和非空 payload |
| 4 | ACK | session、ack 非零；payload 为空 |
| 5 | PING | session 非零；payload 为空 |
| 6 | PONG | session 非零；payload 为空 |
| 7 | CLOSE | session 非零；payload 为空 |

除 DATA 以外，sequence、message、replyTo、fragment、fragments 均为 0；除 ACK 以外 ack=0。HELLO/WELCOME 以外的控制包无 payload。不满足长度、版本、保留位、CRC、类型或字段组合的包直接丢弃，不向未知来源回复错误。

## 握手与身份

1. 客户端从绑定的 UDP socket 向服务端发送 HELLO。
2. 服务端在会话和事件额度允许时分配 session，并返回 WELCOME。
3. 同一源 IP/端口、同一 nonce 的 HELLO 重传复用原 session；WELCOME 丢失不会产生重复会话。
4. 客户端只接受指定服务端 IP/端口且 nonce 匹配的 WELCOME。此后两端均检查 session 和源 IP/端口。
5. 传输握手不等于业务登录。LOGIN 成功后将用户 ID 绑定到 Connection；其余业务必须使用该身份，禁止伪造 JSON `id`。

IPv4 地址必须使用数字形式。v1 不支持 NAT 地址迁移，地址变化或重连需要创建新 UdpNode/会话。session 和 nonce 由 `std::random_device` 生成，不得视作抗监听的安全认证；本版用于本地/可信局域网开发，公网接入需独立的认证加密、抗重放及地址验证设计。

## 可靠传输状态机

每方向独立停等 ARQ：队首消息按序分片，只允许一个 DATA 分片在途。收到恰好匹配当前 sequence 的 ACK 后推进；发送方保留完整消息到最后一片确认，重传同一个 sequence 和原始 payload。双向发送互不阻塞。

接收方只接收期望 sequence、message、fragment；不符合期望的未来包丢弃。旧 sequence 只补发 ACK，不再组装或投递。分片数、replyTo 必须在整条消息内一致；总组装量不能超过 64KiB。最后一片只有在完整消息进入有界业务事件队列后才 ACK；队列满则保持状态不变，等待重传。

序号不回绕，耗尽时关闭会话。ACK 是单个分片确认，不是累计 ACK；没有 DATA 的旧式换行拆包、选择确认窗口或滑动窗口。

默认首个重传间隔 250ms，按 2 倍退避，上限 2s；总发送次数（含首次）最多 7 次，失败关闭会话并让所有未完成发送报告失败。HELLO 同样退避。相邻新 DATA 最少间隔 2ms。每 5s 发送心跳；20s 未收到该会话合法包则关闭。PING 回复 PONG；CLOSE 尽力发送一次，丢失由超时兜底。使用 steady_clock，避免系统时间调整影响超时。

停等和退避是此开发版本的基础负载控制，尚未实现 RTT 自适应、路径 MTU 探测或完善的公网拥塞控制。1200 字节是固定开发上限，不承诺适合任意路径；部署前需根据实际 MTU 和网络条件验证。

## 队列与线程约束

- 最多 256 个活跃会话；每会话最多 32 条待发消息、256KiB 待发体；全局待发体最多 4MiB。
- 每会话最多组装一条 64KiB 消息；活跃会话额度限制重组内存。
- UdpNode 最多 1024 个事件，消息体合计最多 2MiB；关闭事件及发送完成回调提前预留事件槽位。业务停滞时不会无限堆积控制事件或消息。
- ReliableUdp 是单所有者状态机，回调不可重入。UdpNode 使用 mutex 包装它；一个线程 poll，业务线程 nextEvent/send。Connection::send 可跨线程调用。
- 业务/数据库只在业务线程执行；网络线程不执行用户回调。Delivered 事件由消费者调用 completion(success)。
- send 返回 true 仅表示进入发送队列；false 表示消息为空、超过上限、队列满或会话已关闭。true 后的失败通过完成回调通知。

## 交付语义与业务兼容

传输 ACK 表示分片已被接收，最终 ACK 表示完整消息进入接收端内存事件队列。它不承诺业务执行、事务落库、磁盘持久化或用户已读。会话存续期间重复包不会重复调用业务 Handler。ACK 丢失后最终失败仍可能出现“接收方收到、发送方不确定”的情况；离线补偿可能使应用收到重复内容。跨会话/跨进程 exactly-once 需要持久化业务请求 ID 和事务设计，不在 v1 承诺范围内。

原 `EnMsgType` 0..15 编号保持；新增 16=OFFLINE_PULL_MSG，17=OFFLINE_BATCH_MSG，18=ONE_CHAT_MSG_ACK。响应 replyTo 关联请求传输 message；单聊和群聊主动转发 replyTo=0。单聊 `queued/stored`、群聊 `accepted` 都只是受理结果。

业务请求体限制为 8192 字节、JSON 嵌套最多 32 层，协议库仍支持 64KiB。登录响应携带第一批 `offlinemsg`（原 JSON 字符串数组格式），并提供 `offline_more`。每批离线消息的 JSON 编码体最多约 32KiB、查询最多 50 条；上一批确认前的拉取返回忙。客户端在确认后用 `{"msgid":16}` 拉取后续批次。ACK 完成回调按本批数据库主键删除；失败保留，本批查询后新到的消息不会被整批误删。好友列表仍复用原业务逻辑，完整登录响应超过 64KiB 时明确拒绝并回滚在线状态，后续可增加好友分页接口。

## 源码导航

`protocol.cpp` 处理格式和校验；`reliable_udp.cpp` 处理握手、分片与 ARQ；`udp_socket.cpp` 处理 WinSock/POSIX；`udp_node.cpp` 处理线程边界、事件队列和 Connection 适配；`chatserver.cpp` 处理 JSON 校验、身份检查与业务分发。旧 TCP 示例仅保留在 test/testmuduo，不参与新构建。
