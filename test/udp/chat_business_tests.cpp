// 使用真实 ChatServer、ChatService 和 UDP 通信，仅以容器实现替代 MySQL 读写。
// 用户、好友、群成员和离线消息保存在本测试进程内，用于检查业务状态变化。
#include "chatserver.hpp"
#include "chatservice.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <thread>

using namespace chat::net;
#define CHECK(c) do { if (!(c)) throw std::runtime_error(std::string(__func__) + ": " #c); } while(false)
namespace {
std::map<int, User> users;
std::map<int, std::vector<OfflineMsgModel::Message>> inbox;
std::map<int, std::set<int>> members;
std::set<std::pair<int, int>> friends;
std::uint64_t offlineId = 0;
int groupId = 0;
}
// 提供与数据库模型相同的接口，使业务代码无需判断当前是否处于测试环境。
bool UserModel::insert(User& user) { user.setId(static_cast<int>(users.size() + 1)); users[user.getId()] = user; return true; }
User UserModel::query(int id) { auto it = users.find(id); return it == users.end() ? User{} : it->second; }
bool UserModel::updateState(User user) { users[user.getId()] = user; return true; }
bool UserModel::resetState() { for (auto& entry : users) entry.second.setState("offline"); return true; }
void FriendModel::insert(int a, int b) { friends.emplace(a, b); }
std::vector<User> FriendModel::query(int id) { std::vector<User> result; for (auto p : friends) if (p.first == id) result.push_back(users.at(p.second)); return result; }
bool GroupModel::createGroup(Group& group) { group.setId(++groupId); return true; }
void GroupModel::addGroup(int group, int user, const std::string&) { members[group].insert(user); }
std::vector<Group> GroupModel::queryGroups(int) { return {}; }
std::vector<int> GroupModel::queryGroupUsers(int user, int group) { std::vector<int> result; for (int id : members[group]) if (id != user) result.push_back(id); return result; }
bool OfflineMsgModel::insert(int user, const std::string& body) { inbox[user].push_back({++offlineId, body}); return true; }
std::vector<OfflineMsgModel::Message> OfflineMsgModel::queryPending(int user, std::size_t limit) {
    auto result = inbox[user]; if (result.size() > limit) result.resize(limit); return result;
}
bool OfflineMsgModel::acknowledge(int user, const std::vector<std::uint64_t>& ids) {
    auto& messages = inbox[user];
    messages.erase(std::remove_if(messages.begin(), messages.end(), [&](const Message& m) {
        return std::find(ids.begin(), ids.end(), m.id) != ids.end();
    }), messages.end()); return true;
}

// 保存每条响应的 replyTo，区分请求响应和其他客户端发来的主动消息。
struct Client {
    struct Reply { std::uint32_t to; json body; };
    UdpNode node{false, {"127.0.0.1", 0}};
    ConnectionPtr connection;
    std::vector<Reply> replies;
    std::uint32_t sequence = 0;
    void poll() {
        node.poll(1); Event e;
        while (node.nextEvent(e)) {
            if (e.type == EventType::Opened) connection = e.connection;
            if (e.type == EventType::Message) replies.push_back({e.message.replyTo, json::parse(e.message.body)});
            if (e.type == EventType::Delivered && e.completion) e.completion(e.success);
        }
    }
};
std::vector<Client*> clients;
// 持续轮询两个客户端直到条件满足，超时后让断言失败，避免测试无限等待。
template<class Predicate> void until(Predicate predicate) {
    auto deadline = monotonicMs() + 5000;
    while (!predicate() && monotonicMs() < deadline) for (auto client : clients) client->poll();
    CHECK(predicate());
}
json request(Client& client, json body) {
    // 每次发送成功都会分配递增消息号，按同一编号寻找对应响应。
    const auto id = ++client.sequence;
    CHECK(client.connection->send(body.dump()));
    auto found = [&] { return std::find_if(client.replies.begin(), client.replies.end(), [&](const Client::Reply& r) { return r.to == id; }); };
    until([&] { return found() != client.replies.end(); });
    auto it = found(); auto response = it->body; client.replies.erase(it); return response;
}

void businessIntegration() {
    // 服务端独立运行，测试线程驱动两个客户端完成真实请求和消息转发。
    ChatServer server({"127.0.0.1", 0});
    volatile std::sig_atomic_t running = 1;
    std::exception_ptr failure;
    std::thread service([&] { try { server.run(running); } catch (...) { failure = std::current_exception(); } });
    try {
        Client alice, bob; clients = {&alice, &bob};
        alice.node.connect(server.localEndpoint()); bob.node.connect(server.localEndpoint());
        until([&] { return alice.connection && bob.connection; });
        // 未登录不能聊天；登录后也不能通过请求中的 id 冒充另一用户。
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", 2}})["errno"] == 1);
        const int a = request(alice, {{"msgid", REG_MSG}, {"name", "alice"}, {"password", "test"}})["id"];
        const int b = request(bob, {{"msgid", REG_MSG}, {"name", "bob"}, {"password", "test"}})["id"];
        CHECK(request(alice, {{"msgid", LOGIN_MSG}, {"id", a}, {"password", "test"}})["errno"] == 0);
        CHECK(request(bob, {{"msgid", LOGIN_MSG}, {"id", b}, {"password", "test"}})["errno"] == 0);
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"id", b}, {"to", b}, {"msg", "spoof"}})["errno"] == 1);
        const std::string text(6000, 'x'); // 超过单个数据报容量，经分片重组后再交给聊天业务。
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", b}, {"msg", text}})["status"] == "queued");
        until([&] { return !bob.replies.empty(); });
        CHECK(bob.replies.back().body["msg"] == text && bob.replies.back().body["id"] == a);
        bob.replies.clear();
        CHECK(request(alice, {{"msgid", ADD_FRIEND_MSG}, {"friendid", b}})["errno"] == 0);
        const int group = request(alice, {{"msgid", CREATE_GROUP_MSG}, {"groupname", "udp"}, {"groupdesc", "test"}})["groupid"];
        CHECK(request(bob, {{"msgid", ADD_GROUP_MSG}, {"groupid", group}})["errno"] == 0);
        CHECK(request(alice, {{"msgid", GROUP_CHAT_MSG}, {"groupid", group}, {"msg", "group"}})["errno"] == 0);
        until([&] { return !bob.replies.empty(); }); CHECK(bob.replies.back().body["msg"] == "group"); bob.replies.clear();
        // 接收者退出登录后消息落入离线收件箱，重新登录获取并确认首批消息。
        CHECK(request(bob, {{"msgid", LOGOUT_MSG}})["errno"] == 0);
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", b}, {"msg", "offline"}})["status"] == "stored");
        auto login = request(bob, {{"msgid", LOGIN_MSG}, {"id", b}, {"password", "test"}});
        CHECK(login["offlinemsg"].size() == 1);
        CHECK(json::parse(login["offlinemsg"][0].get<std::string>())["msg"] == "offline");
        auto batch = request(bob, {{"msgid", OFFLINE_PULL_MSG}});
        CHECK(batch["errno"] == 0 && batch["offlinemsg"].empty());
        // 检查未知业务类型、错误字段类型、整数越界和过深 JSON 都返回错误响应。
        CHECK(request(alice, {{"msgid", 999}})["errno"] == 1);
        CHECK(request(alice, {{"msgid", "bad"}})["errno"] == 1);
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", 9223372036854775807LL}})["errno"] == 1);
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", 4294967297ULL}})["errno"] == 1);
        CHECK(request(alice, {{"msgid", 4294967297ULL}, {"id", a}, {"password", "test"}})["errno"] == 1);
        json nested = json::object();
        for (int i = 0; i < 40; ++i) nested = json{{"nested", std::move(nested)}};
        CHECK(request(alice, {{"msgid", ONE_CHAT_MSG}, {"to", b}, {"msg", nested}})["errno"] == 1);
        alice.node.stop(); bob.node.stop(); server.stop(); service.join();
        if (failure) std::rethrow_exception(failure);
        CHECK(inbox[b].empty() && groupId == 1 && friends.count({a, b}) == 1);
    } catch (...) { server.stop(); if (service.joinable()) service.join(); throw; }
}

// 手动保存并触发完成回调，精确控制离线批次确认成功、失败和入队拒绝的时机。
struct ControlledConnection : Connection {
    std::vector<DeliveryCallback> callbacks;
    std::vector<json> responses;
    bool accept = true;
    std::uint64_t id() const noexcept override { return 1000; }
    bool alive() const noexcept override { return true; }
    bool send(std::string body, DeliveryCallback callback, std::uint32_t) override {
        if (!accept) return false;
        responses.push_back(json::parse(body)); if (callback) callbacks.push_back(std::move(callback)); return true;
    }
};
void offlineAcknowledgments() {
    auto service = ChatService::instance(); service->reset();
    User user(50, "offline-test", "test"); users[50] = user;
    OfflineMsgModel model; model.insert(50, "old");
    auto connection = std::make_shared<ControlledConnection>();
    json login = {{"id", 50}, {"password", "test"}};
    service->login(connection, login, {});
    CHECK(inbox[50].size() == 1 && connection->callbacks.size() == 1);
    // 查询后新增的消息不属于已发送快照，旧批次确认只能删除旧消息。
    model.insert(50, "new"); connection->callbacks[0](true);
    CHECK(inbox[50].size() == 1 && inbox[50][0].body == "new");
    json pull; service->pullOffline(connection, pull, {});
    // 传输失败保留原记录；登录响应无法入队时，还要撤销会话身份和在线状态。
    connection->callbacks.back()(false); CHECK(inbox[50].size() == 1);
    service->clientClose(connection);
    connection->accept = false; service->login(connection, login, {});
    CHECK(connection->userId() == -1 && users[50].getState() == "offline" && inbox[50].size() == 1);
    service->reset();
}
int main() {
    try {
        businessIntegration(); std::cout << "PASS real UDP register/login/authentication, single/group chat, offline pull, JSON validation\n";
        offlineAcknowledgments(); std::cout << "PASS offline snapshot ACK deletion, failure retention and failed login queue rollback\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
