#include"chatservice.hpp"
#include<vector>
using namespace std::placeholders;

namespace
{
    // 发送错误消息的辅助函数
    void sendError(const ConnectionPtr& conn, const RequestContext& context, int ackMsgId, const string& message)
    {
        json response;
        response["msgid"] = ackMsgId;
        response["errno"] = 1;
        response["errmsg"] = message;
        conn->send(response.dump(), {}, context.messageId);
    }
}

// 获取单例对象的接口函数(线程安全的懒汉式单例模式)
ChatService* ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及绑定对应的Handler回调操作
ChatService::ChatService()
{
    if (!_userModel.resetState())
    {
        throw std::runtime_error("database unavailable or schema missing; check CHAT_DB_* and db/schema.sql");
    }

    // 注册消息msgid以及绑定对应的Handler回调操作
    _msgHandlerMap.insert({LOGIN_MSG,  std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGOUT_MSG, std::bind(&ChatService::logout, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({OFFLINE_PULL_MSG, std::bind(&ChatService::pullOffline, this, _1, _2, _3)});

}

// 服务器异常退出，业务重置方法
void ChatService::reset()
{
    // 清理在线用户连接
    {
        lock_guard<mutex> lock(_userConnMapMutex);
        _userConnMap.clear();
        _offlineInFlight.clear();
    }
    // 将数据库中的用户状态全部重置为离线
    if (!_userModel.resetState())
    {
        LOG_ERROR << "failed to reset all user states during shutdown";
    }
}

ChatService::~ChatService() = default;

// 处理登录业务
void ChatService::login(const ConnectionPtr& conn, json& request, RequestContext context)
{
    // 检查json中是否包含必要的字段
    if (!request.contains("id") || !request["id"].is_number_integer())
    {
        sendError(conn, context, LOGIN_MSG_ACK, "登录消息缺少整数类型的id");
        return;
    }
    if (!request.contains("password") || !request["password"].is_string())
    {
        sendError(conn, context, LOGIN_MSG_ACK, "登录消息缺少字符串类型的password");
        return;
    }

    // 获取用户id和密码并查询数据库
    int id = request["id"].get<int>();
    string password = request["password"];
    User user = _userModel.query(id);
    if (user.getId() == id && user.getsPwd() == password)
    {
        if(user.getState() == "online")
        {
            // 用户已经登录，不允许重复登录
            json js;
            js["msgid"] = LOGIN_MSG_ACK;
            js["errno"] = 2;            // 2表示用户已经登录
            js["errmsg"] = "该账户已经登录，请重新输入账号";
            conn->send(js.dump(), {}, context.messageId);
            return;
        }
        // 登录成功
        user.setState("online");
        if (!_userModel.updateState(user)) {
            sendError(conn, context, LOGIN_MSG_ACK, "更新在线状态失败");
            return;
        }
        {
            // 线程安全地存储用户的连接信息
            lock_guard<mutex> lock(_userConnMapMutex);
            _userConnMap[user.getId()] = conn;
        }
        conn->bindUser(id);

        // 发送登录成功的响应消息，栈上json对象隔离，会在函数返回时自动销毁
        json js;
        js["msgid"] = LOGIN_MSG_ACK;
        js["errno"] = 0;
        js["errmsg"] = "登录成功";
        js["id"] = user.getId();
        js["name"] = user.getName();
        
        // 查询用户的好友信息并返回给客户端
        vector<User> friends = _friendModel.query(user.getId());
        if (!friends.empty())
        {
            js["friends"] = json::array();  // 初始化为一个空的JSON数组
            // 遍历好友列表，将每个好友的信息打包为JSON对象并添加到返回JSON中
            for (const auto& friendUser : friends)
            {
                json friendJson;
                friendJson["id"] = friendUser.getId();
                friendJson["name"] = friendUser.getName();
                friendJson["state"] = friendUser.getState();
                js["friends"].push_back(friendJson);
            }
        }

        // 发送登录成功的响应消息给客户端
        if (!sendOffline(conn, std::move(js), id, context)) clientClose(conn);
    }
    else
    {
        // 登录失败
        json js;
        js["msgid"] = LOGIN_MSG_ACK;
        js["errno"] = 1;
        js["errmsg"] = "用户名或密码错误";
        conn->send(js.dump(), {}, context.messageId);
    }
}  

// 处理注册业务     name, password
void ChatService::reg(const ConnectionPtr& conn, json& request, RequestContext context)
{
    if (!request.contains("name") || !request["name"].is_string())
    {
        sendError(conn, context, REG_MSG_ACK, "注册消息缺少字符串类型的name");
        return;
    }
    if (!request.contains("password") || !request["password"].is_string())
    {
        sendError(conn, context, REG_MSG_ACK, "注册消息缺少字符串类型的password");
        return;
    }

    string name = request["name"];
    string password = request["password"];
    User user;
    user.setName(name);
    user.setPassword(password);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json js;
        js["msgid"] = REG_MSG_ACK;
        js["errno"] = 0;
        js["id"] = user.getId();
        // 通过传输无关的连接接口发送；协议层负责分片和确认
        conn->send(js.dump(), {}, context.messageId);  // dump()函数是json库提供的将json对象转换为字符串的接口函数
    }
    else
    {
        // 注册失败
        json js;
        js["msgid"] = REG_MSG_ACK;
        js["errno"] = 1;
        conn->send(js.dump(), {}, context.messageId);
    }

}

// 处理注销业务（用户退出登录，但不一定会话关闭）
void ChatService::logout(const ConnectionPtr& conn, json& js, RequestContext context)
{
    if (!js.contains("id") || !js["id"].is_number_integer())
    {
        sendError(conn, context, LOGOUT_MSG_ACK, "注销消息缺少整数类型的id");
        return;
    }

    // 获取用户id
    int id = js["id"].get<int>();
    bool isCurrentConnection = false;
    {
        // 检查当前连接是否已经登录该用户
        lock_guard<mutex> lock(_userConnMapMutex);
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end() && it->second == conn)
        {
            isCurrentConnection = true;
        }
    }

    if (!isCurrentConnection)
    {
        sendError(conn, context, LOGOUT_MSG_ACK, "当前连接未登录该用户");
        return;
    }

    // 更新用户状态为离线
    User user = _userModel.query(id);
    if (user.getId() != id)
    {
        sendError(conn, context, LOGOUT_MSG_ACK, "用户不存在");
        return;
    }
    user.setState("offline");

    if (!_userModel.updateState(user))
    {
        sendError(conn, context, LOGOUT_MSG_ACK, "注销失败，请稍后重试");
        return;
    }
    {
        lock_guard<mutex> lock(_userConnMapMutex);
        _userConnMap.erase(id);
    }
    conn->bindUser(-1);

    // 发送注销成功的响应消息
    json response;
    response["msgid"] = LOGOUT_MSG_ACK;
    response["errno"] = 0;
    response["errmsg"] = "注销成功";
    conn->send(response.dump(), {}, context.messageId);
}

// 处理客户端会话关闭/退出
void ChatService::clientClose(const ConnectionPtr& conn)
{
    conn->bindUser(-1);
    int id = -1;
    {
        // 线程安全地查找_userConnMap中的用户连接并移除
        lock_guard<mutex> lock(_userConnMapMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 找到了对应的用户连接，获取用户id并从_userConnMap中移除
                id = it->first;
                _userConnMap.erase(it);
                break;
            }
        }
    }   // 释放锁，避免在更新用户状态时持有锁，防止潜在的死锁问题

    if (id == -1)
        return; // 没有找到对应的用户连接，直接返回

    // 更新用户状态为离线
    User user = _userModel.query(id);
    if (user.getId() == id)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

// 根据msgid获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    auto it = _msgHandlerMap.find(msgid);
    // msgid没有对应的事件处理回调，记录错误日志
    if (it == _msgHandlerMap.end())
    {
        LOG_ERROR << "msgid:" << msgid << " can not find handler!";
        // 返回一个默认的处理器，空操作
        return [=](const ConnectionPtr& conn, json&, RequestContext context)
        {
            LOG_DEBUG << "msgid:" << msgid << " can not find handler!";
            sendError(conn, context, ERROR_MSG, "不支持的msgid: " + to_string(msgid));
        };
    }
    else
        return it->second;  // 返回对应的处理器
}

// 一对一聊天业务
void ChatService::oneChat(const ConnectionPtr& conn, json& js, RequestContext context)
{
    int toid = js["to"].get<int>();  // 获取消息接收者的id
    // 获取接收者的连接信息
    ConnectionPtr targetConn;     
    {
        // 线程安全地查找接收者的连接信息
        lock_guard<mutex> lock(_userConnMapMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            targetConn = it->second;
        }
    }   // 释放锁，避免在发送消息时持有锁_userConnMapMutex

    // toid在线，转发消息
    const auto payload = js.dump();
    if (targetConn && targetConn->send(payload, [this, toid, payload](bool delivered) {
        if (!delivered && !_offlineMsgModel.insert(toid, payload)) LOG_ERROR << "failed to persist undelivered chat";
    }))
    {
        json response = {{"msgid", ONE_CHAT_MSG_ACK}, {"errno", 0}, {"status", "queued"}};
        conn->send(response.dump(), {}, context.messageId);
        return;
    }

    // toid不在线，存储离线消息到离线消息表中
    if (!_offlineMsgModel.insert(toid, payload)) {
        sendError(conn, context, ONE_CHAT_MSG_ACK, "离线消息保存失败"); return;
    }
    json response = {{"msgid", ONE_CHAT_MSG_ACK}, {"errno", 0}, {"status", "stored"}};
    conn->send(response.dump(), {}, context.messageId);
    LOG_DEBUG << "用户" << toid << "不在线，消息已存储为离线消息";  

}

// 添加好友业务
void ChatService::addFriend(const ConnectionPtr& conn, json& js, RequestContext context)
{
    int userid = js["id"].get<int>();         // 获取发送者id 
    int friendid = js["friendid"].get<int>(); // 获取好友id
    // friend关系存储到数据库中
    _friendModel.insert(userid, friendid);

    // 发送添加好友成功的响应消息
    json response;
    response["msgid"] = ADD_FRIEND_MSG_ACK;
    response["errno"] = 0;
    response["errmsg"] = "添加好友成功";
    conn->send(response.dump(), {}, context.messageId);
}

/*群组聊天相关业务*/
// 创建群组业务
void ChatService::createGroup(const ConnectionPtr& conn, json& js, RequestContext context)
{   
    int userid = js["id"].get<int>();     // 获取创建者id
    string groupName = js["groupname"];   // 群组名称
    string groupDesc = js["groupdesc"];   // 群组描述
    // 组装群组信息
    Group group;
    group.setName(groupName);
    group.setDesc(groupDesc);

    // 创建群组并将群组信息存储到数据库中
    if (_groupModel.createGroup(group))
    {
        // 创建群组成功，将创建者加入群组并设置为创建者
        _groupModel.addGroup(group.getId(), userid, "creator");

        // 发送创建群组成功的响应消息
        json response;
        response["msgid"] = CREATE_GROUP_MSG_ACK;
        response["errno"] = 0;
        response["errmsg"] = "创建群组成功";
        response["groupid"] = group.getId();
        conn->send(response.dump(), {}, context.messageId);
    }
    else
    {
        // 创建群组失败，发送错误响应消息
        sendError(conn, context, CREATE_GROUP_MSG_ACK, "创建群组失败，请稍后重试");
    }
}

// 加入群组业务
void ChatService::addGroup(const ConnectionPtr& conn, json& js, RequestContext context)
{
    int userid = js["id"].get<int>();         // 获取加入者id
    int groupid = js["groupid"].get<int>();   // 获取群组id

    // 将用户加入群组，设置为普通成员
    _groupModel.addGroup(groupid, userid, "normal");

    // 发送加入群组成功的响应消息
    json response;
    response["msgid"] = ADD_GROUP_MSG_ACK;
    response["errno"] = 0;
    response["errmsg"] = "加入群组成功";
    conn->send(response.dump(), {}, context.messageId);
}

// 群组聊天业务
void ChatService::groupChat(const ConnectionPtr& conn, json& js, RequestContext context)
{
    int userid = js["id"].get<int>();         // 获取发送者id
    int groupid = js["groupid"].get<int>();   // 获取群组id
    // 查询群组成员id，除了发送者自己
    vector<int> groupUsers = _groupModel.queryGroupUsers(userid, groupid);
    // 遍历群组成员，将消息发送给每个成员
    for (int id : groupUsers)
    {
        // 查找成员的连接信息
        ConnectionPtr targetConn;
        {
            lock_guard<mutex> lock(_userConnMapMutex);
            auto it = _userConnMap.find(id);
            if (it != _userConnMap.end()) targetConn = it->second;
        }
            
        // 成员在线，转发消息
        const auto payload = js.dump();
        const bool queued = targetConn && targetConn->send(payload, [this, id, payload](bool delivered) {
            if (!delivered && !_offlineMsgModel.insert(id, payload)) LOG_ERROR << "failed to persist undelivered group message";
        });
        // 成员不在线，存储离线消息到离线消息表中
        if (!queued)
        {
            if (!_offlineMsgModel.insert(id, payload)) {
                sendError(conn, context, GROUP_CHAT_MSG_ACK, "群聊离线消息保存失败，部分成员可能已接收"); return;
            }
            LOG_DEBUG << "用户" << id << "不在线，群聊消息已存储为离线消息";  
        }
    }
    json response = {{"msgid", GROUP_CHAT_MSG_ACK}, {"errno", 0}, {"status", "accepted"}};
    conn->send(response.dump(), {}, context.messageId);
}

bool ChatService::sendOffline(const ConnectionPtr& conn, json response, int userid, RequestContext context)
{
    if (_offlineInFlight.count(userid)) {
        sendError(conn, context, OFFLINE_BATCH_MSG, "上一批离线消息尚未确认，请稍后再试"); return false;
    }
    auto pending = _offlineMsgModel.queryPending(userid);
    vector<std::uint64_t> ids;
    response["offlinemsg"] = json::array();
    std::size_t budget = 0;
    for (const auto& message : pending) {
        const auto encodedSize = json(message.body).dump().size();
        if (budget + encodedSize > 32 * 1024) break;
        response["offlinemsg"].push_back(message.body); ids.push_back(message.id); budget += encodedSize;
    }
    response["offline_more"] = ids.size() < pending.size() || pending.size() == 50;
    if (!pending.empty() && ids.empty()) {
        sendError(conn, context, OFFLINE_BATCH_MSG, "离线消息超出当前业务批次限制"); return false;
    }
    auto payload = response.dump();
    if (payload.size() > chat::net::MaxMessage) {
        sendError(conn, context, ERROR_MSG, "响应超出64KiB限制"); return false;
    }
    if (ids.empty()) return conn->send(std::move(payload), {}, context.messageId);
    _offlineInFlight.emplace(userid, true);
    if (conn->send(std::move(payload), [this, userid, ids](bool delivered) {
        _offlineInFlight.erase(userid);
        if (delivered && !_offlineMsgModel.acknowledge(userid, ids)) LOG_ERROR << "offline acknowledgment failed; messages retained";
    }, context.messageId)) return true;
    _offlineInFlight.erase(userid); return false;
}
void ChatService::pullOffline(const ConnectionPtr& conn, json&, RequestContext context)
{
    sendOffline(conn, {{"msgid", OFFLINE_BATCH_MSG}, {"errno", 0}}, conn->userId(), context);
}
