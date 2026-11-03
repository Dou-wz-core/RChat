#include "chatserver.hpp"
#include "chatservice.hpp"
#include "common/log.hpp"
#include <thread>
#include <limits>

using namespace chat::net;
namespace {
// JSON 整数可能采用有符号或无符号表示，先检查范围，再转换为业务使用的 int。
int positiveId(const json& value) {
    if (!value.is_number_integer()) throw std::invalid_argument("expected integer id");
    if (value.is_number_unsigned()) {
        const auto n = value.get<std::uint64_t>();
        if (n == 0 || n > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::invalid_argument("id out of range");
        return static_cast<int>(n);
    }
    const auto n = value.get<std::int64_t>();
    if (n <= 0 || n > std::numeric_limits<int>::max()) throw std::invalid_argument("id out of range");
    return static_cast<int>(n);
}
void validate(const json& request, int type) {
    // 按请求类型校验必填标识和字符串字节数，避免无效参数进入数据模型。
    auto integer = [&](const char* key) {
        if (!request.contains(key)) throw std::invalid_argument(std::string("missing id: ") + key);
        positiveId(request[key]);
    };
    auto text = [&](const char* key, std::size_t limit) {
        if (!request.contains(key) || !request[key].is_string() || request[key].get_ref<const std::string&>().empty() ||
            request[key].get_ref<const std::string&>().size() > limit)
            throw std::invalid_argument(std::string("missing/invalid/oversized string: ") + key);
    };
    switch (type) {
    case LOGIN_MSG: integer("id"); text("password", 128); break;
    case REG_MSG: text("name", 64); text("password", 128); break;
    case ONE_CHAT_MSG: integer("to"); break;
    case ADD_FRIEND_MSG: integer("friendid"); break;
    case CREATE_GROUP_MSG: text("groupname", 128); text("groupdesc", 512); break;
    case ADD_GROUP_MSG: case GROUP_CHAT_MSG: integer("groupid"); break;
    default: break;
    }
}
}
ChatServer::ChatServer(Endpoint local) : node_(true, std::move(local)) {}
void ChatServer::onMessage(const Event& event) {
    // 排队期间会话可能已经关闭，此时不再执行过期请求。
    if (!event.connection->alive()) return;
    try {
        // 传输层允许 64KiB 消息，业务请求另限 8KiB，并限制 JSON 的嵌套深度。
        if (event.message.body.size() > 8192) throw std::invalid_argument("business request exceeds 8192 bytes");
        json request = json::parse(event.message.body, [](int depth, json::parse_event_t, json&) {
            if (depth > 32) throw std::invalid_argument("JSON nesting exceeds 32 levels");
            return true;
        });
        if (!request.is_object() || !request.contains("msgid") || !request["msgid"].is_number_integer())
            throw std::invalid_argument("message must be an object with integer msgid");
        const int type = positiveId(request["msgid"]);
        if (type != LOGIN_MSG && type != REG_MSG) {
            // 发送者以登录会话为准；可省略 id，但显式填写其他用户的 id 会被拒绝。
            const int user = event.connection->userId();
            if (user < 0) throw std::invalid_argument("login required");
            if (request.contains("id") && positiveId(request["id"]) != user)
                throw std::invalid_argument("sender id does not match session");
            request["id"] = user;
        } else if (event.connection->userId() >= 0) throw std::invalid_argument("logout before login/register");
        validate(request, type);
        // 将传输消息号传入请求上下文，使业务响应能关联到本次请求。
        ChatService::instance()->getHandler(type)(event.connection, request, {event.message.id});
    } catch (const std::exception& error) {
        json response = {{"msgid", ERROR_MSG}, {"errno", 1}, {"errmsg", error.what()}};
        if (!event.connection->send(response.dump(), {}, event.message.id)) LOG_ERROR << "error response send rejected";
    }
}
void ChatServer::handle(Event& event) {
    // 完成回调也在业务线程执行，发送失败落库和离线批次删除与业务请求串行处理。
    switch (event.type) {
    case EventType::Message: onMessage(event); break;
    case EventType::Closed: if (event.connection) ChatService::instance()->clientClose(event.connection); break;
    case EventType::Delivered: if (event.completion) event.completion(event.success); break;
    case EventType::Opened: LOG_INFO << "UDP session established " << event.connection->id(); break;
    }
}
void ChatServer::run(volatile std::sig_atomic_t& running) {
    // 先初始化业务和数据库状态，成功后才启动网络线程接收请求。
    ChatService::instance();
    std::exception_ptr networkError;
    std::thread network([&] {
        try { while (!node_.stopped()) node_.poll(); }
        catch (...) { networkError = std::current_exception(); node_.stop(); }
    });
    LOG_INFO << "ChatServer UDP listening " << node_.localEndpoint().text();
    // 主线程消费事件，同步数据库访问不会阻塞网络线程处理 ACK 和重传。
    try {
        while (running && !node_.stopped()) { Event event; if (node_.nextEvent(event, 20)) handle(event); }
    } catch (...) { node_.stop(); network.join(); ChatService::instance()->reset(); throw; }
    // 等待网络线程退出后再读取其异常，并处理关闭时生成的剩余事件。
    node_.stop(); network.join();
    Event event;
    while (node_.nextEvent(event)) handle(event); // 在业务线程执行失败回调，保存未交付的消息。
    ChatService::instance()->reset();
    if (networkError) std::rethrow_exception(networkError);
}
