#include "net/protocol.hpp"
#include "net/reliable_udp.hpp"
#include "net/udp_node.hpp"
#include "net/udp_socket.hpp"
#include "common/settings.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

using namespace chat::net;
using namespace std::string_literals;
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(std::string(__func__) + ": " #condition); } while (false)
void localConfiguration() {
    const auto file = std::filesystem::current_path() / ("settings-test-" + std::to_string(randomToken()) + ".env");
    const auto key = "CHAT_TEST_" + std::to_string(randomToken());
    const auto previous = std::getenv(key.c_str());
    const bool existed = previous != nullptr;
    const std::string oldValue = previous ? previous : "";
    auto set = [&](const std::string& value) {
#ifdef _WIN32
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 1);
#endif
    };
    auto cleanup = [&] {
#ifdef _WIN32
        _putenv_s(key.c_str(), existed ? oldValue.c_str() : "");
#else
        if (existed) setenv(key.c_str(), oldValue.c_str(), 1); else unsetenv(key.c_str());
#endif
        std::filesystem::remove(file);
    };
    try {
        { std::ofstream out(file); out << "\xef\xbb\xbf# local test\n" << key << " = \"value # = !\"\r\nCHAT_TEST_EMPTY=\n"; }
        chat::settings::load(file);
        CHECK(chat::settings::get(key.c_str()) == "value # = !");
        CHECK(chat::settings::get("CHAT_TEST_EMPTY", "fallback").empty());
        set("environment"); CHECK(chat::settings::get(key.c_str()) == "environment");
        { std::ofstream out(file); out << "PATH=not-allowed\n"; }
        bool rejected = false;
        try { chat::settings::load(file); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
        { std::ofstream out(file); out << key << "='missing end\n"; }
        rejected = false;
        try { chat::settings::load(file); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    } catch (...) { cleanup(); throw; }
    cleanup();
}

// 校验标准 CRC 值、字段字节序、二进制载荷，以及被修改或截断的报文是否遭到拒绝。
void codec() {
    const std::string reference = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(reference.data()), reference.size()) == 0xcbf43926u);
    Packet p; p.session = 0x0102030405060708ull; p.sequence = 0x11223344; p.message = 1;
    p.replyTo = 9; p.fragments = 1; p.payload = {'a', 0, '\n', 0xff};
    auto bytes = encode(p);
    CHECK(bytes.size() == 48 && bytes[8] == 1 && bytes[15] == 8 && bytes[16] == 0x11 && bytes[19] == 0x44);
    CHECK(bytes[36] == 0 && bytes[37] == 4 && bytes[39] == 44);
    Packet out; CHECK(decode(bytes, out));
    CHECK(out.payload == p.payload && out.session == p.session && out.replyTo == 9);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto corrupt = bytes; corrupt[i] ^= 1; CHECK(!decode(corrupt, out));
    }
    for (std::size_t length = 0; length < bytes.size(); ++length) CHECK(!decode(bytes.data(), length, out));
    auto extra = bytes; extra.push_back(0); CHECK(!decode(extra, out));
    p.fragments = 0;
    bool threw = false; try { encode(p); } catch (const std::invalid_argument&) { threw = true; } CHECK(threw);
    for (auto type : {PacketType::Hello, PacketType::Welcome, PacketType::Ack, PacketType::Ping, PacketType::Pong, PacketType::Close}) {
        Packet control; control.type = type;
        if (type != PacketType::Hello) control.session = 42;
        if (type == PacketType::Hello || type == PacketType::Welcome) control.payload = encodeNonce(123);
        if (type == PacketType::Ack) control.ack = 1;
        CHECK(decode(encode(control), out) && out.type == type);
    }
    // 重新计算 CRC 后仍须拒绝非法版本、类型、保留标志和首部长度。
    for (int offset : {4, 5, 6, 38}) {
        auto invalid = bytes; invalid[static_cast<std::size_t>(offset)] = 255;
        std::fill(invalid.begin() + 40, invalid.begin() + 44, std::uint8_t{0});
        auto crc = crc32(invalid.data(), invalid.size());
        for (int n = 0; n < 4; ++n) invalid[40 + n] = static_cast<std::uint8_t>(crc >> ((3 - n) * 8));
        CHECK(!decode(invalid, out));
    }
}

// 用内存队列连接两端状态机，手动推进毫秒时间，稳定复现丢包和重传过程。
struct Simulation {
    struct Wire { bool toServer; Bytes bytes; };
    Endpoint clientAddress{"127.0.0.1", 41001}, serverAddress{"127.0.0.1", 41002};
    std::deque<Wire> wire;
    std::unique_ptr<ReliableUdp> client, server;
    std::uint64_t now = 0, clientId = 0, serverId = 0;
    std::vector<ReceivedMessage> atServer, atClient;
    unsigned completed = 0, failed = 0, closed = 0, reject = 0, duplicates = 0, dropped = 0;
    bool loss = false, blackhole = false;
    std::map<std::tuple<bool, unsigned, unsigned>, unsigned> seen;
    explicit Simulation(bool lossy = false) : loss(lossy) {
        TransportConfig config; config.initialRtoMs = 10; config.maxRtoMs = 40; config.maxAttempts = 6;
        config.heartbeatMs = 100; config.idleTimeoutMs = 1000; config.pacingMs = 1;
        auto send = [this](bool toServer, const Bytes& bytes) {
            if (blackhole) return;
            Packet p; CHECK(decode(bytes, p));
            auto attempt = seen[{toServer, static_cast<unsigned>(p.type), p.sequence ? p.sequence : p.ack}]++;
            // 仅丢弃指定报文的第一次发送，覆盖握手应答、数据片和 ACK 的重传路径。
            if (loss && attempt == 0 && (p.type == PacketType::Hello || p.type == PacketType::Welcome ||
                (p.type == PacketType::Data && p.sequence % 3 == 0) || (p.type == PacketType::Ack && p.ack % 4 == 0))) { ++dropped; return; }
            wire.push_back({toServer, bytes});
            if (loss && p.type == PacketType::Data && p.sequence % 5 == 0) {
                // 副本插到队首，使重复分片先于原队列中的其他报文到达。
                wire.push_front({toServer, bytes}); ++duplicates;
            }
        };
        ReliableUdp::Callbacks sc;
        sc.opened = [this](std::uint64_t id, const Endpoint&) { serverId = id; return true; };
        sc.received = [this](std::uint64_t, ReceivedMessage m) {
            // 主动拒收若干次完整消息，模拟业务队列暂时没有容量。
            if (reject) { --reject; return false; }
            atServer.push_back(std::move(m)); return true;
        };
        sc.delivered = [this](std::uint64_t, std::uint32_t, bool ok) { ok ? ++completed : ++failed; };
        sc.closed = [this](std::uint64_t, const std::string&) { ++closed; };
        ReliableUdp::Callbacks cc = sc;
        cc.opened = [this](std::uint64_t id, const Endpoint&) { clientId = id; return true; };
        cc.received = [this](std::uint64_t, ReceivedMessage m) { atClient.push_back(std::move(m)); return true; };
        server = std::make_unique<ReliableUdp>(true, [send](const Endpoint&, const Bytes& b) { send(false, b); }, sc, config);
        client = std::make_unique<ReliableUdp>(false, [send](const Endpoint&, const Bytes& b) { send(true, b); }, cc, config);
        client->connect(serverAddress, 1234567, now);
    }
    void advance(unsigned ms) {
        // 两端共享模拟时钟；每步先处理定时器，再交付排队的数据报。
        for (unsigned i = 0; i < ms; ++i) {
            ++now; client->tick(now); server->tick(now);
            unsigned limit = 1000;
            while (!wire.empty() && limit--) {
                auto packet = std::move(wire.front()); wire.pop_front();
                if (packet.toServer) server->input(clientAddress, packet.bytes, now);
                else client->input(serverAddress, packet.bytes, now);
            }
            CHECK(limit > 0);
        }
    }
    void handshake() { advance(100); CHECK(clientId && clientId == serverId); }
};
// 在丢包、重复和队列顺序变化下，验证 64KiB 分片重组、双向消息关联和顺序交付。
void reliableDelivery() {
    Simulation link(true); link.handshake();
    std::string large(MaxMessage, 'x');
    for (std::size_t i = 0; i < large.size(); ++i) large[i] = static_cast<char>(i % 256);
    CHECK(link.client->enqueue(link.clientId, large) == 1);
    CHECK(link.client->enqueue(link.clientId, "second\nmessage") == 2);
    CHECK(link.server->enqueue(link.serverId, "server-push", 42) == 1);
    link.advance(5000);
    CHECK(link.atServer.size() == 2 && link.atServer[0].body == large && link.atServer[1].body == "second\nmessage");
    CHECK(link.atClient.size() == 1 && link.atClient[0].replyTo == 42);
    CHECK(link.completed == 3 && !link.failed && link.dropped > 0 && link.duplicates > 0);
    CHECK(link.client->queuedBytes() == 0 && link.server->queuedBytes() == 0);
}
// 完整消息被拒收后应可重试且只投递一次，发送队列满时拒绝继续入队。
void backpressureAndLimits() {
    Simulation link; link.handshake(); link.reject = 3;
    CHECK(!link.client->enqueue(link.clientId, ""));
    CHECK(!link.client->enqueue(link.clientId, std::string(MaxMessage + 1, 'x')));
    CHECK(!link.client->enqueue(999, "unknown"));
    CHECK(link.client->enqueue(link.clientId, "only-once"));
    link.advance(250);
    CHECK(link.atServer.size() == 1 && link.completed == 1 && !link.failed);
    for (int i = 0; i < 32; ++i) CHECK(link.client->enqueue(link.clientId, "queued"));
    CHECK(!link.client->enqueue(link.clientId, "overflow"));
    link.client->closeAll(); CHECK(link.client->queuedBytes() == 0 && link.failed == 32);
}
// 拒绝错误地址、会话号和未来序号，分别检查重传、心跳和握手超时后的关闭结果。
void peerValidationAndTimeout() {
    Simulation link; link.handshake();
    Packet p; p.session = link.serverId; p.sequence = 1; p.message = 1; p.fragments = 1; p.payload = {'x'};
    link.server->input({"127.0.0.1", 49999}, encode(p), link.now);
    CHECK(link.atServer.empty());
    p.sequence = 2; link.server->input(link.clientAddress, encode(p), link.now); CHECK(link.atServer.empty());
    p.sequence = 1; ++p.session; link.server->input(link.clientAddress, encode(p), link.now); CHECK(link.atServer.empty());
    link.blackhole = true;
    CHECK(link.client->enqueue(link.clientId, "never delivered"));
    link.advance(500);
    CHECK(!link.client->sessionCount() && link.failed == 1 && link.closed >= 1);
    Simulation idle; idle.handshake(); idle.blackhole = true; idle.advance(1100);
    CHECK(!idle.server->sessionCount() && !idle.client->sessionCount());
    Simulation timeout; timeout.blackhole = true; timeout.wire.clear(); timeout.advance(500); CHECK(timeout.closed == 1 && !timeout.clientId);
}
// 检查同一消息的分片元数据一致性，以及会话数量、单会话和全局字节数上限。
void fragmentAndResourceLimits() {
    Simulation link; link.handshake();
    Packet first; first.session = link.serverId; first.sequence = 1; first.message = 1;
    first.fragments = 2; first.replyTo = 99; first.payload.assign(MaxPayload, 'a');
    link.server->input(link.clientAddress, encode(first), link.now);
    link.server->input(link.clientAddress, encode(first), link.now); // 重复的旧分片不能再次追加到组装缓冲。
    Packet last = first; last.sequence = 2; last.fragment = 1; last.payload = {'b'};
    last.fragments = 3; last.payload.assign(MaxPayload, 'b');
    link.server->input(link.clientAddress, encode(last), link.now); // 后续分片声明的总片数与首片不一致。
    CHECK(link.atServer.empty());
    last.fragments = 2; last.payload = {'b'}; last.replyTo = 100;
    link.server->input(link.clientAddress, encode(last), link.now); CHECK(link.atServer.empty());
    last.replyTo = 99; link.server->input(link.clientAddress, encode(last), link.now);
    CHECK(link.atServer.size() == 1 && link.atServer[0].body == std::string(MaxPayload, 'a') + 'b');
    link.server->input(link.clientAddress, encode(last), link.now); CHECK(link.atServer.size() == 1);

    TransportConfig config; config.maxSessions = 2; config.maxPeerBytes = 8; config.maxTotalBytes = 10;
    std::vector<std::uint64_t> ids; unsigned failures = 0;
    ReliableUdp::Callbacks callbacks;
    callbacks.opened = [&](std::uint64_t id, const Endpoint&) { ids.push_back(id); return true; };
    callbacks.delivered = [&](std::uint64_t, std::uint32_t, bool ok) { if (!ok) ++failures; };
    ReliableUdp server(true, [](const Endpoint&, const Bytes&) {}, callbacks, config);
    Packet hello; hello.type = PacketType::Hello;
    for (unsigned i = 1; i <= 3; ++i) {
        hello.payload = encodeNonce(i); server.input({"127.0.0.1", static_cast<std::uint16_t>(42000 + i)}, encode(hello), 1);
    }
    CHECK(ids.size() == 2 && server.sessionCount() == 2);
    CHECK(server.enqueue(ids[0], "12345678"));
    CHECK(!server.enqueue(ids[0], "x")); // 该会话的待发字节数已经达到上限。
    CHECK(!server.enqueue(ids[1], "123")); // 两个会话合计将超过全局待发字节数上限。
    CHECK(server.enqueue(ids[1], "12")); CHECK(server.queuedBytes() == 10);
    server.closeAll(); CHECK(failures == 2 && server.queuedBytes() == 0);
}
// 故意不消费服务端事件，填满消息队列后仍应能生成预留的关闭事件。
void nodeEventBackpressure() {
    TransportConfig config; config.pacingMs = 0;
    UdpNode server(true, {"127.0.0.1", 0}, config);
    UdpNode client(false, {"127.0.0.1", 0}, config);
    client.connect(server.localEndpoint()); ConnectionPtr connection;
    auto deadline = monotonicMs() + 3000;
    while (!connection && monotonicMs() < deadline) {
        client.poll(1); server.poll(1); Event event;
        while (client.nextEvent(event)) if (event.type == EventType::Opened) connection = event.connection;
    }
    CHECK(connection);
    unsigned rejected = 0;
    deadline = monotonicMs() + 1000;
    while (monotonicMs() < deadline) {
        if (!connection->send("bounded-event-queue")) ++rejected;
        client.poll(0); server.poll(0); client.poll(0);
    }
    CHECK(rejected > 0);
    server.stop(); // 消息队列饱和时，预留槽位仍保证关闭事件可以入队。
    Event event; unsigned count = 0, closed = 0;
    while (server.nextEvent(event)) { ++count; if (event.type == EventType::Closed) ++closed; }
    CHECK(count == 1024 && closed == 1);
    client.stop();
}
// 使用真实回环 UDP 和独立网络线程，检查含换行、空字符的长消息及零字节数据报。
void realSockets() {
    UdpNode server(true, {"127.0.0.1", 0});
    UdpNode client(false, {"127.0.0.1", 0});
    client.connect(server.localEndpoint());
    std::atomic<bool> run{true}; std::exception_ptr networkError;
    std::thread io([&] {
        try { while (run) { server.poll(1); client.poll(1); } }
        catch (...) { networkError = std::current_exception(); run = false; }
    });
    bool received = false, delivered = false; ConnectionPtr connection;
    const std::string payload = std::string(10000, 'z') + "\n\0binary"s;
    auto deadline = monotonicMs() + 5000;
    try {
        while (run && !received && monotonicMs() < deadline) {
            Event event;
            while (client.nextEvent(event, 1)) {
                if (event.type == EventType::Opened) { connection = event.connection; CHECK(connection->send(payload, [&](bool ok) { delivered = ok; })); }
                // 测试消费线程负责执行完成回调，网络线程只维护套接字和状态机。
                if (event.type == EventType::Delivered) event.completion(event.success);
                if (event.type == EventType::Message) { CHECK(event.message.body == payload && event.message.replyTo == 1); received = true; }
            }
            while (server.nextEvent(event)) {
                if (event.type == EventType::Message) CHECK(event.connection->send(event.message.body, {}, event.message.id));
            }
        }
    } catch (...) { run = false; io.join(); throw; }
    run = false; io.join();
    if (networkError) std::rethrow_exception(networkError);
    CHECK(received && delivered && connection && connection->alive());
    client.stop(); server.stop(); CHECK(!connection->alive() && !connection->send("closed"));
    UdpSocket raw({"127.0.0.1", 0}); UdpSocket receiver({"127.0.0.1", 0});
    CHECK(raw.send(receiver.localEndpoint(), {})); CHECK(receiver.waitReadable(1000));
    Endpoint from; Bytes bytes; CHECK(receiver.receive(from, bytes) && bytes.empty());
}
int main() {
    try {
        localConfiguration(); std::cout << "PASS local .env parsing, BOM/quotes, validation and environment precedence\n";
        codec(); std::cout << "PASS codec, byte order, CRC and malformed frames\n";
        reliableDelivery(); std::cout << "PASS lost HELLO/WELCOME/DATA/ACK, duplicate/reordered packets, fragmentation and ordering\n";
        backpressureAndLimits(); std::cout << "PASS queue backpressure, size/queue limits and failure cleanup\n";
        peerValidationAndTimeout(); std::cout << "PASS endpoint/session validation, retransmit and heartbeat/handshake timeout\n";
        fragmentAndResourceLimits(); std::cout << "PASS fragment metadata, session cap and per-peer/global byte caps\n";
        nodeEventBackpressure(); std::cout << "PASS saturated event queue, sender backpressure and reserved close event\n";
        realSockets(); std::cout << "PASS real UDP sockets, separate I/O thread, bidirectional fragmented transfer and zero-byte datagram\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
