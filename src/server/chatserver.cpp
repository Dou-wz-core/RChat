#include "chatserver.hpp"
#include "chatservice.hpp"
#include "common/log.hpp"
#include <thread>
#include <limits>

using namespace chat::net;
namespace {
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
    if (!event.connection->alive()) return;
    try {
        if (event.message.body.size() > 8192) throw std::invalid_argument("business request exceeds 8192 bytes");
        json request = json::parse(event.message.body, [](int depth, json::parse_event_t, json&) {
            if (depth > 32) throw std::invalid_argument("JSON nesting exceeds 32 levels");
            return true;
        });
        if (!request.is_object() || !request.contains("msgid") || !request["msgid"].is_number_integer())
            throw std::invalid_argument("message must be an object with integer msgid");
        const int type = positiveId(request["msgid"]);
        if (type != LOGIN_MSG && type != REG_MSG) {
            const int user = event.connection->userId();
            if (user < 0) throw std::invalid_argument("login required");
            if (request.contains("id") && positiveId(request["id"]) != user)
                throw std::invalid_argument("sender id does not match session");
            request["id"] = user;
        } else if (event.connection->userId() >= 0) throw std::invalid_argument("logout before login/register");
        validate(request, type);
        ChatService::instance()->getHandler(type)(event.connection, request, {event.message.id});
    } catch (const std::exception& error) {
        json response = {{"msgid", ERROR_MSG}, {"errno", 1}, {"errmsg", error.what()}};
        if (!event.connection->send(response.dump(), {}, event.message.id)) LOG_ERROR << "error response send rejected";
    }
}
void ChatServer::handle(Event& event) {
    switch (event.type) {
    case EventType::Message: onMessage(event); break;
    case EventType::Closed: if (event.connection) ChatService::instance()->clientClose(event.connection); break;
    case EventType::Delivered: if (event.completion) event.completion(event.success); break;
    case EventType::Opened: LOG_INFO << "UDP session established " << event.connection->id(); break;
    }
}
void ChatServer::run(volatile std::sig_atomic_t& running) {
    ChatService::instance();
    std::exception_ptr networkError;
    std::thread network([&] {
        try { while (!node_.stopped()) node_.poll(); }
        catch (...) { networkError = std::current_exception(); node_.stop(); }
    });
    LOG_INFO << "ChatServer UDP listening " << node_.localEndpoint().text();
    try {
        while (running && !node_.stopped()) { Event event; if (node_.nextEvent(event, 20)) handle(event); }
    } catch (...) { node_.stop(); network.join(); ChatService::instance()->reset(); throw; }
    node_.stop(); network.join();
    Event event;
    while (node_.nextEvent(event)) handle(event); // delivery failures are persisted on the business thread
    ChatService::instance()->reset();
    if (networkError) std::rethrow_exception(networkError);
}
